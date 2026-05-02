// Input tools — every action becomes one or more UART_PKT_INPUT_EVENT packets.
// We keep per-slot held-button + analog state on the host side so each packet
// carries the *full* desired state (the firmware overwrites, doesn't merge).

import { z } from "zod";
import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { state } from "../state.js";
import {
  PktType,
  INPUT_TYPE_GAMEPAD,
  encodeInputEvent,
  NEUTRAL_ANALOG,
} from "../protocol.js";
import { parseButtons, parseAxis, AXIS_INDEX, maskToNames } from "../buttons.js";

function sleep(ms: number): Promise<void> {
  return new Promise((r) => setTimeout(r, ms));
}

// Send the slot's full desired state as a UART_PKT_INPUT_EVENT. uart_host's
// NORMAL mode submits this directly to the router as if a real controller
// had reported it (dev_addr 0xD0 + player_index). The packet carries the
// complete state — firmware overwrites, doesn't merge.
async function applySlot(slotIndex: number): Promise<void> {
  const conn = state.requireConn();
  const s = state.slot(slotIndex);
  await conn.send(
    PktType.INPUT_EVENT,
    encodeInputEvent({
      playerIndex: slotIndex,
      deviceType: INPUT_TYPE_GAMEPAD,
      buttons: s.heldButtons,
      analog: s.analog,
      deltaX: 0,
      deltaY: 0,
    }),
  );
  state.lastCommandAt = Date.now();
}

const slotArg = z.number().int().min(0).max(7).optional().describe("Player slot (default 0)");

export function registerInputTools(server: McpServer): void {
  server.tool(
    "tap",
    "Press one or more buttons briefly (default 80ms) then release. Buttons can be a single name like 'B1' or combined with '+' like 'B1+DR'. Blocks until the press completes.",
    {
      buttons: z.string().describe("Button name(s), e.g. 'B1' or 'B1+DR' or 'START'"),
      duration_ms: z.number().int().positive().optional().describe("Press duration (default 80ms)"),
      slot: slotArg,
    },
    async ({ buttons, duration_ms, slot }) => {
      const i = slot ?? 0;
      const mask = parseButtons(buttons);
      const dur = duration_ms ?? 80;
      const s = state.slot(i);
      const before = s.heldButtons;
      s.heldButtons = before | mask;
      await applySlot(i);
      await sleep(dur);
      s.heldButtons = before;
      await applySlot(i);
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify({ ok: true, action: "tap", buttons: maskToNames(mask), duration_ms: dur, slot: i }),
          },
        ],
      };
    },
  );

  server.tool(
    "press",
    "Press buttons N times. Each press = duration_ms held + gap_ms released. Useful for stepping through menus ('press DR 3 times'). Blocks until done.",
    {
      buttons: z.string(),
      count: z.number().int().positive().optional().describe("Number of presses (default 1)"),
      duration_ms: z.number().int().positive().optional().describe("Hold time per press (default 80ms)"),
      gap_ms: z.number().int().nonnegative().optional().describe("Release gap between presses (default 50ms)"),
      slot: slotArg,
    },
    async ({ buttons, count, duration_ms, gap_ms, slot }) => {
      const i = slot ?? 0;
      const mask = parseButtons(buttons);
      const n = count ?? 1;
      const dur = duration_ms ?? 80;
      const gap = gap_ms ?? 50;
      const s = state.slot(i);
      const before = s.heldButtons;
      for (let k = 0; k < n; k++) {
        s.heldButtons = before | mask;
        await applySlot(i);
        await sleep(dur);
        s.heldButtons = before;
        await applySlot(i);
        if (k < n - 1) await sleep(gap);
      }
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify({ ok: true, action: "press", buttons: maskToNames(mask), count: n, slot: i }),
          },
        ],
      };
    },
  );

  server.tool(
    "hold",
    "Press buttons and don't release. Returns immediately. Use `release` to let go, or `release_all` to drop everything.",
    { buttons: z.string(), slot: slotArg },
    async ({ buttons, slot }) => {
      const i = slot ?? 0;
      const mask = parseButtons(buttons);
      const s = state.slot(i);
      s.heldButtons |= mask;
      await applySlot(i);
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify({ ok: true, action: "hold", buttons: maskToNames(mask), held: maskToNames(s.heldButtons), slot: i }),
          },
        ],
      };
    },
  );

  server.tool(
    "release",
    "Release named buttons (or all if `buttons` omitted). Analog axes are not affected.",
    { buttons: z.string().optional(), slot: slotArg },
    async ({ buttons, slot }) => {
      const i = slot ?? 0;
      const s = state.slot(i);
      if (buttons) {
        const mask = parseButtons(buttons);
        s.heldButtons &= ~mask;
      } else {
        s.heldButtons = 0;
      }
      await applySlot(i);
      return {
        content: [
          {
            type: "text",
            text: JSON.stringify({ ok: true, held: maskToNames(s.heldButtons), slot: i }),
          },
        ],
      };
    },
  );

  server.tool(
    "axis",
    "Set one analog axis. Persists until changed. Values: LX/LY/RX/RY in 0-255 (128=center), LT/RT in 0-255 (0=released).",
    {
      axis: z.string().describe("LX, LY, RX, RY, LT, or RT"),
      value: z.number().int().min(0).max(255),
      slot: slotArg,
    },
    async ({ axis, value, slot }) => {
      const i = slot ?? 0;
      const a = parseAxis(axis);
      const s = state.slot(i);
      s.analog[AXIS_INDEX[a]] = value;
      await applySlot(i);
      return {
        content: [
          { type: "text", text: JSON.stringify({ ok: true, axis: a, value, analog: s.analog, slot: i }) },
        ],
      };
    },
  );

  server.tool(
    "axes",
    "Set multiple analog axes at once. Omit a field to leave it unchanged.",
    {
      lx: z.number().int().min(0).max(255).optional(),
      ly: z.number().int().min(0).max(255).optional(),
      rx: z.number().int().min(0).max(255).optional(),
      ry: z.number().int().min(0).max(255).optional(),
      lt: z.number().int().min(0).max(255).optional(),
      rt: z.number().int().min(0).max(255).optional(),
      slot: slotArg,
    },
    async ({ lx, ly, rx, ry, lt, rt, slot }) => {
      const i = slot ?? 0;
      const s = state.slot(i);
      if (lx !== undefined) s.analog[0] = lx;
      if (ly !== undefined) s.analog[1] = ly;
      if (rx !== undefined) s.analog[2] = rx;
      if (ry !== undefined) s.analog[3] = ry;
      if (lt !== undefined) s.analog[4] = lt;
      if (rt !== undefined) s.analog[5] = rt;
      await applySlot(i);
      return {
        content: [{ type: "text", text: JSON.stringify({ ok: true, analog: s.analog, slot: i }) }],
      };
    },
  );

  server.tool(
    "combo",
    "Run a sequence of steps without round-tripping to the assistant between each. Each step has buttons (pressed for duration_ms then released) and/or axes (persist after the combo). Use this when timing matters — eg. 'down then A' for a fighter input.",
    {
      steps: z
        .array(
          z.object({
            buttons: z.string().optional(),
            duration_ms: z.number().int().positive().optional(),
            gap_ms: z.number().int().nonnegative().optional(),
            axes: z
              .object({
                lx: z.number().int().min(0).max(255).optional(),
                ly: z.number().int().min(0).max(255).optional(),
                rx: z.number().int().min(0).max(255).optional(),
                ry: z.number().int().min(0).max(255).optional(),
                lt: z.number().int().min(0).max(255).optional(),
                rt: z.number().int().min(0).max(255).optional(),
              })
              .optional(),
          }),
        )
        .min(1),
      slot: slotArg,
    },
    async ({ steps, slot }) => {
      const i = slot ?? 0;
      const s = state.slot(i);
      const baseHeld = s.heldButtons;
      for (let k = 0; k < steps.length; k++) {
        const step = steps[k];
        if (step.axes) {
          if (step.axes.lx !== undefined) s.analog[0] = step.axes.lx;
          if (step.axes.ly !== undefined) s.analog[1] = step.axes.ly;
          if (step.axes.rx !== undefined) s.analog[2] = step.axes.rx;
          if (step.axes.ry !== undefined) s.analog[3] = step.axes.ry;
          if (step.axes.lt !== undefined) s.analog[4] = step.axes.lt;
          if (step.axes.rt !== undefined) s.analog[5] = step.axes.rt;
        }
        if (step.buttons) {
          const mask = parseButtons(step.buttons);
          const dur = step.duration_ms ?? 80;
          const gap = step.gap_ms ?? 50;
          s.heldButtons = baseHeld | mask;
          await applySlot(i);
          await sleep(dur);
          s.heldButtons = baseHeld;
          await applySlot(i);
          if (k < steps.length - 1) await sleep(gap);
        } else {
          await applySlot(i);
          if (step.duration_ms) await sleep(step.duration_ms);
        }
      }
      return {
        content: [{ type: "text", text: JSON.stringify({ ok: true, action: "combo", steps: steps.length, slot: i }) }],
      };
    },
  );

  server.tool(
    "release_all",
    "Safety reset: zero all buttons, recenter all analog axes (128/0). Good to call if a hold gets stuck.",
    { slot: slotArg },
    async ({ slot }) => {
      const i = slot ?? 0;
      const s = state.slot(i);
      s.heldButtons = 0;
      s.analog = [...NEUTRAL_ANALOG];
      await applySlot(i);
      return { content: [{ type: "text", text: JSON.stringify({ ok: true, slot: i }) }] };
    },
  );
}
