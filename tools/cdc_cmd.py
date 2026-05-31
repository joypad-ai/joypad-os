#!/usr/bin/env python3
# Send one JoypadOS CDC binary command and print all response/event frames
# for a short window. Usage: cdc_cmd.py <PORT> '<json>' [seconds]
import struct
import sys
import time

import serial

SYNC = 0xAA


def crc16(data, init=0xFFFF, poly=0x1021):
    crc = init
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ poly if (crc & 0x8000) else crc << 1) & 0xFFFF
    return crc


def frame(typ, seq, payload):
    c = crc16(bytes([typ, seq]) + payload)
    return bytes([SYNC]) + struct.pack("<H", len(payload)) + bytes([typ, seq]) + payload + struct.pack("<H", c)


port = sys.argv[1]
js = sys.argv[2] if len(sys.argv) > 2 else '{"cmd":"INFO"}'
secs = float(sys.argv[3]) if len(sys.argv) > 3 else 2.0

s = serial.Serial(port, 115200, timeout=0.1)
s.write(frame(0x01, 0x00, js.encode()))
s.flush()

state, length, typ, seq, payload = "SYNC", 0, 0, 0, bytearray()
end = time.time() + secs
TYPES = {0x02: "RSP", 0x03: "EVT", 0x04: "ACK", 0x05: "NAK"}
n = 0
while time.time() < end:
    for b in s.read(256):
        if state == "SYNC":
            if b == SYNC:
                state, payload = "LEN_LO", bytearray()
        elif state == "LEN_LO":
            length, state = b, "LEN_HI"
        elif state == "LEN_HI":
            length |= b << 8
            state = "TYPE" if length <= 1024 else "SYNC"
        elif state == "TYPE":
            typ, state = b, "SEQ"
        elif state == "SEQ":
            seq, state = b, ("PAYLOAD" if length else "CRC_LO")
        elif state == "PAYLOAD":
            payload.append(b)
            if len(payload) >= length:
                state = "CRC_LO"
        elif state == "CRC_LO":
            state = "CRC_HI"
        elif state == "CRC_HI":
            n += 1
            print("%s: %s" % (TYPES.get(typ, "0x%02X" % typ),
                              payload.decode("utf-8", "replace")))
            state = "SYNC"
s.close()
print("[cdc_cmd] %d frames" % n)
