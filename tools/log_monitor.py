#!/usr/bin/env python3
# Enable DEBUG.STREAM on a running JoypadOS device and print the debug log text
# live (decodes {"type":"log","msg":...} event frames).
# Usage: log_monitor.py [/dev/cu.usbmodemXXXX] [seconds]
import glob, json, struct, sys, time
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

def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("no /dev/cu.usbmodem* port found")
    return ports[0]

port = sys.argv[1] if len(sys.argv) > 1 else find_port()
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 30.0

s = serial.Serial(port, 115200, timeout=0.1)
s.dtr = True
s.write(frame(0x01, 0x00, b'{"cmd":"DEBUG.STREAM","enable":true}'))
s.flush()
sys.stderr.write(f"[log_monitor] {port} streaming {secs:.0f}s\n")

buf = bytearray()
end = time.time() + secs
linebuf = ""
while time.time() < end:
    buf += s.read(512)
    # parse frames: SYNC, len(u16 LE), typ, seq, payload(len), crc(u16)
    while True:
        i = buf.find(bytes([SYNC]))
        if i < 0:
            buf.clear(); break
        if len(buf) < i + 5:
            break
        ln = buf[i+1] | (buf[i+2] << 8)
        total = i + 1 + 2 + 2 + ln + 2
        if len(buf) < total:
            break
        payload = bytes(buf[i+5:i+5+ln])
        del buf[:total]
        try:
            obj = json.loads(payload.decode("utf-8", "replace"))
        except Exception:
            continue
        if obj.get("type") == "log":
            sys.stdout.write(obj.get("msg", ""))
            sys.stdout.flush()
