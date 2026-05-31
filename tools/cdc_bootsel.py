#!/usr/bin/env python3
# Reboot a running JoypadOS device (with CDC binary protocol) into the BOOTSEL
# bootloader by sending the binary {"cmd":"BOOTSEL"} packet over its CDC port.
# Usage: cdc_bootsel.py [/dev/cu.usbmodemXXXX] [BOOTSEL|REBOOT]
import glob
import struct
import sys
import time

import serial


def crc16(data, init=0xFFFF, poly=0x1021):
    crc = init
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ poly if (crc & 0x8000) else crc << 1) & 0xFFFF
    return crc


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("no /dev/cu.usbmodem* port found")
    return ports[0]


port = sys.argv[1] if len(sys.argv) > 1 else find_port()
cmd = sys.argv[2] if len(sys.argv) > 2 else "BOOTSEL"

payload = ('{"cmd":"%s"}' % cmd).encode()
typ, seq = 0x01, 0x00
c = crc16(bytes([typ, seq]) + payload)
pkt = bytes([0xAA]) + struct.pack("<H", len(payload)) + bytes([typ, seq]) + payload + struct.pack("<H", c)

print("port=%s cmd=%s bytes=%d" % (port, cmd, len(pkt)))
s = serial.Serial(port, 115200, timeout=0.5)
s.write(pkt)
s.flush()
time.sleep(1.5)  # let firmware parse + run deferred reboot (disconnects USB)
try:
    s.close()
except Exception:
    pass
print("sent")
