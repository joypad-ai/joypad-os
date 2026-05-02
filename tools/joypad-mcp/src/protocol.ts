// Packet types and struct layouts mirroring src/core/uart/uart_protocol.h.
// All multi-byte fields are little-endian. Structs are __attribute__((packed)).

export enum PktType {
  NOP = 0x00,
  PING = 0x01,
  PONG = 0x02,
  VERSION = 0x03,
  RESET = 0x04,
  ACK = 0x05,
  NAK = 0x06,

  INPUT_EVENT = 0x10,
  INPUT_CONNECT = 0x11,
  INPUT_DISCONNECT = 0x12,

  RUMBLE = 0x20,
  LED = 0x21,
  FEEDBACK_ACK = 0x22,

  GET_STATUS = 0x30,
  STATUS = 0x31,
  GET_PLAYERS = 0x32,
  PLAYERS = 0x33,

  SET_PROFILE = 0x40,
  GET_PROFILE = 0x41,
  PROFILE = 0x42,
  SET_MODE = 0x43,
}

// uart_input_event_t — 14 bytes packed
//   uint8_t  player_index;
//   uint8_t  device_type;
//   uint32_t buttons;
//   uint8_t  analog[6];     // LX, LY, RX, RY, LT, RT
//   int8_t   delta_x, delta_y;
export interface InputEvent {
  playerIndex: number;
  deviceType: number;
  buttons: number;
  analog: [number, number, number, number, number, number];
  deltaX: number;
  deltaY: number;
}

// INPUT_TYPE_GAMEPAD from core/input_event.h. Used as `device_type` so
// uart_host's INPUT_EVENT branch routes synthesized events as gamepads.
export const INPUT_TYPE_GAMEPAD = 0;

export function encodeInputEvent(p: InputEvent): Uint8Array {
  const buf = Buffer.alloc(14);
  buf.writeUInt8(p.playerIndex & 0xff, 0);
  buf.writeUInt8(p.deviceType & 0xff, 1);
  buf.writeUInt32LE(p.buttons >>> 0, 2);
  for (let i = 0; i < 6; i++) buf.writeUInt8(p.analog[i] & 0xff, 6 + i);
  buf.writeInt8(p.deltaX | 0, 12);
  buf.writeInt8(p.deltaY | 0, 13);
  return new Uint8Array(buf.buffer, buf.byteOffset, buf.byteLength);
}

export function decodeInputEvent(payload: Uint8Array): InputEvent | null {
  if (payload.length < 14) return null;
  const buf = Buffer.from(payload.buffer, payload.byteOffset, payload.byteLength);
  return {
    playerIndex: buf.readUInt8(0),
    deviceType: buf.readUInt8(1),
    buttons: buf.readUInt32LE(2),
    analog: [
      buf.readUInt8(6),
      buf.readUInt8(7),
      buf.readUInt8(8),
      buf.readUInt8(9),
      buf.readUInt8(10),
      buf.readUInt8(11),
    ],
    deltaX: buf.readInt8(12),
    deltaY: buf.readInt8(13),
  };
}

// uart_version_t — 8 bytes
export interface Version {
  major: number;
  minor: number;
  patch: number;
  boardType: number;
  features: number;
}

export function decodeVersion(payload: Uint8Array): Version | null {
  if (payload.length < 8) return null;
  const buf = Buffer.from(payload.buffer, payload.byteOffset, payload.byteLength);
  return {
    major: buf.readUInt8(0),
    minor: buf.readUInt8(1),
    patch: buf.readUInt8(2),
    boardType: buf.readUInt8(3),
    features: buf.readUInt32LE(4),
  };
}

export const BOARD_NAMES: Record<number, string> = {
  0x01: "rp2040",
  0x02: "esp32s3",
};

// Default centered analog (HID convention: 128=center)
export const NEUTRAL_ANALOG: [number, number, number, number, number, number] = [
  128, 128, 128, 128, 0, 0,
];
