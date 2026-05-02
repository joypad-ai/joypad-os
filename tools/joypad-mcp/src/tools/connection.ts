import { z } from "zod";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { SerialPort } from "serialport";
import { Connection, DEFAULT_BAUD } from "../transport.js";
import { state } from "../state.js";
import {
  PktType,
  INPUT_TYPE_GAMEPAD,
  encodeInputEvent,
  NEUTRAL_ANALOG,
} from "../protocol.js";

// USB VID/PID heuristic for boards that commonly run Joypad OS.
// We don't *require* a match — the user can pass any port — this just helps
// `list_adapters` filter to plausible candidates.
const KNOWN_BOARDS: { vid: string; pid?: string; name: string }[] = [
  { vid: "239a", pid: "80f4", name: "Adafruit KB2040" },
  { vid: "239a", pid: "8108", name: "Adafruit KB2040 (CDC)" },
  { vid: "239a", pid: "8123", name: "Adafruit Feather RP2040" },
  { vid: "2e8a", pid: "0003", name: "Raspberry Pi Pico (BOOTSEL)" },
  { vid: "2e8a", pid: "000a", name: "Raspberry Pi Pico (CDC)" },
  { vid: "2e8a", pid: "000c", name: "Raspberry Pi Pico W" },
  { vid: "2e8a", pid: "0009", name: "Raspberry Pi Pico 2" },
  { vid: "2e8a", pid: "0010", name: "Raspberry Pi Pico 2 W" },
  { vid: "303a", name: "Espressif ESP32-S3" },
  { vid: "1915", pid: "521f", name: "Seeed XIAO nRF52840" },
];

function describePort(p: { manufacturer?: string; vendorId?: string; productId?: string }): string | undefined {
  const vid = p.vendorId?.toLowerCase();
  const pid = p.productId?.toLowerCase();
  for (const k of KNOWN_BOARDS) {
    if (vid === k.vid && (!k.pid || pid === k.pid)) return k.name;
  }
  return p.manufacturer;
}

export function registerConnectionTools(server: McpServer): void {
  server.tool(
    "list_adapters",
    "List serial ports that look like Joypad OS adapters (RP2040/RP2350, ESP32-S3, nRF52840, KB2040 USB-UART bridges). Picks candidates by USB VID/PID; the assistant should then call `connect` with the chosen port.",
    {},
    async () => {
      const ports = await SerialPort.list();
      const candidates = ports
        .map((p) => ({
          path: p.path,
          manufacturer: p.manufacturer,
          vendorId: p.vendorId,
          productId: p.productId,
          serialNumber: p.serialNumber,
          looks_like: describePort(p),
        }))
        .filter((p) => p.looks_like || (p.path.includes("usbmodem") || p.path.includes("ttyACM") || p.path.includes("ttyUSB")));
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify({ adapters: candidates, all_ports: ports.map((p) => p.path) }, null, 2),
          },
        ],
      };
    },
  );

  server.tool(
    "connect",
    "Open a serial connection to an adapter. Sends UART INPUT_EVENT packets that the firmware's `uart_host` (in NORMAL mode) submits to the router as a synthetic controller on dev_addr 0xD0+slot. The firmware must be built with CONFIG_UART_HOST=1 and the UART pins wired to a host-side serial bridge.",
    {
      port: z.string().describe("Serial port path, e.g. /dev/cu.usbmodem1101"),
      baud: z.number().int().positive().optional().describe(`Baud rate (default ${DEFAULT_BAUD})`),
      slot: z.number().int().min(0).max(7).optional().describe("Player slot 0-7 (default 0)"),
    },
    async ({ port, baud, slot }) => {
      if (state.conn) {
        try {
          await state.conn.close();
        } catch {}
        state.reset();
      }
      const conn = new Connection(port, baud ?? DEFAULT_BAUD);
      await conn.open();
      state.attach(conn);
      const s = slot ?? 0;
      // No firmware handshake yet (uart_host is RX-only). Just record the
      // slot and let `tap`/`hold`/etc. start sending INPUT_EVENT packets.
      state.lastCommandAt = Date.now();
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify(
              {
                ok: true,
                port,
                baud: baud ?? DEFAULT_BAUD,
                slot: s,
                note: "Connected. Subsequent input tools will submit as a synthetic player on this slot.",
              },
              null,
              2,
            ),
          },
        ],
      };
    },
  );

  server.tool(
    "disconnect",
    "Zero buttons on any touched slots, then close the serial port.",
    {},
    async () => {
      if (!state.conn) {
        return { content: [{ type: "text", text: '{"ok":true,"note":"not connected"}' }] };
      }
      for (let i = 0; i < state.slots.length; i++) {
        const s = state.slot(i);
        if (s.heldButtons === 0 && s.analog.every((v, idx) => v === NEUTRAL_ANALOG[idx])) continue;
        try {
          await state.conn.send(
            PktType.INPUT_EVENT,
            encodeInputEvent({
              playerIndex: i,
              deviceType: INPUT_TYPE_GAMEPAD,
              buttons: 0,
              analog: [...NEUTRAL_ANALOG],
              deltaX: 0,
              deltaY: 0,
            }),
          );
        } catch {}
      }
      try {
        await state.conn.close();
      } catch {}
      state.reset();
      return { content: [{ type: "text", text: '{"ok":true}' }] };
    },
  );

  server.tool(
    "info",
    "Report current connection state, held buttons, and recent activity.",
    {},
    async () => {
      const conn = state.conn;
      const slots = state.slots.map((s, i) => ({
        slot: i,
        held_buttons: s.heldButtons,
        analog: s.analog,
        last_input_event_ms_ago: s.lastInputEvent ? Date.now() - s.lastInputEvent.ts : null,
      }));
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify(
              {
                connected: !!conn,
                port: conn?.path,
                baud: conn?.baud,
                last_command_ms_ago: state.lastCommandAt ? Date.now() - state.lastCommandAt : null,
                adapter: state.adapterInfo,
                slots: slots.filter((s) => s.held_buttons || s.last_input_event_ms_ago !== null),
              },
              null,
              2,
            ),
          },
        ],
      };
    },
  );
}
