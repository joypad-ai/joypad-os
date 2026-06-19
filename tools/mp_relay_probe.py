#!/usr/bin/env python3
# Diagnose the MouthPad NUS relay on a JoypadOS CDC port WITHOUT the utility.
# 1) INFO  -> confirms which firmware build is running (app/version/board/build)
# 2) DEBUG.STREAM (~3s) -> shows MouthPad / NUS connection state
# 3) device_info_read relay frame -> confirms the dongle-level relay responds
#
# Usage: mp_relay_probe.py [/dev/cu.usbmodemXXXX]
import glob, json, struct, sys, time
import serial

def crc16(data, init=0xFFFF, poly=0x1021):
    crc = init
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ poly if (crc & 0x8000) else crc << 1) & 0xFFFF
    return crc

# --- JoypadOS command framing: AA, len(LE u16), type, seq, payload, crc(typ+seq+payload) ---
def jos_frame(typ, seq, payload):
    c = crc16(bytes([typ, seq]) + payload)
    return bytes([0xAA]) + struct.pack("<H", len(payload)) + bytes([typ, seq]) + payload + struct.pack("<H", c)

def jos_cmd(obj):
    return jos_frame(0x01, 0x00, json.dumps(obj).encode())

# --- MouthPad relay (PacketFramer): AA 55, len(BE u16), proto, crc(proto) BE ---
def relay_frame(proto):
    return bytes([0xAA, 0x55]) + struct.pack(">H", len(proto)) + proto + struct.pack(">H", crc16(proto))

# AppToRelayMessage{ destination=RELAY(1), device_info_read{} }  -> field1 varint=1, field4 len0
DEVICE_INFO_READ = relay_frame(bytes([0x08, 0x01, 0x22, 0x00]))

def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports: sys.exit("no /dev/cu.usbmodem* port found (is the utility still holding it?)")
    return ports[0]

port = sys.argv[1] if len(sys.argv) > 1 else find_port()
s = serial.Serial(port, 115200, timeout=0.1)
s.dtr = True
print(f"[probe] {port}")

buf = bytearray()
def pump(seconds):
    end = time.time() + seconds
    while time.time() < end:
        buf.extend(s.read(256))
        # demux: 0xAA 0x55 = relay frame; 0xAA + other = JoypadOS frame
        while True:
            i = buf.find(0xAA)
            if i < 0 or len(buf) < i + 4:
                if i > 0: del buf[:i]
                break
            if buf[i+1] == 0x55:                       # relay frame
                n = (buf[i+2] << 8) | buf[i+3]
                total = i + 4 + n + 2
                if len(buf) < total: break
                proto = bytes(buf[i+4:i+4+n]); del buf[:total]
                yield ("relay", proto)
            else:                                       # JoypadOS frame
                n = buf[i+1] | (buf[i+2] << 8)
                total = i + 5 + n + 2
                if len(buf) < total: break
                payload = bytes(buf[i+5:i+5+n]); del buf[:total]
                yield ("jos", payload)

def drain(label, seconds):
    for kind, data in pump(seconds):
        if kind == "relay":
            print(f"[RELAY RESP] {len(data)}B: {data.hex()}")
        else:
            try:
                obj = json.loads(data.decode("utf-8", "replace"))
                if obj.get("type") == "log": sys.stdout.write("[LOG] " + obj.get("msg",""))
                else: print(f"[JOS] {obj}")
            except Exception:
                pass

print("\n=== 0) MP.STATS (relay loss counters — drops==0 proves zero loss) ===")
s.write(jos_cmd({"cmd": "MP.STATS"})); s.flush()
drain("stats", 1.5)

print("\n=== 1) INFO (which firmware?) ===")
s.write(jos_cmd({"cmd": "INFO"})); s.flush()
drain("info", 1.5)

print("\n=== 2) DEBUG.STREAM ~3s (MouthPad / NUS state) ===")
s.write(jos_cmd({"cmd": "DEBUG.STREAM", "enable": True})); s.flush()
drain("dbg", 3.0)

print("\n=== 3) device_info_read relay frame -> expect device_info_response ===")
s.write(DEVICE_INFO_READ); s.flush()
got = False
for kind, data in pump(2.5):
    if kind == "relay":
        print(f"[RELAY RESP] {len(data)}B: {data.hex()}")
        got = True
        break
if not got:
    print("NO relay response (timeout) — relay not answering device_info_read")
print("\n[probe] done")
