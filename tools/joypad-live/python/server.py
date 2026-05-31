#!/usr/bin/env python3
"""
joypad-live — local REST bridge for live controller-remap crowd control.

Mirrors the C# reference in .dev/docs/streamer-live-remap.md. Talks the binary
CDC protocol to a Joypad OS adapter (usb2usb etc.) over its USB-CDC serial port,
and exposes a tiny HTTP API that Streamer.bot (or curl) can call to switch
button-remap profiles live.

    pip install pyserial
    python3 server.py /dev/cu.usbmodemXXXX            # macOS/Linux
    python3 server.py COM5 --http-port 8777           # Windows

    # define the two demo profiles on the device, then exit:
    python3 server.py /dev/cu.usbmodemXXXX --define-demo

Endpoints:
    GET  /health          -> {"ok":true,"port":...}
    GET  /info            -> INFO
    GET  /profiles        -> PROFILE.LIST
    POST /profile/<n>     -> PROFILE.SET {index:n}   (an effect)
    POST /neutral         -> PROFILE.SET {index:0}   (back to default)
    POST /save            -> PROFILE.SAVE (JSON body forwarded as args)

NOTE: only one process may own the serial port. Close config.joypad.ai (browser
Web Serial) before running this.
"""

import argparse
import collections
import json
import queue
import struct
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

import serial  # pyserial


# ============================================================================
# Event broadcaster — drives the viewer overlay's live activity feed via SSE.
# Each command endpoint publishes an event with user/platform attribution
# (read from X-User / X-Platform request headers, set by the chat bots).
# ============================================================================

class EventBroadcaster:
    def __init__(self, max_history: int = 50):
        self._lock = threading.Lock()
        self._history = collections.deque(maxlen=max_history)
        self._subscribers: list[queue.Queue] = []

    def publish(self, kind: str, label: str, user: str | None, platform: str | None):
        ev = {
            "ts": int(time.time() * 1000),
            "kind": kind,
            "label": label,
            "user": user or "anonymous",
            "platform": platform or "?",
        }
        with self._lock:
            self._history.append(ev)
            for q in list(self._subscribers):
                try:
                    q.put_nowait(ev)
                except queue.Full:
                    pass

    def subscribe(self) -> tuple[queue.Queue, list]:
        q = queue.Queue(maxsize=100)
        with self._lock:
            self._subscribers.append(q)
            return q, list(self._history)

    def unsubscribe(self, q: queue.Queue):
        with self._lock:
            if q in self._subscribers:
                self._subscribers.remove(q)


events = EventBroadcaster()

# Friendly button name → JP_BUTTON_* bitmask, used by /press, /hold endpoints.
# Aliases per common labels (Xbox A, PS Cross, Switch B, etc.) — first match
# in this dict wins for any given name.
BUTTON_NAMES = {
    "a": 1, "cross": 1, "b1": 1,
    "b": 2, "circle": 2, "b2": 2,
    "x": 4, "square": 4, "b3": 4,
    "y": 8, "triangle": 8, "b4": 8,
    "l1": 16, "lb": 16,
    "r1": 32, "rb": 32,
    "l2": 64, "lt": 64,
    "r2": 128, "rt": 128,
    "select": 256, "back": 256, "minus": 256, "share": 256, "s1": 256,
    "start": 512, "plus": 512, "options": 512, "s2": 512,
    "l3": 1024, "ls": 1024,
    "r3": 2048, "rs": 2048,
    "up": 4096, "u": 4096, "du": 4096,
    "down": 8192, "d": 8192, "dd": 8192,
    "left": 16384, "l": 16384, "dl": 16384,
    "right": 32768, "r": 32768, "dr": 32768,
    "home": 65536, "guide": 65536, "ps": 65536, "a1": 65536,
}

TAP_MS = 80  # default duration of a single /press tap

CDC_SYNC = 0xAA
MSG_CMD = 0x01
MSG_RSP = 0x02


def crc16_ccitt(data: bytes) -> int:
    """CRC-16-CCITT (poly 0x1021, init 0xFFFF) — matches src/usb/usbd/cdc/cdc_protocol.c."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) if (crc & 0x8000) else (crc << 1)
            crc &= 0xFFFF
    return crc


def build_frame(payload: bytes, seq: int = 1) -> bytes:
    header = struct.pack("<BHBB", CDC_SYNC, len(payload), MSG_CMD, seq)
    crc = crc16_ccitt(struct.pack("BB", MSG_CMD, seq) + payload)
    return header + payload + struct.pack("<H", crc)


class CdcClient:
    """Synchronous request/response CDC client over a single serial port."""

    def __init__(self, port: str, baud: int = 115200, timeout: float = 0.8):
        # exclusive=True (posix) makes the single-owner rule explicit.
        try:
            self.ser = serial.Serial(port, baud, timeout=0.1, exclusive=True)
        except TypeError:
            self.ser = serial.Serial(port, baud, timeout=0.1)
        self.port = port
        self.timeout = timeout
        self.lock = threading.Lock()

    def command(self, cmd: str, **args) -> dict:
        """Send a command, return the decoded JSON response (or an error dict)."""
        payload = {"cmd": cmd}
        payload.update(args)
        frame = build_frame(json.dumps(payload, separators=(",", ":")).encode())

        with self.lock:
            self.ser.reset_input_buffer()
            self.ser.write(frame)

            import time
            deadline = time.monotonic() + self.timeout
            buf = bytearray()
            while time.monotonic() < deadline:
                chunk = self.ser.read(256)
                if chunk:
                    buf += chunk
                rsp = self._scan_rsp(buf)
                if rsp is not None:
                    return rsp
            return {"ok": False, "error": "timeout", "cmd": cmd}

    @staticmethod
    def _scan_rsp(buf: bytearray):
        i = 0
        while i + 5 <= len(buf):
            if buf[i] != CDC_SYNC:
                i += 1
                continue
            length = buf[i + 1] | (buf[i + 2] << 8)
            if i + 5 + length + 2 > len(buf):
                return None  # need more bytes
            if buf[i + 3] == MSG_RSP:
                try:
                    return json.loads(bytes(buf[i + 5 : i + 5 + length]).decode())
                except Exception:
                    return {"ok": False, "error": "bad json in response"}
            i += 5 + length + 2
        return None

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass


# Two demo profiles for the end-to-end test (button_map = 18 entries, see the doc).
DEMO_PROFILES = [
    # A<->B swap: pos0(B1)->2(B2), pos1(B2)->1(B1)
    {"name": "AswapB", "button_map": [2, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]},
    # D-pad scramble: U->L(15), D->R(16), L->D(14), R->U(13)
    {"name": "Chaos",  "button_map": [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 15, 16, 14, 13, 0, 0]},
]


def define_demo(cdc: CdcClient):
    print("Defining demo profiles...")
    for p in DEMO_PROFILES:
        r = cdc.command("PROFILE.SAVE", index=255, name=p["name"], button_map=p["button_map"])
        print(f"  PROFILE.SAVE {p['name']:8s} -> {r}")
    print("Current profiles:")
    print(" ", cdc.command("PROFILE.LIST"))


def make_handler(cdc: CdcClient):
    class Handler(BaseHTTPRequestHandler):
        def _send(self, obj, code=200):
            body = json.dumps(obj).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Access-Control-Allow-Origin", "*")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def log_message(self, fmt, *a):  # quieter logging
            print("[http]", self.command, self.path, "->", fmt % a if a else fmt)

        def _attrib(self):
            """Read viewer attribution from headers set by the chat bots."""
            return (
                self.headers.get("X-User") or "anonymous",
                self.headers.get("X-Platform") or "?",
            )

        def _serve_html(self, filename):
            """Serve a static HTML file from tools/joypad-live/."""
            from pathlib import Path
            html_path = Path(__file__).resolve().parent.parent / filename
            if not html_path.exists():
                return self._send({"ok": False, "error": f"{filename} not found"}, 404)
            body = html_path.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            path = self.path.strip("/")
            if path in ("", "dashboard"):
                # Streamer's control surface — manual override panel.
                self._serve_html("dashboard.html")
            elif path == "overlay":
                # Viewer-facing OBS browser source. Reads /events SSE for the
                # live activity feed; lists chat commands.
                self._serve_html("overlay.html")
            elif path == "events":
                # Server-Sent Events stream of command activity. The overlay
                # subscribes here; bots publish via the command endpoints.
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream; charset=utf-8")
                self.send_header("Cache-Control", "no-cache")
                self.send_header("Connection", "keep-alive")
                self.send_header("Access-Control-Allow-Origin", "*")
                self.send_header("X-Accel-Buffering", "no")  # disable proxy buffering
                self.end_headers()
                q, history = events.subscribe()
                try:
                    # Replay recent history so an overlay reconnect doesn't
                    # start with an empty feed.
                    for ev in history:
                        self.wfile.write(f"data: {json.dumps(ev)}\n\n".encode())
                    self.wfile.flush()
                    while True:
                        try:
                            ev = q.get(timeout=15)
                            self.wfile.write(f"data: {json.dumps(ev)}\n\n".encode())
                        except queue.Empty:
                            # SSE comment line — keeps the connection alive
                            # through aggressive proxies (and OBS itself).
                            self.wfile.write(b": heartbeat\n\n")
                        self.wfile.flush()
                except (BrokenPipeError, ConnectionResetError, OSError):
                    pass
                finally:
                    events.unsubscribe(q)
                return
            elif path == "health":
                self._send({"ok": True, "port": cdc.port})
            elif path == "info":
                self._send(cdc.command("INFO"))
            elif path == "profiles":
                self._send(cdc.command("PROFILE.LIST"))
            else:
                self._send({"ok": False, "error": "not found"}, 404)

        def do_POST(self):
            path = self.path.strip("/")
            # Hot-path selection uses PROFILE.SELECT — RAM only, no flash
            # write. The persistent boot default survives via /default/<n>.
            if path.startswith("profile/") and path[8:].isdigit():
                user, plat = self._attrib()
                r = cdc.command("PROFILE.SELECT", index=int(path[8:]))
                self._send(r)
                if r.get("ok"):
                    events.publish("effect", r.get("name", f"profile {path[8:]}"), user, plat)
            elif path == "neutral":
                user, plat = self._attrib()
                r = cdc.command("PROFILE.SELECT", index=0)
                self._send(r)
                if r.get("ok"):
                    events.publish("reset", "neutral", user, plat)
            elif path.startswith("default/") and path[8:].isdigit():
                # Persistent — writes to flash. For "make this the new boot
                # default," not for hot-path crowd-control switching.
                self._send(cdc.command("PROFILE.SET", index=int(path[8:])))
            elif path == "save":
                length = int(self.headers.get("Content-Length", 0))
                try:
                    args = json.loads(self.rfile.read(length) or b"{}")
                except Exception:
                    return self._send({"ok": False, "error": "bad json body"}, 400)
                self._send(cdc.command("PROFILE.SAVE", **args))
            elif path == "apply":
                # PROFILE.APPLY — ephemeral RAM-only remap; no flash write, no
                # 4-slot cap. Body forwarded as args (button_map, name, etc.).
                length = int(self.headers.get("Content-Length", 0))
                try:
                    args = json.loads(self.rfile.read(length) or b"{}")
                except Exception:
                    return self._send({"ok": False, "error": "bad json body"}, 400)
                user, plat = self._attrib()
                r = cdc.command("PROFILE.APPLY", **args)
                self._send(r)
                if r.get("ok"):
                    events.publish("effect", r.get("name") or args.get("name") or "apply", user, plat)
            elif path == "clear":
                # PROFILE.CLEAR — drop the ephemeral override.
                user, plat = self._attrib()
                r = cdc.command("PROFILE.CLEAR")
                self._send(r)
                if r.get("ok"):
                    events.publish("reset", "clear apply", user, plat)
            elif path == "overlay":
                # OVERLAY.SET — runtime "live tweak" layer composed on top of
                # whatever profile is active. Body fields (all optional):
                # flags, left_stick_sens, right_stick_sens, socd_mode,
                # l2_threshold, r2_threshold.
                length = int(self.headers.get("Content-Length", 0))
                try:
                    args = json.loads(self.rfile.read(length) or b"{}")
                except Exception:
                    return self._send({"ok": False, "error": "bad json body"}, 400)
                user, plat = self._attrib()
                r = cdc.command("OVERLAY.SET", **args)
                self._send(r)
                if r.get("ok"):
                    # Summarize the overlay in a viewer-friendly label.
                    flags = args.get("flags", 0) or 0
                    parts = []
                    if flags & 1:  parts.append("swap sticks")
                    if flags & 2:  parts.append("invert LY")
                    if flags & 4:  parts.append("invert RY")
                    if flags & 8:  parts.append("invert LX")
                    if flags & 16: parts.append("invert RX")
                    if args.get("socd_mode"): parts.append(f"SOCD {args['socd_mode']}")
                    label = " + ".join(parts) or "overlay"
                    events.publish("overlay", label, user, plat)
            elif path == "overlay/clear":
                user, plat = self._attrib()
                r = cdc.command("OVERLAY.CLEAR")
                self._send(r)
                if r.get("ok"):
                    events.publish("reset", "clear overlay", user, plat)
            elif path.startswith("press/"):
                name = path[6:]
                mask = BUTTON_NAMES.get(name.lower())
                if mask is None:
                    return self._send({"ok": False, "error": f"unknown button: {name}"}, 400)
                cdc.command("INPUT.INJECT", buttons=mask)
                time.sleep(TAP_MS / 1000.0)
                cdc.command("INPUT.INJECT", buttons=0)
                user, plat = self._attrib()
                events.publish("press", f"tap {name.upper()}", user, plat)
                self._send({"ok": True, "tapped": name, "mask": mask})
            elif path.startswith("hold/"):
                name = path[5:]
                mask = BUTTON_NAMES.get(name.lower())
                if mask is None:
                    return self._send({"ok": False, "error": f"unknown button: {name}"}, 400)
                r = cdc.command("INPUT.INJECT", buttons=mask)
                user, plat = self._attrib()
                events.publish("press", f"hold {name.upper()}", user, plat)
                self._send(r)
            elif path == "release":
                user, plat = self._attrib()
                r = cdc.command("INPUT.INJECT", buttons=0)
                self._send(r)
                if r.get("ok"):
                    events.publish("reset", "release", user, plat)
            elif path == "inject":
                # Raw INPUT.INJECT — body forwarded verbatim. For advanced
                # use (custom analog values, non-zero slot, etc.).
                length = int(self.headers.get("Content-Length", 0))
                try:
                    args = json.loads(self.rfile.read(length) or b"{}")
                except Exception:
                    return self._send({"ok": False, "error": "bad json body"}, 400)
                self._send(cdc.command("INPUT.INJECT", **args))
            else:
                self._send({"ok": False, "error": "not found"}, 404)

    return Handler


def main():
    ap = argparse.ArgumentParser(description="Joypad live-remap REST bridge")
    ap.add_argument("port", help="serial port, e.g. /dev/cu.usbmodemXXXX or COM5")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--http-port", type=int, default=8777)
    ap.add_argument("--define-demo", action="store_true",
                    help="save the demo profiles to the device, then exit")
    args = ap.parse_args()

    try:
        cdc = CdcClient(args.port, args.baud)
    except Exception as e:
        print(f"Failed to open {args.port}: {e}", file=sys.stderr)
        sys.exit(1)

    info = cdc.command("INFO")
    print(f"Connected {args.port}: {info}")
    # INFO returns the device's fields directly ({"app":..., "version":...}), no "ok"
    # key. Only warn when we got an actual error or timeout back.
    if "error" in info:
        print(f"WARNING: {info['error']} — is this the adapter's CDC port, "
              "and is something else (config.joypad.ai) holding it?", file=sys.stderr)

    if args.define_demo:
        define_demo(cdc)
        cdc.close()
        return

    httpd = ThreadingHTTPServer(("127.0.0.1", args.http_port), make_handler(cdc))
    print(f"REST API on http://127.0.0.1:{args.http_port}/  "
          f"(GET /profiles | POST /profile/<n> | POST /neutral)")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        cdc.close()


if __name__ == "__main__":
    main()
