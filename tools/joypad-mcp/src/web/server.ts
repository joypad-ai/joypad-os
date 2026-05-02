// Local control UI for joypad-mcp. Single-process HTTP server embedded in
// the MCP runtime so the user can:
//   - watch the latest webcam frame
//   - click-calibrate the matrix region for game-specific parsers
//   - start/stop autoplay loops without touching a tool from chat
//   - see live state (camera fps, parsed game state, joypad connection)
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
import { startTetrisLoop, AutoplayHandle } from "../autoplay/tetris.js";
import { parseMatrix } from "../games/tetris/parser.js";
import { parseButtons } from "../buttons.js";
import { encodeInputEvent, INPUT_TYPE_GAMEPAD, NEUTRAL_ANALOG, PktType } from "../protocol.js";

const __dirname = dirname(fileURLToPath(import.meta.url));
const UI_HTML = join(__dirname, "ui.html");

const CONFIG_DIR = join(homedir(), ".joypad-mcp");
const CALIBRATION_FILE = join(CONFIG_DIR, "calibration.json");

export interface CalibrationData {
  matrix?: { corners: [number, number][] }; // 4 corners in source-image pixels: TL, TR, BR, BL
  next_preview?: { x: number; y: number; w: number; h: number };
  source_size?: { w: number; h: number };
  camera_device?: number;
  game?: string;
  saved_at?: number;
}

export const calibration: CalibrationData = loadCalibration();

function loadCalibration(): CalibrationData {
  try {
    if (existsSync(CALIBRATION_FILE)) {
      return JSON.parse(readFileSync(CALIBRATION_FILE, "utf8"));
    }
  } catch {}
  return {};
}

function saveCalibration(data: CalibrationData): void {
  data.saved_at = Date.now();
  if (!existsSync(CONFIG_DIR)) mkdirSync(CONFIG_DIR, { recursive: true });
  writeFileSync(CALIBRATION_FILE, JSON.stringify(data, null, 2));
}

// Autoplay state — minimal placeholder until the parser/heuristic land.
export interface AutoplayStatus {
  running: boolean;
  game: string | null;
  pieces_placed: number;
  started_at: number | null;
  last_tick_ms: number;
  last_error: string | null;
}

const autoplay: AutoplayStatus = {
  running: false,
  game: null,
  pieces_placed: 0,
  started_at: null,
  last_tick_ms: 0,
  last_error: null,
};

let activeHandle: AutoplayHandle | null = null;

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

  if (req.method === "GET" && path === "/api/parse") {
    if (!streamingCamera.isOpen()) { json(res, 503, { error: "camera not running" }); return; }
    if (!calibration.matrix?.corners) { json(res, 400, { error: "no matrix calibration" }); return; }
    try {
      const { jpeg } = await streamingCamera.getFrame(2000);
      const parsed = await parseMatrix(jpeg, { corners: calibration.matrix.corners });
      json(res, 200, {
        ms: parsed.ms,
        heights: parsed.heights,
        cells: parsed.cells.map((row) => row.map((b) => (b ? 1 : 0)).join("")),
        rgb_sample: parsed.rgb?.slice(0, 5).map((row) => row.slice(0, 5)),
      });
    } catch (e: any) {
      json(res, 500, { error: String(e?.message ?? e) });
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
      calibration.camera_device = dev.index;
      saveCalibration(calibration);
      json(res, 200, { ok: true, device: dev, camera: streamingCamera.stats() });
    } catch (e: any) {
      json(res, 400, { error: String(e?.message ?? e) });
    }
    return;
  }

  if (req.method === "GET" && path === "/api/state") {
    const live = activeHandle?.status();
    if (live) {
      autoplay.running = live.running;
      autoplay.pieces_placed = live.pieces_placed;
      autoplay.last_tick_ms = live.last_tick_ms;
      autoplay.last_error = live.last_error;
    }
    json(res, 200, {
      camera: streamingCamera.stats(),
      joypad: {
        connected: !!joypadState.conn,
        port: joypadState.conn?.path,
        last_command_ms_ago: joypadState.lastCommandAt ? Date.now() - joypadState.lastCommandAt : null,
      },
      calibration,
      autoplay: { ...autoplay, ...(live ?? {}) },
    });
    return;
  }

  if (req.method === "POST" && path === "/api/calibrate") {
    try {
      const body = await readJsonBody<CalibrationData>(req);
      Object.assign(calibration, body);
      saveCalibration(calibration);
      json(res, 200, { ok: true, calibration });
    } catch (e: any) {
      json(res, 400, { error: String(e?.message ?? e) });
    }
    return;
  }

  if (req.method === "POST" && path === "/api/autoplay/start") {
    try {
      const body = await readJsonBody<{ game?: string }>(req);
      activeHandle?.stop();
      autoplay.running = true;
      autoplay.game = body.game ?? "tetris";
      autoplay.pieces_placed = 0;
      autoplay.started_at = Date.now();
      autoplay.last_error = null;
      activeHandle = startTetrisLoop(calibration.matrix);
      json(res, 200, { ok: true, autoplay });
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

  if (req.method === "POST" && path === "/api/autoplay/stop") {
    activeHandle?.stop();
    activeHandle = null;
    autoplay.running = false;
    json(res, 200, { ok: true, autoplay });
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
  server.listen(port, "127.0.0.1", () => {
    // Avoid stdout — that's the MCP transport. stderr is fine.
    console.error(`[joypad-mcp] web UI on http://localhost:${port}`);
  });
  // Auto-start the saved camera so the UI has a frame waiting on first load.
  if (calibration.camera_device != null && !streamingCamera.isOpen()) {
    try {
      streamingCamera.start(calibration.camera_device);
    } catch (e) {
      console.error(`[joypad-mcp] auto-start camera failed:`, e);
    }
  }
}

export function stopWebServer(): void {
  server?.close();
  server = null;
}

export function getAutoplay(): AutoplayStatus {
  return autoplay;
}
