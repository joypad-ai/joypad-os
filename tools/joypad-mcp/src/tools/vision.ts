// Screen capture. Two backends today:
//   - desktop: macOS `screencapture` for laptop screen / emulator window
//   - webcam:  ffmpeg avfoundation for any UVC device (USB webcam, HDMI
//              capture card, iPhone Continuity Camera, etc.)
// Linux/Windows desktop capture is not implemented yet.

import { z } from "zod";
import { spawn } from "node:child_process";
import { mkdtempSync, readFileSync, unlinkSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { streamingCamera } from "../capture/streaming_camera.js";
import { analyzeImage, GAME_PROMPTS } from "../capture/moondream.js";
import { listAvfVideoDevices, resolveDevice, AvfDevice } from "../capture/avf.js";

interface CaptureSettings {
  source: "desktop" | "webcam";
  region?: { x: number; y: number; w: number; h: number };
  device?: string; // avfoundation index or name substring
  maxDim: number;
}

const settings: CaptureSettings = {
  source: "desktop",
  maxDim: 1024,
};

let lastHash: string | null = null;

function captureMacosDesktop(region?: CaptureSettings["region"]): Promise<Buffer> {
  return new Promise((resolve, reject) => {
    const dir = mkdtempSync(join(tmpdir(), "joypad-mcp-"));
    const out = join(dir, "shot.png");
    const args = ["-x", "-t", "png"];
    if (region) args.push("-R", `${region.x},${region.y},${region.w},${region.h}`);
    args.push(out);
    const proc = spawn("screencapture", args, { stdio: "ignore" });
    proc.on("error", reject);
    proc.on("exit", (code) => {
      if (code !== 0) return reject(new Error(`screencapture exited ${code}`));
      try {
        const buf = readFileSync(out);
        try { unlinkSync(out); } catch {}
        resolve(buf);
      } catch (e) {
        reject(e);
      }
    });
  });
}


async function captureMacosWebcam(deviceIndex: number): Promise<{ buf: Buffer; mime: string }> {
  // Lazy-start the streaming camera or switch device. Subsequent calls hit
  // the cached latest frame in memory — no per-shot ffmpeg handshake.
  if (!streamingCamera.isOpen() || streamingCamera.currentDevice() !== deviceIndex) {
    streamingCamera.start(deviceIndex);
  }
  const { jpeg } = await streamingCamera.getFrame();
  return { buf: jpeg, mime: "image/jpeg" };
}

// djb2 over the PNG bytes — coarse change-detection. Not a perceptual hash;
// good enough for "did the screen change at all".
function quickHash(buf: Buffer): string {
  let h = 5381;
  for (let i = 0; i < buf.length; i += 64) h = (((h << 5) + h) ^ buf[i]) >>> 0;
  return `${buf.length}:${h.toString(16)}`;
}

async function capture(): Promise<{ buf: Buffer; mime: string; hash: string }> {
  if (process.platform !== "darwin") {
    throw new Error("capture is currently macOS-only");
  }
  if (settings.source === "desktop") {
    const png = await captureMacosDesktop(settings.region);
    return { buf: png, mime: "image/png", hash: quickHash(png) };
  }
  const dev = await resolveDevice(settings.device);
  const { buf, mime } = await captureMacosWebcam(dev.index);
  return { buf, mime, hash: quickHash(buf) };
}

export function registerVisionTools(server: McpServer): void {
  server.tool(
    "list_cameras",
    "List AVFoundation video input devices (built-in cameras, USB webcams, HDMI capture cards, Continuity Camera, screen-capture devices). Returns index + human name; pass either to `set_capture_source({source:'webcam', device:...})`.",
    {},
    async () => {
      const devices = await listAvfVideoDevices();
      return { content: [{ type: "text", text: JSON.stringify({ devices }, null, 2) }] };
    },
  );

  server.tool(
    "set_capture_source",
    "Configure where `screenshot` reads from. `desktop` uses the OS screen grabber (macOS `screencapture`); `webcam` uses `ffmpeg avfoundation` against any UVC device. `region` crops to a rectangle (in screen coords for desktop, in raw camera pixels for webcam).",
    {
      source: z.enum(["desktop", "webcam"]),
      device: z.string().optional().describe("AVFoundation device index ('0', '1', ...) or substring of the name. First video device used if omitted. Use `list_cameras` to enumerate."),
      region: z
        .object({ x: z.number().int(), y: z.number().int(), w: z.number().int().positive(), h: z.number().int().positive() })
        .optional(),
      max_dim: z.number().int().positive().optional(),
    },
    async ({ source, device, region, max_dim }) => {
      settings.source = source;
      settings.device = device;
      settings.region = region;
      if (max_dim) settings.maxDim = max_dim;
      lastHash = null;
      // For webcam, start the persistent ffmpeg stream now so subsequent
      // screenshot() calls hit a warm frame buffer (~30ms) instead of paying
      // the avfoundation handshake (~1-2s) per shot.
      if (source === "webcam") {
        const dev = await resolveDevice(device);
        if (!streamingCamera.isOpen() || streamingCamera.currentDevice() !== dev.index) {
          streamingCamera.start(dev.index);
        }
      } else {
        streamingCamera.stop();
      }
      return { content: [{ type: "text", text: JSON.stringify({ ok: true, settings, camera: streamingCamera.stats() }, null, 2) }] };
    },
  );

  server.tool(
    "screenshot",
    "Take a screenshot of the configured source and return it as an image. The assistant sees the pixels on its next turn.",
    {},
    async () => {
      const { buf, mime, hash } = await capture();
      lastHash = hash;
      return {
        content: [
          { type: "image", data: buf.toString("base64"), mimeType: mime },
          { type: "text", text: JSON.stringify({ bytes: buf.length, hash, source: settings.source, mime }) },
        ],
      };
    },
  );

  server.tool(
    "analyze_game_state",
    "Send the latest webcam/desktop frame to a LOCAL Moondream model (via Ollama) and return its text/JSON description. The image bytes never leave the laptop and never enter your context — you receive only the structured text the local model produced. Use this instead of `screenshot` for tight gameplay loops where multimodal cost is the bottleneck. Requires `ollama serve` running and `ollama pull moondream`.",
    {
      game: z.enum(["tetris", "generic"]).optional().describe("Pick a built-in prompt template; defaults to generic if no `prompt` given"),
      prompt: z.string().optional().describe("Override prompt sent to Moondream"),
      model: z.string().optional().describe("Ollama model tag (default 'moondream')"),
    },
    async ({ game, prompt, model }) => {
      const { buf, mime } = await capture();
      // Moondream wants a JPEG. Desktop screencapture gives PNG; if we ever
      // need to pre-convert we can pull in `sharp`. For now JPEG-from-webcam
      // is the common path; Moondream/Ollama also accepts PNGs.
      const finalPrompt = prompt ?? GAME_PROMPTS[game ?? "generic"] ?? GAME_PROMPTS.generic;
      try {
        const result = await analyzeImage({ jpeg: buf, prompt: finalPrompt, model });
        return {
          content: [
            {
              type: "text",
              text: JSON.stringify(
                {
                  ok: true,
                  text: result.text,
                  model: result.model,
                  ms: result.ms,
                  source: settings.source,
                  mime,
                  bytes: buf.length,
                  eval_count: result.eval_count,
                },
                null,
                2,
              ),
            },
          ],
        };
      } catch (e: any) {
        return {
          content: [{ type: "text", text: JSON.stringify({ ok: false, error: String(e?.message ?? e) }) }],
          isError: true,
        };
      }
    },
  );

  server.tool(
    "screenshot_diff",
    "Take a screenshot only if it has changed since the last `screenshot` or `screenshot_diff` call. Saves tokens during cutscenes/static menus. Returns `{changed:false}` if unchanged.",
    {},
    async () => {
      const { buf, mime, hash } = await capture();
      if (hash === lastHash) {
        return { content: [{ type: "text", text: JSON.stringify({ changed: false, hash }) }] };
      }
      lastHash = hash;
      return {
        content: [
          { type: "image", data: buf.toString("base64"), mimeType: mime },
          { type: "text", text: JSON.stringify({ changed: true, bytes: buf.length, hash, source: settings.source, mime }) },
        ],
      };
    },
  );
}
