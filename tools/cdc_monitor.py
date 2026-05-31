#!/usr/bin/env python3
# Enable INPUT.STREAM on a running JoypadOS device and decode the binary CDC
# event frames (controller input/output), printing them live.
# Usage: cdc_monitor.py [/dev/cu.usbmodemXXXX] [seconds]
import glob
import struct
import sys
import time

import serial

SYNC = 0xAA
MSG_CMD = 0x01
MSG_EVT = 0x03


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


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("no /dev/cu.usbmodem* port found")
    return ports[0]


port = sys.argv[1] if len(sys.argv) > 1 else find_port()
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 20.0

s = serial.Serial(port, 115200, timeout=0.1)
s.write(frame(MSG_CMD, 0x00, b'{"cmd":"INPUT.STREAM","enable":true}'))
s.flush()
print("[monitor] port=%s streaming for %.0fs — press buttons on the hub controller" % (port, secs))

# Parser state machine
buf = bytearray()
state = "SYNC"
length = 0
typ = 0
seq = 0
payload = bytearray()
n_evt = 0
end = time.time() + secs

while time.time() < end:
    data = s.read(256)
    for b in data:
        if state == "SYNC":
            if b == SYNC:
                state, payload = "LEN_LO", bytearray()
        elif state == "LEN_LO":
            length = b
            state = "LEN_HI"
        elif state == "LEN_HI":
            length |= b << 8
            state = "TYPE" if length <= 1024 else "SYNC"
        elif state == "TYPE":
            typ = b
            state = "SEQ"
        elif state == "SEQ":
            seq = b
            state = "PAYLOAD" if length else "CRC_LO"
        elif state == "PAYLOAD":
            payload.append(b)
            if len(payload) >= length:
                state = "CRC_LO"
        elif state == "CRC_LO":
            state = "CRC_HI"
        elif state == "CRC_HI":
            if typ == MSG_EVT:
                n_evt += 1
                try:
                    print("EVT:", payload.decode("utf-8", "replace"))
                except Exception:
                    print("EVT(raw):", bytes(payload))
            state = "SYNC"

s.write(frame(MSG_CMD, 0x01, b'{"cmd":"INPUT.STREAM","enable":false}'))
s.flush()
s.close()
print("[monitor] done — %d event frames received" % n_evt)
