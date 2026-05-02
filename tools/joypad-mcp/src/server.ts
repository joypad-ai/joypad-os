#!/usr/bin/env node
// Joypad MCP — drives a Joypad OS adapter as a synthetic player.
// stdio MCP transport. One adapter per process.

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { registerConnectionTools } from "./tools/connection.js";
import { registerInputTools } from "./tools/input.js";
import { registerObserveTools } from "./tools/observe.js";
import { registerVisionTools } from "./tools/vision.js";
import { state } from "./state.js";
import { streamingCamera } from "./capture/streaming_camera.js";
import { startWebServer, stopWebServer } from "./web/server.js";

const server = new McpServer(
  { name: "joypad-mcp", version: "0.1.0" },
  {
    instructions:
      "Drive a Joypad OS adapter (RP2040/RP2350/ESP32-S3/nRF52840) as a synthetic player and observe results via screenshots. Typical flow: list_adapters → connect → set_capture_source (if not desktop) → tap/press/hold/axis loop with screenshot/screenshot_diff between actions. Use 'release_all' if a held button gets stuck.",
  },
);

registerConnectionTools(server);
registerInputTools(server);
registerObserveTools(server);
registerVisionTools(server);

async function main(): Promise<void> {
  const transport = new StdioServerTransport();
  await server.connect(transport);
  startWebServer();

  const shutdown = async () => {
    stopWebServer();
    streamingCamera.stop();
    if (state.conn) {
      try { await state.conn.close(); } catch {}
    }
    process.exit(0);
  };
  process.on("SIGINT", shutdown);
  process.on("SIGTERM", shutdown);
}

main().catch((err) => {
  console.error("[joypad-mcp] fatal:", err);
  process.exit(1);
});
