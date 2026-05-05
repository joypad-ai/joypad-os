// Local control UI for joypad-mcp. Single-process HTTP server embedded in
// the MCP runtime so the user can monitor and test the rig:
//   - watch the latest webcam frame
//   - pick / pause / resume a camera
//   - connect / disconnect the joypad adapter
//   - send manual button presses for testing
//
// Game-specific autoplay loops (tetris.ts etc.) are intentionally NOT
// wired here — those source files stay on disk for future reference but
// aren't loaded. Demo flow is conversational: Claude Code drives the
// MCP from chat using the manual press / screenshot tools.
//
// stdio MCP and this HTTP server coexist in one Node process — no extra
// launcher, no extra port to remember. Default URL: http://localhost:11600

import { createServer, IncomingMessage, ServerResponse } from "node:http";
import { readFileSync, existsSync, mkdirSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { homedir } from "node:os";

import { streamingCamera } from "../capture/streaming_camera.js";
import { state as joypadState } from "../state.js";
import { listAvfVideoDevices, resolveDevice } from "../capture/avf.js";
import { parseButtons } from "../buttons.js";
import { encodeInputEvent, INPUT_TYPE_GAMEPAD, NEUTRAL_ANALOG, PktType } from "../protocol.js";
import { Connection, DEFAULT_BAUD } from "../transport.js";
import { SerialPort } from "serialport";

const __dirname = dirname(fileURLToPath(import.meta.url));
const UI_HTML = join(__dirname, "ui.html");

const CONFIG_DIR = join(homedir(), ".joypad-mcp");
const CONFIG_FILE = join(CONFIG_DIR, "calibration.json");

// Persisted across restarts: which camera + joypad to auto-attach on boot.
// Filename kept as calibration.json for backward compat with existing saves
// even though there's no calibration to speak of in this build.
export interface SessionConfig {
  camera_device?: number;
  joypad_port?: string;
  joypad_baud?: number;
  saved_at?: number;
}

export const config: SessionConfig = loadConfig();

function loadConfig(): SessionConfig {
  try {
    if (existsSync(CONFIG_FILE)) {
      return JSON.parse(readFileSync(CONFIG_FILE, "utf8"));
    }
  } catch {}
  return {};
}

function saveConfig(data: SessionConfig): void {
  data.saved_at = Date.now();
  if (!existsSync(CONFIG_DIR)) mkdirSync(CONFIG_DIR, { recursive: true });
  writeFileSync(CONFIG_FILE, JSON.stringify(data, null, 2));
}

function setCors(res: ServerResponse): void {
  res.setHeader("access-control-allow-origin", "*");
  res.setHeader("access-control-allow-methods", "GET,POST,OPTIONS");
  res.setHeader("access-control-allow-headers", "content-type");
}

async function readJsonBody<T = any>(req: IncomingMessage): Promise<T> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = [];
    req.on("data", (c) => chunks.push(c));
    req.on("end", () => {
      try {
        resolve(JSON.parse(Buffer.concat(chunks).toString("utf8")));
      } catch (e) {
        reject(e);
      }
    });
    req.on("error", reject);
  });
}

function json(res: ServerResponse, code: number, body: unknown): void {
  setCors(res);
  res.writeHead(code, { "content-type": "application/json" });
  res.end(JSON.stringify(body));
}

async function handle(req: IncomingMessage, res: ServerResponse): Promise<void> {
  if (req.method === "OPTIONS") {
    setCors(res);
    res.writeHead(204);
    res.end();
    return;
  }
  const url = new URL(req.url ?? "/", "http://localhost");
  const path = url.pathname;

  if (req.method === "GET" && path === "/") {
    setCors(res);
    res.writeHead(200, { "content-type": "text/html; charset=utf-8" });
    res.end(readFileSync(UI_HTML));
    return;
  }

  if (req.method === "GET" && path === "/api/frame.jpg") {
    if (!streamingCamera.isOpen()) {
      json(res, 503, { error: "camera not running" });
      return;
    }
    try {
      const { jpeg } = await streamingCamera.getFrame(2000);
      setCors(res);
      res.writeHead(200, { "content-type": "image/jpeg", "cache-control": "no-store" });
      res.end(jpeg);
    } catch (e: any) {
      json(res, 504, { error: String(e?.message ?? e) });
    }
    return;
  }

  if (req.method === "GET" && path === "/api/cameras") {
    try {
      const devices = await listAvfVideoDevices();
      json(res, 200, { devices });
    } catch (e: any) {
      json(res, 500, { error: String(e?.message ?? e) });
    }
    return;
  }

  if (req.method === "POST" && path === "/api/camera") {
    try {
      const body = await readJsonBody<{ device?: number | string }>(req);
      const dev = await resolveDevice(body.device != null ? String(body.device) : undefined);
      if (!streamingCamera.isOpen() || streamingCamera.currentDevice() !== dev.index) {
        streamingCamera.start(dev.index);
      }
      config.camera_device = dev.index;
      saveConfig(config);
      json(res, 200, { ok: true, device: dev, camera: streamingCamera.stats() });
    } catch (e: any) {
      json(res, 400, { error: String(e?.message ?? e) });
    }
    return;
  }

  if (req.method === "POST" && path === "/api/camera/pause") {
    streamingCamera.stop();
    json(res, 200, { ok: true, camera: streamingCamera.stats() });
    return;
  }

  if (req.method === "POST" && path === "/api/camera/resume") {
    try {
      const dev = config.camera_device;
      if (dev == null) {
        json(res, 400, { error: "no camera saved — pick one first" });
        return;
      }
      if (!streamingCamera.isOpen() || streamingCamera.currentDevice() !== dev) {
        streamingCamera.start(dev);
      }
      json(res, 200, { ok: true, camera: streamingCamera.stats() });
    } catch (e: any) {
      json(res, 400, { error: String(e?.message ?? e) });
    }
    return;
  }

  if (req.method === "GET" && path === "/api/state") {
    json(res, 200, {
      camera: streamingCamera.stats(),
      joypad: {
        connected: !!joypadState.conn,
        port: joypadState.conn?.path,
        last_command_ms_ago: joypadState.lastCommandAt ? Date.now() - joypadState.lastCommandAt : null,
      },
      config,
    });
    return;
  }

  if (req.method === "GET" && path === "/api/joypad/adapters") {
    try {
      const ports = await SerialPort.list();
      const candidates = ports
        .map((p) => ({
          path: p.path,
          manufacturer: p.manufacturer,
          vendorId: p.vendorId,
          productId: p.productId,
        }))
        .filter((p) => p.path.includes("usbmodem") || p.path.includes("ttyACM") || p.path.includes("ttyUSB"));
      json(res, 200, { adapters: candidates });
    } catch (e: any) {
      json(res, 500, { error: String(e?.message ?? e) });
    }
    return;
  }

  if (req.method === "POST" && path === "/api/joypad/connect") {
    try {
      const body = await readJsonBody<{ port?: string; baud?: number }>(req);
      if (!body.port) { json(res, 400, { error: "port required" }); return; }
      const baud = body.baud ?? DEFAULT_BAUD;
      if (joypadState.conn) {
        try { await joypadState.conn.close(); } catch {}
        joypadState.reset();
      }
      const conn = new Connection(body.port, baud);
      await conn.open();
      joypadState.attach(conn);
      joypadState.lastCommandAt = Date.now();
      config.joypad_port = body.port;
      config.joypad_baud = baud;
      saveConfig(config);
      json(res, 200, { ok: true, port: body.port, baud });
    } catch (e: any) {
      json(res, 400, { error: String(e?.message ?? e) });
    }
    return;
  }

  if (req.method === "GET" && path === "/api/joypad/log") {
    if (!joypadState.conn) { json(res, 503, { error: "joypad not connected" }); return; }
    const since = parseInt(url.searchParams.get("since_ms") ?? "60000", 10);
    const grep = url.searchParams.get("grep") || undefined;
    json(res, 200, { lines: joypadState.conn.recentLogs(since, grep) });
    return;
  }

  if (req.method === "POST" && path === "/api/joypad/disconnect") {
    try {
      if (joypadState.conn) {
        try { await joypadState.conn.close(); } catch {}
        joypadState.reset();
      }
      json(res, 200, { ok: true });
    } catch (e: any) {
      json(res, 400, { error: String(e?.message ?? e) });
    }
    return;
  }

  if (req.method === "POST" && path === "/api/press") {
    if (!joypadState.conn) { json(res, 503, { error: "joypad not connected" }); return; }
    try {
      const body = await readJsonBody<{ buttons?: string; duration_ms?: number }>(req);
      const mask = parseButtons(body.buttons ?? "");
      const dur = Math.max(20, Math.min(2000, body.duration_ms ?? 100));
      const conn = joypadState.requireConn();
      const send = (buttons: number) =>
        conn.send(
          PktType.INPUT_EVENT,
          encodeInputEvent({
            playerIndex: 0,
            deviceType: INPUT_TYPE_GAMEPAD,
            buttons,
            analog: [...NEUTRAL_ANALOG] as [number, number, number, number, number, number],
            deltaX: 0,
            deltaY: 0,
          }),
        );
      await send(mask);
      joypadState.lastCommandAt = Date.now();
      await new Promise<void>((r) => setTimeout(r, dur));
      await send(0);
      json(res, 200, { ok: true, buttons: body.buttons, duration_ms: dur });
    } catch (e: any) {
      json(res, 400, { error: String(e?.message ?? e) });
    }
    return;
  }

  json(res, 404, { error: "not found" });
}

let server: ReturnType<typeof createServer> | null = null;

export function startWebServer(port = parseInt(process.env.JOYPAD_WEB_PORT ?? "11600", 10)): void {
  server = createServer((req, res) => {
    handle(req, res).catch((e) => {
      try { json(res, 500, { error: String(e?.message ?? e) }); } catch {}
    });
  });
  server.on("error", (e: any) => {
    console.error(`[joypad-mcp] web UI disabled: ${String(e?.message ?? e)}`);
    server = null;
  });
  server.listen(port, "127.0.0.1", () => {
    // Avoid stdout — that's the MCP transport. stderr is fine.
    console.error(`[joypad-mcp] web UI on http://localhost:${port}`);
  });
  // Sweep any ffmpeg orphans left over from a prior Claude Code session
  // (those have parent PID 1 and our exit handler can't reach them).
  // Without this, auto-start will hit "camera busy" because the previous
  // process still holds it.
  streamingCamera.sweepOrphans();

  // Auto-start the saved camera so the UI has a frame waiting on first load.
  if (config.camera_device != null && !streamingCamera.isOpen()) {
    try {
      streamingCamera.start(config.camera_device);
    } catch (e) {
      console.error(`[joypad-mcp] auto-start camera failed:`, e);
    }
  }

  // Auto-reconnect to the saved joypad adapter so manual button presses
  // from the web UI work without first running the MCP `connect` tool.
  if (config.joypad_port && !joypadState.conn) {
    (async () => {
      try {
        const conn = new Connection(config.joypad_port!, config.joypad_baud ?? DEFAULT_BAUD);
        await conn.open();
        joypadState.attach(conn);
        console.error(`[joypad-mcp] reconnected ${config.joypad_port}`);
      } catch (e) {
        console.error(`[joypad-mcp] auto-reconnect joypad failed:`, e);
      }
    })();
  }
}

export function stopWebServer(): void {
  server?.close();
  server = null;
}
