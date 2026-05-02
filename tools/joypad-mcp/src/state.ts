// Shared connection + per-slot state. One adapter per process.
//
// We track held buttons and analog axes locally so that `hold`/`release` /
// `axis` interact correctly: each AI_INJECT carries the *full* state, so the
// firmware doesn't accumulate, it overwrites.

import { Connection } from "./transport.js";
import { NEUTRAL_ANALOG, decodeInputEvent, decodeVersion } from "./protocol.js";

export interface SlotState {
  heldButtons: number;
  analog: [number, number, number, number, number, number];
  lastInputEvent?: { ts: number; buttons: number; analog: number[]; deltaX: number; deltaY: number };
}

const MAX_SLOTS = 8;

function freshSlot(): SlotState {
  return {
    heldButtons: 0,
    analog: [...NEUTRAL_ANALOG],
  };
}

class State {
  conn: Connection | null = null;
  adapterInfo: { app?: string; board?: string; version?: string } = {};
  lastCommandAt = 0;
  slots: SlotState[] = Array.from({ length: MAX_SLOTS }, () => freshSlot());

  reset(): void {
    this.conn = null;
    this.adapterInfo = {};
    this.slots = Array.from({ length: MAX_SLOTS }, () => freshSlot());
  }

  slot(i = 0): SlotState {
    if (i < 0 || i >= MAX_SLOTS) throw new Error(`slot ${i} out of range [0..${MAX_SLOTS - 1}]`);
    return this.slots[i];
  }

  requireConn(): Connection {
    if (!this.conn) throw new Error("not connected — call `connect` first");
    return this.conn;
  }

  attach(conn: Connection): void {
    this.conn = conn;
    conn.on("packet", (pkt) => this.handlePacket(pkt));
    conn.on("close", () => this.reset());
  }

  private handlePacket(pkt: { type: number; payload: Uint8Array }): void {
    switch (pkt.type) {
      case 0x10: {
        // INPUT_EVENT
        const ev = decodeInputEvent(pkt.payload);
        if (!ev) return;
        if (ev.playerIndex < MAX_SLOTS) {
          this.slots[ev.playerIndex].lastInputEvent = {
            ts: Date.now(),
            buttons: ev.buttons,
            analog: ev.analog,
            deltaX: ev.deltaX,
            deltaY: ev.deltaY,
          };
        }
        break;
      }
      case 0x03: {
        // VERSION
        const v = decodeVersion(pkt.payload);
        if (v) this.adapterInfo.version = `${v.major}.${v.minor}.${v.patch}`;
        break;
      }
    }
  }
}

export const state = new State();
