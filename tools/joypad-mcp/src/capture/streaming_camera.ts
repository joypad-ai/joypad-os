// Persistent webcam stream. Spawns ffmpeg once, parses an MJPEG stream off
// stdout, and keeps the latest complete frame in memory. screenshot() then
// returns that frame immediately instead of paying the avfoundation handshake
// cost (~1-2s on Continuity Camera) per call.

import { spawn, ChildProcess } from "node:child_process";

const SOI = 0xffd8;
const EOI = 0xffd9;

interface LatestFrame { jpeg: Buffer; ts: number }

export class StreamingCamera {
  private proc: ChildProcess | null = null;
  private deviceIndex = -1;
  private buf: Buffer = Buffer.alloc(0);
  private latest: LatestFrame | null = null;
  private waiters: Array<(frame: Buffer) => void> = [];
  private startedAt = 0;
  private framesReceived = 0;

  isOpen(): boolean {
    return this.proc !== null && !this.proc.killed;
  }

  currentDevice(): number {
    return this.deviceIndex;
  }

  // Wire up SIGINT/SIGTERM/exit handlers exactly once per process so any path
  // out of Node — clean shutdown, parent-death, uncaughtException — leaves no
  // orphan ffmpeg subprocesses holding the camera. Without this, every
  // Claude Code restart leaks an ffmpeg that keeps the iPhone's camera-active
  // indicator lit until manually killed.
  private static guardInstalled = false;
  private installExitGuard(): void {
    if (StreamingCamera.guardInstalled) return;
    StreamingCamera.guardInstalled = true;
    const kill = () => { try { this.proc?.kill("SIGKILL"); } catch {} };
    process.once("exit", kill);
    process.once("SIGINT", () => { kill(); process.exit(130); });
    process.once("SIGTERM", () => { kill(); process.exit(143); });
    process.once("SIGHUP", () => { kill(); process.exit(129); });
    process.once("uncaughtException", (e) => { kill(); console.error(e); process.exit(1); });
  }

  stats(): { open: boolean; device: number; frames: number; uptime_ms: number; last_frame_ms_ago: number | null } {
    return {
      open: this.isOpen(),
      device: this.deviceIndex,
      frames: this.framesReceived,
      uptime_ms: this.startedAt ? Date.now() - this.startedAt : 0,
      last_frame_ms_ago: this.latest ? Date.now() - this.latest.ts : null,
    };
  }

  start(deviceIndex: number): void {
    this.stop();
    this.deviceIndex = deviceIndex;
    this.buf = Buffer.alloc(0);
    this.latest = null;
    this.framesReceived = 0;
    this.startedAt = Date.now();
    this.installExitGuard();

    // -q:v 5 keeps JPEGs small without crushing the image. The native res
    // (whatever Continuity Camera provides) is preserved — game text needs
    // to stay readable.
    this.proc = spawn(
      "ffmpeg",
      [
        "-hide_banner", "-loglevel", "error",
        "-f", "avfoundation",
        "-framerate", "30",       // input rate (avfoundation requires this)
        "-i", String(deviceIndex),
        "-r", "5",                 // throttle output to 5 fps so we don't
                                   // saturate the Node event loop parsing
                                   // 30 MJPEGs/s when we screenshot every
                                   // few seconds. Serial I/O coexists fine
                                   // at this rate.
        "-f", "image2pipe",
        "-vcodec", "mjpeg",
        "-q:v", "5",
        "pipe:1",
      ],
      { stdio: ["ignore", "pipe", "pipe"] },
    );
    this.proc.stdout!.on("data", (chunk: Buffer) => this.onChunk(chunk));
    this.proc.stderr!.on("data", () => { /* suppress */ });
    this.proc.on("exit", () => { this.proc = null; });
    this.proc.on("error", () => { this.proc = null; });
  }

  stop(): void {
    if (this.proc) {
      try { this.proc.kill("SIGTERM"); } catch {}
      this.proc = null;
    }
    this.buf = Buffer.alloc(0);
    this.latest = null;
    // Resolve any pending waiters with an error
    const waiters = this.waiters;
    this.waiters = [];
    for (const w of waiters) {
      try { w(Buffer.alloc(0)); } catch {}
    }
  }

  private onChunk(chunk: Buffer): void {
    this.buf = this.buf.length === 0 ? chunk : Buffer.concat([this.buf, chunk]);
    // Walk JPEG frame boundaries. SOI = 0xFFD8, EOI = 0xFFD9.
    while (this.buf.length >= 4) {
      let soi = -1;
      for (let i = 0; i < this.buf.length - 1; i++) {
        if (this.buf[i] === 0xff && this.buf[i + 1] === 0xd8) { soi = i; break; }
      }
      if (soi < 0) { this.buf = Buffer.alloc(0); break; }
      let eoi = -1;
      for (let i = soi + 2; i < this.buf.length - 1; i++) {
        if (this.buf[i] === 0xff && this.buf[i + 1] === 0xd9) { eoi = i; break; }
      }
      if (eoi < 0) {
        // Drop bytes before SOI; keep the partial frame
        if (soi > 0) this.buf = this.buf.subarray(soi);
        break;
      }
      const frame = Buffer.from(this.buf.subarray(soi, eoi + 2));
      this.latest = { jpeg: frame, ts: Date.now() };
      this.framesReceived++;
      const waiters = this.waiters;
      this.waiters = [];
      for (const w of waiters) {
        try { w(frame); } catch {}
      }
      this.buf = this.buf.subarray(eoi + 2);
    }
  }

  async getFrame(timeoutMs = 3000): Promise<{ jpeg: Buffer; ts: number }> {
    if (!this.isOpen()) throw new Error("streaming camera not started — call set_capture_source({source:'webcam', ...}) first");
    if (this.latest) return { jpeg: this.latest.jpeg, ts: this.latest.ts };
    // First frame hasn't arrived yet (ffmpeg startup). Wait for one.
    const ts = Date.now();
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        const idx = this.waiters.indexOf(waiter);
        if (idx >= 0) this.waiters.splice(idx, 1);
        reject(new Error(`camera frame timeout after ${timeoutMs}ms`));
      }, timeoutMs);
      const waiter = (frame: Buffer) => {
        clearTimeout(timer);
        if (frame.length === 0) reject(new Error("camera stopped"));
        else resolve({ jpeg: frame, ts });
      };
      this.waiters.push(waiter);
    });
  }
}

export const streamingCamera = new StreamingCamera();
