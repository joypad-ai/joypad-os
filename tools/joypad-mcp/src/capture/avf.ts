// AVFoundation device discovery. Shared by the MCP vision tools and the
// embedded web UI so both can resolve "device 1" / "Charmeleon Camera"
// against the same enumeration.

import { spawn } from "node:child_process";

export interface AvfDevice {
  index: number;
  name: string;
}

export function listAvfVideoDevices(): Promise<AvfDevice[]> {
  return new Promise((resolve, reject) => {
    // ffmpeg writes the device list to stderr; exits non-zero because we pass
    // an empty input. That's expected — parse stderr regardless.
    const proc = spawn("ffmpeg", [
      "-hide_banner",
      "-f", "avfoundation",
      "-list_devices", "true",
      "-i", "",
    ]);
    let stderr = "";
    proc.stderr.on("data", (d: Buffer) => { stderr += d.toString("utf8"); });
    proc.on("error", reject);
    proc.on("exit", () => {
      const devices: AvfDevice[] = [];
      let inVideoSection = false;
      for (const raw of stderr.split("\n")) {
        if (/AVFoundation video devices:/.test(raw)) { inVideoSection = true; continue; }
        if (/AVFoundation audio devices:/.test(raw)) { inVideoSection = false; continue; }
        if (!inVideoSection) continue;
        const m = raw.match(/\[(\d+)\]\s+(.+?)\s*$/);
        if (m) devices.push({ index: parseInt(m[1], 10), name: m[2] });
      }
      resolve(devices);
    });
  });
}

// Resolve a user-supplied device spec (numeric index string, or a substring
// of the device name) to an avfoundation index.
export async function resolveDevice(spec: string | undefined): Promise<AvfDevice> {
  const devices = await listAvfVideoDevices();
  if (devices.length === 0) throw new Error("no AVFoundation video devices found — check camera permissions");
  if (!spec) return devices[0];
  if (/^\d+$/.test(spec)) {
    const idx = parseInt(spec, 10);
    const found = devices.find((d) => d.index === idx);
    if (!found) throw new Error(`no AVFoundation video device with index ${idx}`);
    return found;
  }
  const lower = spec.toLowerCase();
  const found = devices.find((d) => d.name.toLowerCase().includes(lower));
  if (!found) throw new Error(`no AVFoundation video device matching "${spec}" — available: ${devices.map((d) => `[${d.index}] ${d.name}`).join(", ")}`);
  return found;
}
