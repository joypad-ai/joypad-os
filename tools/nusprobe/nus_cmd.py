#!/usr/bin/env python3
# Send one JoypadOS CDC binary command over BLE NUS to an OS-paired device.
# BLE sibling of cdc_cmd.py: same frame format, but delivered through the
# ad-hoc signed NUSProbe.app shell (macOS hides OS-paired BLE HID devices
# from unsigned scripts — the app bundle is the identity that passes).
# Usage: nus_cmd.py '<json>'
#        nus_cmd.py '{"cmd":"FACE.STYLE","style":"astro"}'
import os
import struct
import subprocess
import sys
import tempfile

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


here = os.path.dirname(os.path.abspath(__file__))
app = os.path.join(here, "NUSProbe.app")
if not os.path.isdir(app):
    subprocess.run(["sh", os.path.join(here, "build.sh")], check=True)

js = sys.argv[1] if len(sys.argv) > 1 else '{"cmd":"PING"}'
hexbytes = frame(0x01, 0x00, js.encode()).hex()

log = tempfile.NamedTemporaryFile(suffix=".log", delete=False)
log.close()
r = subprocess.run(["open", "-W", "--stdout", log.name, "--stderr", log.name,
                    app, "--args", hexbytes])
with open(log.name) as f:
    sys.stdout.write(f.read())
os.unlink(log.name)
sys.exit(r.returncode)
