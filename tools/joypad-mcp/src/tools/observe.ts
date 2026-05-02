// Observation tools — read-only views of adapter state.
//
// `read_state` returns what the MCP last *sent* (held buttons + analog),
// since the firmware doesn't currently echo applied state back.
// `read_human_input` returns the most recent INPUT_EVENT we've seen — only
// meaningful if the adapter is configured to emit events back over UART
// (most apps don't yet; this is forward-looking).
// `read_log` returns recent printf lines captured from the same UART stream.

import { z } from "zod";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { state } from "../state.js";
import { maskToNames } from "../buttons.js";

const slotArg = z.number().int().min(0).max(7).optional();

export function registerObserveTools(server: McpServer): void {
  server.tool(
    "read_state",
    "Return the current input state we're driving for a slot: held buttons, analog axes. This is what the MCP last sent — it's the desired state, not necessarily what the console actually applied.",
    { slot: slotArg },
    async ({ slot }) => {
      const i = slot ?? 0;
      const s = state.slot(i);
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify(
              {
                slot: i,
                held_buttons: maskToNames(s.heldButtons),
                buttons_mask: s.heldButtons,
                analog: { lx: s.analog[0], ly: s.analog[1], rx: s.analog[2], ry: s.analog[3], lt: s.analog[4], rt: s.analog[5] },
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
    "read_human_input",
    "Return the most recent INPUT_EVENT from a real controller on this slot, if any. Returns null if the adapter has not emitted an event for this slot. (Forward-looking — most apps don't echo INPUT_EVENT back yet.)",
    { slot: slotArg },
    async ({ slot }) => {
      const i = slot ?? 0;
      const ev = state.slot(i).lastInputEvent;
      if (!ev) {
        return { content: [{ type: "text", text: '{"event":null,"note":"no events seen for this slot"}' }] };
      }
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify(
              {
                slot: i,
                ms_ago: Date.now() - ev.ts,
                buttons: maskToNames(ev.buttons),
                buttons_mask: ev.buttons,
                analog: ev.analog,
                delta_x: ev.deltaX,
                delta_y: ev.deltaY,
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
    "read_log",
    "Recent log lines from the adapter (printf output). Useful for debugging — e.g., to see if the firmware parsed an inject packet or rejected it.",
    {
      since_ms: z.number().int().positive().optional().describe("Look back this many ms (default 5000)"),
      grep: z.string().optional().describe("Optional regex to filter lines"),
    },
    async ({ since_ms, grep }) => {
      const conn = state.conn;
      if (!conn) return { content: [{ type: "text", text: '{"lines":[],"note":"not connected"}' }] };
      const lines = conn.recentLogs(since_ms ?? 5000, grep);
      return { content: [{ type: "text", text: JSON.stringify({ lines, count: lines.length }, null, 2) }] };
    },
  );
}
