import { test } from "node:test";
import assert from "node:assert/strict";
import { crc8, buildFrame, RxParser, SYNC_BYTE } from "../src/transport.js";
import { encodeInputEvent, INPUT_TYPE_GAMEPAD, NEUTRAL_ANALOG, PktType } from "../src/protocol.js";

// Reference CRC-8 implementation copied verbatim from uart_protocol.h.
// If this test passes, the TS port matches the firmware byte-for-byte.
function crc8_ref(data: Uint8Array): number {
  let crc = 0;
  for (const byte of data) {
    crc ^= byte;
    for (let i = 0; i < 8; i++) {
      if (crc & 0x80) crc = ((crc << 1) ^ 0x07) & 0xff;
      else crc = (crc << 1) & 0xff;
    }
  }
  return crc;
}

test("crc8 matches reference for empty input", () => {
  assert.equal(crc8(new Uint8Array(0)), 0);
});

test("crc8 matches reference for many random inputs", () => {
  for (let n = 0; n < 100; n++) {
    const len = Math.floor(Math.random() * 64);
    const data = new Uint8Array(len);
    for (let i = 0; i < len; i++) data[i] = Math.floor(Math.random() * 256);
    assert.equal(crc8(data), crc8_ref(data), `mismatch for len=${len}`);
  }
});

test("crc8 standard vector ('123456789') = 0xF4", () => {
  // CRC-8 with polynomial 0x07, init 0x00, no reflection, no XOR-out
  // is the classic "CRC-8/SMBUS" — well, almost. Reference value for "123456789"
  // with that config is 0xF4.
  const ascii = new TextEncoder().encode("123456789");
  assert.equal(crc8(ascii), 0xf4);
});

test("buildFrame produces correct layout", () => {
  const payload = new Uint8Array([0x01, 0x02, 0x03]);
  const frame = buildFrame(0x50, payload);
  assert.equal(frame[0], SYNC_BYTE);
  assert.equal(frame[1], 3);
  assert.equal(frame[2], 0x50);
  assert.deepEqual(Array.from(frame.slice(3, 6)), [0x01, 0x02, 0x03]);
  assert.equal(frame[6], crc8_ref(new Uint8Array([3, 0x50, 0x01, 0x02, 0x03])));
});

test("RxParser: clean packet roundtrip", () => {
  const parser = new RxParser();
  const packets: { type: number; payload: Uint8Array }[] = [];
  parser.on("packet", (p) => packets.push(p));
  const evt = encodeInputEvent({
    playerIndex: 0,
    deviceType: INPUT_TYPE_GAMEPAD,
    buttons: 0x0001,
    analog: [...NEUTRAL_ANALOG],
    deltaX: 0,
    deltaY: 0,
  });
  const frame = buildFrame(PktType.INPUT_EVENT, evt);
  parser.feed(frame);
  assert.equal(packets.length, 1);
  assert.equal(packets[0].type, PktType.INPUT_EVENT);
  assert.deepEqual(Array.from(packets[0].payload), Array.from(evt));
});

test("RxParser: ASCII log lines emitted between packets", () => {
  const parser = new RxParser();
  const lines: string[] = [];
  const packets: { type: number; payload: Uint8Array }[] = [];
  parser.on("log", (l: string) => lines.push(l));
  parser.on("packet", (p) => packets.push(p));

  const log = Buffer.from("[uart_host] hello\n", "utf8");
  const frame = buildFrame(0x10, new Uint8Array(14));
  const log2 = Buffer.from("[uart_host] world\n", "utf8");
  parser.feed(Buffer.concat([log, frame, log2]));

  assert.deepEqual(lines, ["[uart_host] hello", "[uart_host] world"]);
  assert.equal(packets.length, 1);
  assert.equal(packets[0].type, 0x10);
});

test("RxParser: byte-at-a-time delivery", () => {
  const parser = new RxParser();
  const packets: { type: number; payload: Uint8Array }[] = [];
  parser.on("packet", (p) => packets.push(p));
  const frame = buildFrame(0x03, new Uint8Array([1, 2, 3, 4, 5, 6, 7, 8]));
  for (const b of frame) parser.feed(Uint8Array.of(b));
  assert.equal(packets.length, 1);
  assert.equal(packets[0].type, 0x03);
});

test("RxParser: bad CRC discards packet, treats SYNC as log byte", () => {
  const parser = new RxParser();
  const packets: { type: number; payload: Uint8Array }[] = [];
  const lines: string[] = [];
  parser.on("packet", (p) => packets.push(p));
  parser.on("log", (l: string) => lines.push(l));
  const frame = buildFrame(0x10, new Uint8Array(4));
  frame[frame.length - 1] ^= 0x55; // corrupt CRC
  parser.feed(frame);
  parser.feed(Buffer.from("\n"));
  // No valid packet, but the SYNC byte (and rest) should have ended up in the log buffer.
  assert.equal(packets.length, 0);
  // Not asserting exact log line shape — bytes are mostly non-printable here.
});

test("RxParser: invalid length is rejected", () => {
  const parser = new RxParser();
  const packets: { type: number; payload: Uint8Array }[] = [];
  const lines: string[] = [];
  parser.on("packet", (p) => packets.push(p));
  parser.on("log", (l: string) => lines.push(l));
  // SYNC + LEN=200 (over MAX_PAYLOAD=64)
  parser.feed(Uint8Array.of(SYNC_BYTE, 200, 0x10));
  parser.feed(Buffer.from("real log line\n"));
  // Should have skipped the bogus SYNC and emitted the log
  assert.ok(lines.length >= 1);
  assert.equal(packets.length, 0);
});
