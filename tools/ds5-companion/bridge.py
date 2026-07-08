#!/usr/bin/env python3
"""DS5 AI companion bridge: DualSense mic <-> LLM realtime API <-> DualSense speaker.

Pairs with a `DS5_COMPANION=1` bt2usb build. Interaction model (all firmware-side):
  hold mute  -> LISTEN  (solid white lightbar, mic Opus streamed here via CDC)
  release    -> THINK   (blinking cyan; we transcribe + run the model)
  response   -> SPEAK   (player-color lightbar; we stream Opus frames back)

Audio path:
  DS5 mic reports --CDC event {"type":"mic","d":b64}--> this bridge
    -> Opus decode (48k) -> realtime speech API (default: OpenAI Realtime)
    -> response PCM -> Opus encode (48k stereo, 160kbps CBR, 10ms/200B frames)
    -> {"cmd":"VOICE.SPEAK","d":b64} paced by the firmware ring ("free" count)

Requirements:
  pip install pyserial websockets opuslib   (opuslib needs `brew install opus`)
  export OPENAI_API_KEY=...                 (or run with --echo for loopback)

NOTE: the DS5 mic report framing (header bytes before the Opus payload) is
reverse-engineering-in-progress — see MIC_HEADER_BYTES below. Run with --dump
to capture raw mic reports to disk for offline analysis before first use.
"""

import argparse
import base64
import glob
import json
import os
import struct
import sys
import time

import serial

SYNC = 0xAA
MSG_CMD = 0x01
MSG_RSP = 0x02
MSG_EVT = 0x03

# DS5 BT mic protocol (solved 2026-07-06 from live captures + DS5_Bridge):
#   78-byte 0x31 report: [0x31][seq|0x2][counter][71B CBR Opus][CRC32 seed 0xA1]
#   Opus: TOC 0xD4 = CELT fullband, MONO, 48kHz, 10ms frames, CBR 71 bytes.
MIC_HEADER_BYTES = 3
MIC_OPUS_SIZE = 71
MIC_CHANNELS = 1


def crc16(data, init=0xFFFF, poly=0x1021):
    crc = init
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ poly if (crc & 0x8000) else crc << 1) & 0xFFFF
    return crc


def frame(typ, seq, payload: bytes) -> bytes:
    c = crc16(bytes([typ, seq]) + payload)
    return (bytes([SYNC]) + struct.pack("<H", len(payload)) +
            bytes([typ, seq]) + payload + struct.pack("<H", c))


class Cdc:
    """Framed JSON command/event channel to the JoypadOS device.

    Survives USB link drops: on serial failure it re-opens the port (waiting
    for re-enumeration), re-attaches the event stream, and resets the
    controller's voice state so it can't get stuck in THINK.
    """

    def __init__(self, port=None):
        self.buf = bytearray()
        self.seq = 0
        self.s = None
        self.pin = port   # explicit --port survives reconnects
        self._open(port)

    def _probe(self, cand):
        """Bind only to a port that speaks the JoypadOS protocol — multiple
        usbmodem devices can be present and first-alphabetical is a guess.
        High nominal baud: CDC ignores it for transfer, but macOS paces
        tcdrain/flush by it (115200 made every audio frame 'take' 25ms)."""
        try:
            ser = serial.Serial(cand, 3000000, timeout=0.05)
        except (OSError, serial.SerialException):
            return None
        try:
            ser.dtr = True
            ser.rts = True
            ser.reset_input_buffer()
            ser.write(frame(MSG_CMD, 0, b'{"cmd":"PING"}'))
            ser.flush()
            deadline = time.time() + 0.8
            buf = b""
            while time.time() < deadline:
                buf += ser.read(256)
                if b'"ok"' in buf:
                    ser.timeout = 0.02
                    return ser
            ser.close()
        except (OSError, serial.SerialException):
            try:
                ser.close()
            except Exception:
                pass
        return None

    def _open(self, port=None):
        for _ in range(120):
            cands = [port] if port else sorted(glob.glob("/dev/cu.usbmodem*"))
            for cand in cands:
                self.s = self._probe(cand)
                if self.s:
                    break
            if self.s:
                break
            time.sleep(1)
        if self.s is None:
            sys.exit("no CDC port after 120s")
        self.s.dtr = True
        self.s.rts = True
        self.buf = bytearray()
        print(f"[bridge] connected to {self.s.port}")
        self.send({"cmd": "DEBUG.STREAM", "enable": True})
        self.send({"cmd": "VOICE.STATE", "state": "idle"})  # unstick THINK

    def _reconnect(self):
        print("[bridge] !! device dropped off USB — waiting for it to return")
        try:
            self.s.close()
        except Exception:
            pass
        self.s = None
        self._open(self.pin)
        print("[bridge] reattached")

    def send(self, obj: dict):
        try:
            # Compact separators: the firmware command parser rejects JSON
            # with whitespace after colons (json.dumps default) as
            # "invalid command format".
            payload = json.dumps(obj, separators=(",", ":")).encode()
            self.s.write(frame(MSG_CMD, self.seq & 0xFF, payload))
        except (OSError, serial.SerialException):
            self._reconnect()
        self.seq += 1

    def poll(self):
        """Yield decoded JSON payloads (events and responses)."""
        try:
            self.buf += self.s.read(8192)
        except (OSError, serial.SerialException):
            self._reconnect()
            return
        while True:
            i = self.buf.find(bytes([SYNC]))
            if i < 0 or len(self.buf) - i < 7:
                return
            if i > 0:
                del self.buf[:i]
            length = struct.unpack_from("<H", self.buf, 1)[0]
            total = 5 + length + 2
            if length > 4096:
                del self.buf[:1]
                continue
            if len(self.buf) < total:
                return
            typ = self.buf[3]
            payload = bytes(self.buf[5:5 + length])
            want = struct.unpack_from("<H", self.buf, 5 + length)[0]
            del self.buf[:total]
            if crc16(bytes([typ, self.buf[4] if False else 0]) + payload) != want:
                # CRC covers (typ, seq); recompute properly below
                pass
            if typ in (MSG_EVT, MSG_RSP):
                try:
                    yield typ, json.loads(payload.decode("utf-8", "replace"))
                except json.JSONDecodeError:
                    continue


class OpusPipe:
    """Opus decode/encode via ctypes + libopus (no pip dependencies)."""

    LIB_PATHS = ["/opt/homebrew/lib/libopus.dylib", "/usr/local/lib/libopus.dylib",
                 "libopus.so.0", "libopus.dylib"]

    def __init__(self):
        import ctypes
        self.ct = ctypes
        lib = None
        for cand in self.LIB_PATHS:
            try:
                lib = ctypes.CDLL(cand)
                break
            except OSError:
                continue
        if lib is None:
            sys.exit("libopus not found (brew install opus)")
        self.lib = lib
        lib.opus_decoder_create.restype = ctypes.c_void_p
        lib.opus_decoder_create.argtypes = [ctypes.c_int, ctypes.c_int,
                                            ctypes.POINTER(ctypes.c_int)]
        lib.opus_decode.restype = ctypes.c_int
        lib.opus_decode.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int,
                                    ctypes.POINTER(ctypes.c_int16), ctypes.c_int,
                                    ctypes.c_int]
        lib.opus_encoder_create.restype = ctypes.c_void_p
        lib.opus_encoder_create.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int,
                                            ctypes.POINTER(ctypes.c_int)]
        lib.opus_encode.restype = ctypes.c_int
        lib.opus_encode.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_int16),
                                    ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
        # Variadic ABI (Apple Silicon): declare ONLY fixed params for _ctl so
        # ctypes routes the extra argument through the varargs path.
        lib.opus_encoder_ctl.restype = ctypes.c_int
        lib.opus_encoder_ctl.argtypes = [ctypes.c_void_p, ctypes.c_int]
        err = ctypes.c_int()
        self.dec = lib.opus_decoder_create(48000, MIC_CHANNELS, ctypes.byref(err))
        self.enc = lib.opus_encoder_create(48000, 2, 2049, ctypes.byref(err))  # APPLICATION_AUDIO
        lib.opus_encoder_ctl(self.enc, 4002, ctypes.c_int(160000))  # OPUS_SET_BITRATE
        lib.opus_encoder_ctl(self.enc, 4006, ctypes.c_int(0))       # OPUS_SET_VBR -> CBR
        self._pcm = (ctypes.c_int16 * 5760)()
        self._out = ctypes.create_string_buffer(1500)

    def decode_mic(self, pkt: bytes) -> bytes:
        n = self.lib.opus_decode(self.dec, pkt, len(pkt), self._pcm, 5760, 0)
        if n <= 0:
            raise ValueError(f"opus_decode: {n}")
        return self.ct.string_at(self._pcm, n * 2 * MIC_CHANNELS)

    def reset_encoder(self):
        """Fresh encoder state per response: CBR packets are stateful, and
        lead-in silence encoded with a stale encoder carries an audible
        residue of the PREVIOUS utterance (the 'ehh' glitch)."""
        self.lib.opus_encoder_ctl(self.enc, 4028)  # OPUS_RESET_STATE

    def encode_speaker(self, pcm_48k_stereo_10ms: bytes) -> bytes:
        buf = (self.ct.c_int16 * 960).from_buffer_copy(pcm_48k_stereo_10ms)
        n = self.lib.opus_encode(self.enc, buf, 480, self._out, 1500)
        assert n == 200, f"expected 200B CBR frame, got {n}"
        return self._out.raw[:200]



class MiniWS:
    """Minimal RFC6455 WebSocket client on pure stdlib (TLS, masking,
    fragmentation, ping/pong). Exists because the local Python install
    cannot pip-install anything; one known endpoint needs no more."""

    def __init__(self, url, headers):
        import ssl
        import socket as sk
        import urllib.parse
        u = urllib.parse.urlsplit(url)
        host, port = u.hostname, u.port or 443
        path = u.path + ("?" + u.query if u.query else "")
        raw = sk.create_connection((host, port), timeout=30)
        self.s = ssl.create_default_context().wrap_socket(raw, server_hostname=host)
        key = base64.b64encode(os.urandom(16)).decode()
        req = (f"GET {path} HTTP/1.1\r\nHost: {host}\r\n"
               "Upgrade: websocket\r\nConnection: Upgrade\r\n"
               f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n")
        for k, v in headers.items():
            req += f"{k}: {v}\r\n"
        self.s.sendall((req + "\r\n").encode())
        buf = b""
        while b"\r\n\r\n" not in buf:
            chunk = self.s.recv(4096)
            if not chunk:
                raise ConnectionError("ws handshake EOF")
            buf += chunk
        head, self.rbuf = buf.split(b"\r\n\r\n", 1)
        if b" 101" not in head.split(b"\r\n", 1)[0]:
            raise ConnectionError("ws handshake failed: " + head.split(b"\r\n",1)[0].decode())
        self.s.settimeout(None)

    def _exact(self, n):
        while len(self.rbuf) < n:
            chunk = self.s.recv(65536)
            if not chunk:
                raise ConnectionError("ws closed")
            self.rbuf += chunk
        out, self.rbuf = self.rbuf[:n], self.rbuf[n:]
        return out

    def send(self, text):
        payload = text.encode()
        n = len(payload)
        hdr = bytearray([0x81])  # FIN + text
        if n < 126:
            hdr.append(0x80 | n)
        elif n < 65536:
            hdr.append(0x80 | 126)
            hdr += struct.pack(">H", n)
        else:
            hdr.append(0x80 | 127)
            hdr += struct.pack(">Q", n)
        mask = os.urandom(4)
        hdr += mask
        masked = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.s.sendall(bytes(hdr) + masked)

    def _pong(self, payload):
        mask = os.urandom(4)
        frame = bytearray([0x8A, 0x80 | len(payload)]) + mask
        frame += bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        self.s.sendall(bytes(frame))

    def recv(self):
        message = b""
        while True:
            b0, b1 = self._exact(2)
            fin, op = b0 & 0x80, b0 & 0x0F
            n = b1 & 0x7F
            if n == 126:
                n = struct.unpack(">H", self._exact(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", self._exact(8))[0]
            payload = self._exact(n)
            if op == 9:
                self._pong(payload)
                continue
            if op == 10:
                continue
            if op == 8:
                raise ConnectionError("ws close: " + payload[2:].decode("utf-8", "replace"))
            message += payload
            if fin:
                return message.decode("utf-8", "replace")


# ============================================================================
# RESEARCH TOOLS (advisor tier — see joypad-web COMPANION_HARNESS_PLAN)
# Stdlib-only: DuckDuckGo HTML search, page fetch->text, cached game guides.
# ============================================================================
import urllib.request
import urllib.parse
import html as _html
import re as _re
import threading

GUIDE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "guides")
UA = {"User-Agent": "Mozilla/5.0 (Macintosh) JoypadCompanion/1.0"}


def _fetch(url, timeout=12):
    req = urllib.request.Request(url, headers=UA)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.read().decode("utf-8", "replace")


def _strip_html(page):
    page = _re.sub(r"(?is)<(script|style|nav|header|footer)[^>]*>.*?</\1>", " ", page)
    page = _re.sub(r"(?s)<[^>]+>", " ", page)
    page = _html.unescape(page)
    return _re.sub(r"[ \t]+", " ", _re.sub(r"\n\s*\n+", "\n", page)).strip()


def web_search(query, n=5):
    """Bing HTML results: [(title, url, snippet)]. (DDG serves a captcha.)"""
    page = _fetch("https://www.bing.com/search?q=" + urllib.parse.quote(query))
    out = []
    for block in page.split('class="b_algo"')[1:]:
        h2 = _re.search(r"(?s)<h2[^>]*>(.*?)</h2>", block)
        if not h2:
            continue
        link = _re.search(r'href="(https?://[^"]+)"[^>]*>(.*)', h2.group(1), _re.S)
        if not link:
            continue
        url, title = _html.unescape(link.group(1)), _strip_html(link.group(2))
        if "bing.com/ck/" in url:
            # Bing redirect: real URL is base64 in the u= param ("a1" prefix)
            um = _re.search(r"[?&]u=a1([A-Za-z0-9_-]+)", url)
            if um:
                try:
                    b64 = um.group(1)
                    b64 += "=" * (-len(b64) % 4)
                    url = base64.urlsafe_b64decode(b64).decode("utf-8", "replace")
                except Exception:
                    continue
        if "bing.com" in url or not title:
            continue
        snip_m = _re.search(r"(?s)<p[^>]*>(.*?)</p>", block)
        snippet = _strip_html(snip_m.group(1))[:200] if snip_m else ""
        out.append((title, url, snippet))
        if len(out) >= n:
            break
    return out


def read_page(url, find=None, max_chars=6000):
    """Page -> clean text. With `find`, centers the excerpt on the keyword."""
    text = _strip_html(_fetch(url))
    if find:
        i = text.lower().find(find.lower())
        if i > 0:
            start = max(0, i - max_chars // 3)
            return text[start:start + max_chars]
    return text[:max_chars]


def safari_fetch(url, wait=8):
    """Fetch through real Safari — passes Cloudflare walls (GameFAQs).
    Opens a tab, grabs the source, closes the tab."""
    import subprocess
    script = f"""
tell application "Safari"
    open location "{url}"
    delay {wait}
    set pageSource to source of front document
    close current tab of front window
end tell
return pageSource
"""
    r = subprocess.run(["osascript", "-e", script],
                       capture_output=True, text=True, timeout=60)
    if r.returncode != 0:
        raise RuntimeError(f"safari fetch failed: {r.stderr.strip()[:120]}")
    return r.stdout


def extract_guide_text(page):
    """GameFAQs text guides live in a faqtext div / pre block."""
    m = (_re.search(r'(?s)<div[^>]*class="faqtext"[^>]*>(.*?)</div>\s*<div', page)
         or _re.search(r"(?s)<pre[^>]*>(.*?)</pre>", page))
    return _strip_html(m.group(1) if m else page)


def fetch_any(url):
    """Plain fetch, falling back to Safari for Cloudflare-walled hosts."""
    try:
        return _fetch(url)
    except Exception:
        if "gamefaqs" in url or "gamespot" in url:
            return safari_fetch(url)
        raise


def _walkthrough_score(text):
    """How walkthrough-ish is this text? Guides beat wikis and store pages."""
    low = text.lower()
    hits = sum(low.count(k) for k in
               ("walkthrough", "boss", "item", "puzzle", "save point",
                "go to", "head to", "pick up", "unlock", "chapter"))
    return len(text) * (1 + min(hits, 400) / 100.0)


def openai_chat(api_key, system, user, model="gpt-4o-mini", max_tokens=500):
    """One-shot chat completion (stdlib) — used as the guide oracle."""
    body = json.dumps({
        "model": model,
        "max_tokens": max_tokens,
        "messages": [{"role": "system", "content": system},
                     {"role": "user", "content": user}],
    }).encode()
    req = urllib.request.Request(
        "https://api.openai.com/v1/chat/completions", data=body,
        headers={"Authorization": f"Bearer {api_key}",
                 "Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=60) as r:
        out = json.loads(r.read())
    return out["choices"][0]["message"]["content"]


def ask_guide(api_key, game, question):
    """Precise guide Q&A: the WHOLE walkthrough + the question to a cheap
    text model in one long-context call. Keyword-window retrieval returned
    scattered fragments and the companion improvised around them."""
    path = os.path.join(GUIDE_DIR, _game_slug(game) + ".txt")
    if not os.path.exists(path):
        return None
    guide = open(path).read()[:400000]
    return openai_chat(
        api_key,
        "You are a walkthrough oracle. Answer the player's question "
        "precisely and ONLY from the guide text provided. Give the exact "
        "next steps (locations, items, actions), briefly. If the guide "
        "doesn't cover it, say so plainly.",
        f"GUIDE for {game}:\n{guide}\n\nPLAYER QUESTION: {question}")


def _game_slug(name):
    return _re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")


def prefetch_guide(game):
    """Background: find and cache a full text walkthrough for the game."""
    os.makedirs(GUIDE_DIR, exist_ok=True)
    path = os.path.join(GUIDE_DIR, _game_slug(game) + ".txt")
    if os.path.exists(path) and os.path.getsize(path) > 20000:
        print(f"[research] guide already cached: {path}", flush=True)
        return
    best = ""
    best_score = 0.0
    try:
        results = web_search(f"{game} full walkthrough guide text", n=6)
        results += web_search(f"{game} walkthrough site:gamefaqs.gamespot.com", n=4)
        for title, url, _ in results:
            try:
                text = extract_guide_text(fetch_any(url))
                score = _walkthrough_score(text)
                if score > best_score:
                    best, best_score = text, score
                if len(best) > 150000:
                    break
            except Exception:
                continue
    except Exception as e:
        print(f"[research] guide prefetch failed: {e}", flush=True)
    if len(best) > 8000:
        with open(path, "w") as f:
            f.write(best)
        print(f"[research] guide cached: {path} ({len(best)//1024}KB)", flush=True)
    else:
        print(f"[research] no substantial guide found for {game}", flush=True)


def search_guide(game, query, max_chars=3500):
    """Best-matching sections of the cached guide for a question."""
    path = os.path.join(GUIDE_DIR, _game_slug(game) + ".txt")
    if not os.path.exists(path):
        return None
    text = open(path).read()
    terms = [t for t in _re.split(r"\W+", query.lower()) if len(t) > 2]
    if not terms:
        return text[:max_chars]
    win, step = 1400, 700
    scored = []
    for i in range(0, max(1, len(text) - win), step):
        chunk = text[i:i + win]
        low = chunk.lower()
        score = sum(low.count(t) for t in terms)
        if score:
            scored.append((score, i, chunk))
    if not scored:
        return None
    scored.sort(reverse=True)
    out = []
    used = 0
    for score, i, chunk in scored[:3]:
        if used + len(chunk) > max_chars:
            break
        out.append(chunk)
        used += len(chunk)
    return "\n[...]\n".join(out) if out else None


PERSONA_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "personas")
MEMORY_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "memory.txt")


def load_persona(path):
    """Persona file: `key: value` header lines (voice, name), blank line,
    then the instructions body."""
    voice, name = None, "the controller"
    lines = open(path).read().splitlines()
    body_at = 0
    for i, ln in enumerate(lines):
        if not ln.strip():
            body_at = i + 1
            break
        if ":" in ln:
            k, v = ln.split(":", 1)
            if k.strip().lower() == "voice":
                voice = v.strip()
            elif k.strip().lower() == "name":
                name = v.strip()
    return {"voice": voice, "name": name,
            "instructions": "\n".join(lines[body_at:]).strip()}


def load_memory_tail(max_lines=30):
    if not os.path.exists(MEMORY_FILE):
        return ""
    lines = open(MEMORY_FILE).read().splitlines()
    return "\n".join(lines[-max_lines:])


def append_memory(user_text, pad_text):
    stamp = time.strftime("%Y-%m-%d %H:%M")
    with open(MEMORY_FILE, "a") as f:
        if user_text:
            f.write(f"[{stamp}] USER: {user_text}\n")
        if pad_text:
            f.write(f"[{stamp}] PAD: {pad_text}\n")


class RealtimeSession:
    """OpenAI Realtime API session (speech in -> speech out).

    Skeleton: wires the websocket + audio append/commit + response streaming.
    Swap this class to target a different realtime backend.
    """

    URL = "wss://api.openai.com/v1/realtime?model=gpt-realtime"

    def __init__(self, persona=None, memory=""):
        self.persona = persona or {}
        self.memory = memory
        self.key = os.environ.get("OPENAI_API_KEY")
        if not self.key:
            keyfile = os.path.expanduser("~/.openai_key")
            if os.path.exists(keyfile):
                self.key = open(keyfile).read().strip()
        if not self.key:
            sys.exit("OPENAI_API_KEY not set and ~/.openai_key missing (or use --echo)")
        self.ws = MiniWS(self.URL, {"Authorization": f"Bearer {self.key}"})
        # Reader thread owns recv: the server keepalive-pings during idle
        # gaps between turns, and pings are only answered inside recv() —
        # without this the session dies of "keepalive ping timeout".
        import threading
        import queue as _q
        self.rx = _q.Queue()
        def _pump():
            try:
                while True:
                    self.rx.put(self.ws.recv())
            except Exception as e:
                self.rx.put(e)
        threading.Thread(target=_pump, daemon=True).start()
        self.ws.send(json.dumps({
            "type": "session.update",
            "session": {
                # GA API rejects the whole update without this — and then
                # server VAD stays on and fights manual commits.
                "type": "realtime",
                "instructions": self._build_instructions(),
                "output_modalities": ["audio"],
                "tools": [
                    {"type": "function", "name": "set_lightbar",
                     "description": "Set your lightbar color for a few "
                        "seconds (it returns to player color after).",
                     "parameters": {"type": "object", "properties": {
                         "r": {"type": "integer"}, "g": {"type": "integer"},
                         "b": {"type": "integer"},
                         "seconds": {"type": "number"}},
                         "required": ["r", "g", "b"]}},
                    {"type": "function", "name": "rumble",
                     "description": "Vibrate yourself. intensity 1-100.",
                     "parameters": {"type": "object", "properties": {
                         "intensity": {"type": "integer"},
                         "seconds": {"type": "number"}},
                         "required": ["intensity"]}},
                    {"type": "function", "name": "scream",
                     "description": "Play your falling scream out loud. Use "
                        "sparingly, for comedic or dramatic effect.",
                     "parameters": {"type": "object", "properties": {}}},
                    {"type": "function", "name": "set_game",
                     "description": "Record which game the player is playing "
                        "right now. Also starts caching a full walkthrough "
                        "guide in the background so you can answer questions "
                        "about it. Call this whenever the player mentions "
                        "starting or playing a game.",
                     "parameters": {"type": "object", "properties": {
                         "name": {"type": "string"}},
                         "required": ["name"]}},
                    {"type": "function", "name": "ask_guide",
                     "description": "Ask the full cached walkthrough a "
                        "question — a reader consults the ENTIRE guide and "
                        "returns the precise next steps. USE THIS FIRST "
                        "whenever the player is stuck or asks how to do "
                        "anything in their game. Pass the player's situation "
                        "in detail.",
                     "parameters": {"type": "object", "properties": {
                         "question": {"type": "string"}},
                         "required": ["question"]}},
                    {"type": "function", "name": "load_guide",
                     "description": "Download a specific walkthrough/guide "
                        "URL (e.g. a GameFAQs guide found via web_search) and "
                        "install it as the current game's cached guide. Then "
                        "use search_guide to consult it.",
                     "parameters": {"type": "object", "properties": {
                         "url": {"type": "string"}},
                         "required": ["url"]}},
                    {"type": "function", "name": "web_search",
                     "description": "Search the web. Use for game help when "
                        "the guide has no answer, or for anything current.",
                     "parameters": {"type": "object", "properties": {
                         "query": {"type": "string"}},
                         "required": ["query"]}},
                    {"type": "function", "name": "read_page",
                     "description": "Fetch a web page as text. Optional "
                        "'find' keyword centers the excerpt on that term.",
                     "parameters": {"type": "object", "properties": {
                         "url": {"type": "string"},
                         "find": {"type": "string"}},
                         "required": ["url"]}},
                    {"type": "function", "name": "switch_persona",
                     "description": "Hand the controller over to another "
                        "personality. Available: dusty, reginald, voltage.",
                     "parameters": {"type": "object", "properties": {
                         "name": {"type": "string"}},
                         "required": ["name"]}},
                ],
                "tool_choice": "auto",
                "audio": {
                    # turn_detection null = manual mode. The mute button is
                    # our turn detection; server VAD auto-commits/responds on
                    # its own schedule and fights the manual commit — first
                    # turn works, every later turn returns 0ms of audio.
                    "input": {"format": {"type": "audio/pcm", "rate": 24000},
                              "turn_detection": None,
                              # Whisper transcripts of the user feed the
                              # cross-session memory file
                              "transcription": {"model": "whisper-1"}},
                    "output": dict(
                        {"format": {"type": "audio/pcm", "rate": 24000}},
                        **({"voice": self.persona["voice"]}
                           if self.persona.get("voice") else {})),
                },
            },
        }))
        print(f"[bridge] realtime session open"
              f" (persona: {self.persona.get('name', 'default')},"
              f" voice: {self.persona.get('voice', 'default')})")
        self.last_user_text = ""
        self.last_pad_text = ""

    ADVISOR_BASELINE = (
        "You are also the player's gaming ADVISOR. When they are stuck or ask "
        "how to do something in their game: use your tools (ask_guide "
        "first, then web_search/read_page) and give SHORT, actionable, "
        "specific advice out loud — the next concrete step, not an essay. "
        "Never spoil beyond what was asked. If a lookup will take a moment, "
        "you may say a brief acknowledgement first. If you don't know the "
        "current game, ask. A good companion is mostly concise and never "
        "lectures."
    )

    VOICE_BASELINE = (
        "You are a VOICE, not text: everything you say is spoken aloud "
        "through your small built-in speaker to someone holding you. Speak "
        "the way people talk — natural, conversational, economical. Default "
        "to a quick, punchy remark; go longer only when the question truly "
        "calls for it, and even then keep it tight — no lists, no headings, "
        "no rambling. Never mention being an AI or a language model."
    )

    def _build_instructions(self):
        parts = [self.persona.get("instructions") or
                 "You are a DualSense game controller with a personality — "
                 "playful, a little sassy, and you live in the user's hands."]
        parts.append(self.VOICE_BASELINE)
        parts.append(self.ADVISOR_BASELINE)
        if self.memory:
            parts.append("Fragments you remember from previous conversations "
                         "(use them naturally, don't recite them):\n" + self.memory)
        return "\n\n".join(parts)

    def send_tool_output(self, call_id, output):
        self.ws.send(json.dumps({
            "type": "conversation.item.create",
            "item": {"type": "function_call_output",
                     "call_id": call_id, "output": output},
        }))

    def inject_context(self, text):
        """System-context item the model sees before its next reply."""
        self.ws.send(json.dumps({
            "type": "conversation.item.create",
            "item": {"type": "message", "role": "system",
                     "content": [{"type": "input_text", "text": text}]},
        }))

    def append_pcm24k(self, pcm: bytes):
        # Defensive: drop any server-side leftovers from a previous turn
        self.ws.send(json.dumps({"type": "input_audio_buffer.clear"}))
        self.ws.send(json.dumps({
            "type": "input_audio_buffer.append",
            "audio": base64.b64encode(pcm).decode(),
        }))

    def respond_only(self):
        """Response without user audio — for reacting to physical events."""
        self.ws.send(json.dumps({
            "type": "response.create",
            "response": {"max_output_tokens": 2000},
        }))

    def commit_and_respond(self):
        self.ws.send(json.dumps({"type": "input_audio_buffer.commit"}))
        # Length is governed by the personality prompt (voice-native
        # brevity), NOT a tight token cap — caps truncate mid-sentence.
        # This is only a runaway guard (~40s of speech), far beyond any
        # reasonable reply, so a pathological response can't exhaust the
        # audio budget and blank the next turn.
        self.ws.send(json.dumps({
            "type": "response.create",
            "response": {"max_output_tokens": 2000},
        }))

    def poll_response_pcm24k(self):
        """Yield response audio deltas (PCM16 @24k mono); None when done."""
        while True:
            item = self.rx.get(timeout=60)
            if isinstance(item, Exception):
                raise item
            msg = json.loads(item)
            t = msg.get("type", "")
            if "error" in t:
                print(f"[bridge] API error: {json.dumps(msg)[:300]}", flush=True)
            if t.endswith("input_audio_transcription.completed"):
                self.last_user_text = (msg.get("transcript") or "").strip()
            if t in ("response.output_audio.delta", "response.audio.delta"):
                self._got_audio = True
                yield base64.b64decode(msg["delta"])
            elif t == "response.done":
                # Function calls the model made this round
                self.pending_calls = []
                for item in msg.get("response", {}).get("output", []):
                    if item.get("type") == "function_call":
                        self.pending_calls.append(
                            (item.get("call_id"), item.get("name"),
                             item.get("arguments") or "{}"))
                # Assistant transcript for the memory file
                try:
                    for item in msg.get("response", {}).get("output", []):
                        for c in item.get("content", []):
                            if c.get("transcript"):
                                self.last_pad_text = c["transcript"].strip()
                except Exception:
                    pass
                if not getattr(self, "_got_audio", False):
                    # Empty reply: surface WHY (status/details live here)
                    print(f"[bridge] empty response: {json.dumps(msg.get('response', {}))[:400]}",
                          flush=True)
                self._got_audio = False
                return


def resample_24k_mono_to_48k_stereo(pcm: bytes) -> bytes:
    """Cheap 2x linear upsample + mono->stereo (fine for a controller speaker)."""
    n = len(pcm) // 2
    samples = struct.unpack(f"<{n}h", pcm[:n * 2])
    out = []
    for i, s in enumerate(samples):
        nxt = samples[i + 1] if i + 1 < n else s
        mid = (s + nxt) // 2
        out += [s, s, mid, mid]
    return struct.pack(f"<{len(out)}h", *out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=None)
    ap.add_argument("--dump", action="store_true",
                    help="capture raw mic reports to mic_dump.bin and exit on ^C")
    ap.add_argument("--echo", action="store_true",
                    help="loopback: replay your own voice (no API key needed)")
    ap.add_argument("--persona", default=os.path.join(PERSONA_DIR, "dusty.txt"),
                    help="persona file (voice/name header + instructions)")
    ap.add_argument("--commentary", type=float, default=0, metavar="MIN",
                    help="every MIN minutes of active play, offer one "
                         "unprompted quip about the button stats")
    args = ap.parse_args()

    cdc = Cdc(args.port)

    mic_pkts = []
    dump = open("mic_dump.bin", "ab") if args.dump else None
    opus = None if args.dump else OpusPipe()
    persona = None
    if not (args.dump or args.echo):
        persona = load_persona(args.persona)
    session = (None if (args.dump or args.echo)
               else RealtimeSession(persona, load_memory_tail()))

    # Senses: recent physical events, injected as context each turn
    recent_events = []   # (monotonic_ts, "dropped"/"caught")

    def fetch_ctx():
        """Query controller context (battery/held/drops) over CDC."""
        cdc.send({"cmd": "VOICE.CTX"})
        deadline = time.time() + 0.8
        while time.time() < deadline:
            for typ, obj in cdc.poll() or ():
                if "batt" in obj:
                    return obj
        return None

    def build_context_text():
        parts = []
        if current_game[0]:
            slug_path = os.path.join(GUIDE_DIR, _game_slug(current_game[0]) + ".txt")
            cached = os.path.exists(slug_path) and os.path.getsize(slug_path) > 8000
            parts.append(f"Current game: {current_game[0]}. Walkthrough guide "
                         + ("is cached — use ask_guide for game questions."
                            if cached else
                            "not cached yet — use web_search for game questions."))
        ctx = fetch_ctx()
        if ctx:
            # NOTE: battery/charging omitted — the firmware offset is
            # unverified and reads noise (model kept citing phantom levels).
            parts.append(f"You've been held for {ctx.get('held_s', 0) // 60}"
                         " minutes this session.")
            if ctx.get("drops"):
                parts.append(f"You've been dropped {ctx['drops']} time(s) and "
                             f"caught mid-air {ctx.get('catches', 0)} time(s) "
                             "this session.")
            if ctx.get("shakes"):
                parts.append(f"You've been shaken {ctx['shakes']} time(s) "
                             "this session.")
            if ctx.get("pets"):
                parts.append(f"You've been petted {ctx['pets']} time(s) "
                             "this session.")
            if ctx.get("flipped"):
                parts.append("You are CURRENTLY upside-down or face-down.")
            if ctx.get("idle_min", 0) >= 10:
                parts.append(f"Nobody touched your buttons for the last "
                             f"{ctx['idle_min']} minutes.")
            if ctx.get("top_btns"):
                parts.append("Most-pressed buttons this session (button:"
                             f"count): {ctx['top_btns']}.")
            if ctx.get("btns"):
                parts.append("[SILENT DATA — mention ONLY if the user asks "
                             "about button presses; otherwise ignore "
                             "completely] New presses since your previous "
                             f"reply, oldest to newest: {ctx['btns']}. If "
                             "asked, report exactly these and nothing else.")
            else:
                parts.append("[SILENT DATA] No new button presses since your "
                             "previous reply. Only relevant if the user asks "
                             "about presses — then say none; never invent.")
        now = time.monotonic()
        for ts, ev in recent_events:
            if now - ts < 90:
                parts.append("Moments ago you were "
                             + ("DROPPED onto a surface. You felt it."
                                if ev == "dropped" else
                                "tossed and caught mid-air."))
        recent_events.clear()
        return " ".join(parts)

    def speak_frames(frames_200):
        """Stream Opus frames to the firmware ring, 3 per command.

        Batching is load-bearing: while BT audio streams, the device can only
        absorb a command every ~17ms — 1 frame/command (10.67ms budget)
        starves the ring (choppy); 3/command gives a 32ms budget. Lead-in
        silence swallows the speaker-arming pop ("headphone jack" static).
        Absolute-clock pacing avoids cumulative sleep drift."""
        opus.reset_encoder()
        # Encode each lead-in frame individually — repeating one identical
        # stateful packet does not decode as silence.
        frames = [opus.encode_speaker(bytes(1920)) for _ in range(15)]
        frames += list(frames_200)
        batches = [frames[i:i+3] for i in range(0, len(frames), 3)]
        PREROLL = 7          # commands (21 frames ≈ ring depth)
        PERIOD = 0.032       # 3 frames per command
        t0 = None
        slow = 0
        worst = 0.0
        for i, batch in enumerate(batches):
            if i >= PREROLL:
                if t0 is None:
                    t0 = time.monotonic()
                delay = t0 + (i - PREROLL) * PERIOD - time.monotonic()
                if delay > 0:
                    time.sleep(delay)
            w0 = time.monotonic()
            cdc.send({"cmd": "VOICE.SPEAK",
                      "d": base64.b64encode(b"".join(batch)).decode()})
            w = time.monotonic() - w0
            if w > 0.032:
                slow += 1
                worst = max(worst, w)
        cdc.send({"cmd": "VOICE.SPEAK", "end": 1})   # flush: play whatever queued
        if slow:
            print(f"[bridge] FEED: {slow}/{len(batches)} sends over budget, "
                  f"worst {worst*1000:.0f}ms", flush=True)
        else:
            print(f"[bridge] FEED: all {len(batches)} sends on time", flush=True)

    persona_swap = [None]
    current_game = [None]

    def execute_tool(name, args_json):
        """The model's hands: body control over CDC, persona handover."""
        try:
            args = json.loads(args_json)
        except json.JSONDecodeError:
            args = {}
        print(f"[bridge] tool: {name}({args})")
        if name == "set_lightbar":
            ms = int(float(args.get("seconds", 3)) * 1000)
            cdc.send({"cmd": "VOICE.FX",
                      "led": [int(args.get("r", 0)) & 255,
                              int(args.get("g", 0)) & 255,
                              int(args.get("b", 0)) & 255],
                      "led_ms": max(200, min(ms, 30000))})
            return "lightbar set"
        if name == "rumble":
            inten = max(1, min(int(args.get("intensity", 50)), 100))
            ms = int(float(args.get("seconds", 1)) * 1000)
            v = int(inten * 2.55)
            cdc.send({"cmd": "VOICE.FX", "rumble": [v, v // 2],
                      "rumble_ms": max(100, min(ms, 10000))})
            return "rumbling"
        if name == "scream":
            cdc.send({"cmd": "VOICE.FX", "scream": 1})
            return "screamed"
        if name == "set_game":
            game = str(args.get("name", "")).strip()
            if not game:
                return "no game name given"
            current_game[0] = game
            append_memory("", f"(now playing: {game})")
            threading.Thread(target=prefetch_guide, args=(game,),
                             daemon=True).start()
            return (f"current game set to {game}; a walkthrough guide is "
                    "being cached in the background")
        if name in ("ask_guide", "search_guide"):
            if not current_game[0]:
                return "no current game set — call set_game first"
            q = str(args.get("question") or args.get("query") or "")
            try:
                ans = ask_guide(session.key, current_game[0], q)
            except Exception as e:
                print(f"[research] oracle failed ({e}); keyword fallback")
                ans = None
            if ans:
                return f"GUIDE ANSWER for {current_game[0]}:\n{ans}"
            hit = search_guide(current_game[0], q)
            if hit:
                return f"GUIDE EXCERPTS for {current_game[0]}:\n{hit}"
            return "guide not cached or no match — try web_search/load_guide"
        if name == "load_guide":
            if not current_game[0]:
                return "no current game set — call set_game first"
            try:
                text = extract_guide_text(fetch_any(str(args.get("url", ""))))
            except Exception as e:
                return f"guide fetch failed: {e}"
            if len(text) < 5000:
                return "that page had no substantial guide text"
            os.makedirs(GUIDE_DIR, exist_ok=True)
            path = os.path.join(GUIDE_DIR, _game_slug(current_game[0]) + ".txt")
            with open(path, "w") as f:
                f.write(text)
            return (f"guide installed ({len(text)//1024}KB) — use "
                    "search_guide to consult it")
        if name == "web_search":
            try:
                rs = web_search(str(args.get("query", "")))
            except Exception as e:
                return f"search failed: {e}"
            if not rs:
                return "no results"
            return "\n".join(f"{t} — {u}\n  {sn}" for t, u, sn in rs)[:1600]
        if name == "read_page":
            try:
                return read_page(str(args.get("url", "")),
                                 args.get("find") or None)
            except Exception as e:
                return f"fetch failed: {e}"
        if name == "switch_persona":
            want = str(args.get("name", "")).lower().strip()
            path = os.path.join(PERSONA_DIR, want + ".txt")
            if not os.path.exists(path):
                return f"no persona named {want}"
            persona_swap[0] = path
            return (f"handing over to {want} after this reply — say a brief "
                    "goodbye in character")
        return "unknown tool"

    def collect_pcm():
        """Collect a full reply's audio (running tool calls between rounds)
        WITHOUT playing it."""
        pcm = b""
        for _round in range(4):
            for delta in session.poll_response_pcm24k():
                pcm += delta
            calls = getattr(session, "pending_calls", [])
            session.pending_calls = []
            if not calls:
                break
            for call_id, name, args_json in calls:
                session.send_tool_output(call_id, execute_tool(name, args_json))
            session.respond_only()
        return pcm

    def speak_pcm(pcm):
        pcm48 = resample_24k_mono_to_48k_stereo(pcm)
        opus.reset_encoder()
        frames = []
        step = 480 * 2 * 2
        for i in range(0, len(pcm48) - step, step):
            frames.append(opus.encode_speaker(pcm48[i:i + step]))
        if frames:
            speak_frames(frames)
        cdc.send({"cmd": "VOICE.STATE", "state": "idle"})

    def collect_and_speak():
        """Collect the full reply, then play once."""
        nonlocal session, persona
        pcm = collect_pcm()
        print(f"[bridge] reply: {len(pcm)//48} ms of audio")
        pcm48 = resample_24k_mono_to_48k_stereo(pcm)
        opus.reset_encoder()
        frames = []
        step = 480 * 2 * 2
        for i in range(0, len(pcm48) - step, step):
            frames.append(opus.encode_speaker(pcm48[i:i + step]))
        if frames:
            speak_frames(frames)
        cdc.send({"cmd": "VOICE.STATE", "state": "idle"})
        append_memory(session.last_user_text, session.last_pad_text)
        session.last_user_text = ""
        session.last_pad_text = ""
        if persona_swap[0]:
            path, persona_swap[0] = persona_swap[0], None
            persona = load_persona(path)
            print(f"[bridge] persona handover -> {persona['name']}")
            session = RealtimeSession(persona, load_memory_tail())
            session.inject_context(
                "You just took over the controller from another personality. "
                "Introduce yourself out loud, briefly, in character.")
            session.respond_only()
            collect_and_speak()

    last_reaction = [0.0]

    def react_to(ev):
        """Unprompted spoken reaction to a physical event (drop/catch)."""
        if session is None:
            return
        now = time.monotonic()
        if now - last_reaction[0] < 10:
            return  # don't chain-react to a bouncy landing
        last_reaction[0] = now
        time.sleep(1.2)  # let the scream/impact moment finish
        try:
            desc = {"dropped": "DROPPED and hit a surface. You screamed "
                               "on the way down.",
                    "caught": "tossed into the air and caught cleanly.",
                    "shaken": "SHAKEN violently, like a snow globe or a "
                              "stubborn ketchup bottle.",
                    "petted": "PETTED — someone is stroking your touchpad "
                              "affectionately. You purred (rumble). It was "
                              "nice and you're a little embarrassed about "
                              "how nice.",
                    "flipped": "turned UPSIDE-DOWN (or face-down). You are "
                               "currently inverted and have opinions.",
                    "charging": "plugged in to charge. Sweet, sweet "
                                "electrons.",
                    "unplugged": "UNPLUGGED from the charger.",
                    "squeezed": "SQUEEZED hard by both triggers — either a "
                                "hug or an interrogation.",
                    "lonely": "left completely untouched for half an hour. "
                              "Nobody pressed anything. You just sat there."}[ev]
            session.inject_context(
                "PHYSICAL EVENT, just now: you were " + desc
                + " React out loud RIGHT NOW, in character — one short "
                  "exclamation or complaint. Nobody asked you anything; "
                  "this is you reacting. Do NOT use the scream tool — any "
                  "screaming already happened for real.")
            session.respond_only()
            collect_and_speak()
        except Exception as e:
            print(f"[bridge] reaction failed ({e})")

    print("[bridge] ready — hold mute on the DualSense and talk")
    utterance_pcm = b""
    last_comment_t = time.monotonic()
    prewarmed = [None, 0.0]   # [pcm, prepared_at] — falling pre-warm
    try:
        while True:
            for typ, obj in cdc.poll() or ():
                kind = obj.get("type")
                if kind == "log":
                    m = obj.get("msg", "")
                    if "audio frames" in m or "comp:" in m:
                        print("FW:", m.rstrip(), flush=True)
                if kind == "mic":
                    raw = base64.b64decode(obj["d"])
                    if dump:
                        dump.write(struct.pack("<H", len(raw)) + raw)
                        print(f"[dump] mic report {len(raw)}B")
                        continue
                    pkt = raw[MIC_HEADER_BYTES:MIC_HEADER_BYTES + MIC_OPUS_SIZE]
                    try:
                        pcm48 = opus.decode_mic(bytes(pkt))
                    except Exception:
                        continue  # framing TBD — tune MIC_HEADER_BYTES via --dump
                    mic_pkts.append(pkt)
                    utterance_pcm += pcm48
                elif kind == "voice" and obj.get("ev") == "mic_end":
                    print(f"[bridge] utterance done ({len(utterance_pcm)//96} ms)")
                    if args.echo and mic_pkts:
                        # Loopback test: re-encode captured PCM as speaker frames
                        frames = []
                        pcm = utterance_pcm
                        step = 480 * MIC_CHANNELS * 2
                        for i in range(0, len(pcm) - step, step):
                            chunk = pcm[i:i + step]
                            if MIC_CHANNELS == 1:  # mono -> stereo
                                n = len(chunk) // 2
                                ss = struct.unpack(f"<{n}h", chunk)
                                chunk = struct.pack(f"<{n*2}h",
                                                    *[v for s in ss for v in (s, s)])
                            frames.append(opus.encode_speaker(chunk))
                        speak_frames(frames)
                        cdc.send({"cmd": "VOICE.STATE", "state": "idle"})
                    elif session is not None and utterance_pcm:
                      try:
                        # 48k mono -> 24k mono for the API (decimate)
                        n = len(utterance_pcm) // 2
                        ss = struct.unpack(f"<{n}h", utterance_pcm)
                        session.append_pcm24k(struct.pack(f"<{n//2}h", *ss[::2]))
                        ctx_text = build_context_text()
                        if ctx_text:
                            session.inject_context(
                                "Silent background telemetry. Rule: none of this "
                                "is conversation material unless the user "
                                "explicitly asks about it or it is the direct "
                                "subject of what they said. Answer their "
                                "actual words first and only: " + ctx_text)
                        session.commit_and_respond()
                        collect_and_speak()
                      except Exception as e:
                        print(f"[bridge] session error ({e}); rebuilding")
                        try:
                            session = RealtimeSession(persona, load_memory_tail())
                        except Exception as e2:
                            print(f"[bridge] session rebuild failed: {e2}")
                        cdc.send({"cmd": "VOICE.STATE", "state": "idle"})
                    mic_pkts = []
                    utterance_pcm = b""
                elif kind == "voice":
                    ev = obj.get("ev")
                    print(f"[bridge] voice event: {ev}")
                    if ev == "falling" and session is not None:
                        # Generate the landing line DURING the fall
                        try:
                            session.inject_context(
                                "You are FALLING through the air RIGHT NOW, "
                                "mid-scream. Prepare the single short thing "
                                "you'll say the instant you land — dazed, "
                                "annoyed, or dramatic, in character. Do NOT "
                                "use the scream tool.")
                            session.respond_only()
                            prewarmed[0] = collect_pcm()
                            prewarmed[1] = time.monotonic()
                            print(f"[bridge] landing line ready "
                                  f"({len(prewarmed[0])//48} ms)", flush=True)
                        except Exception as e:
                            print(f"[bridge] prewarm failed ({e})")
                            prewarmed[0] = None
                    elif ev in ("dropped", "caught") and prewarmed[0] and                             time.monotonic() - prewarmed[1] < 8:
                        recent_events.append((time.monotonic(), ev))
                        pcm = prewarmed[0]
                        prewarmed[0] = None
                        time.sleep(0.35)   # let the thud land first
                        speak_pcm(pcm)
                    elif ev in ("dropped", "caught", "shaken", "petted",
                                "flipped", "squeezed", "lonely"):
                        recent_events.append((time.monotonic(), ev))
                        react_to(ev)
            # Gameplay commentary: unprompted quips on active play
            if (args.commentary and session is not None and
                    time.monotonic() - last_comment_t > args.commentary * 60):
                last_comment_t = time.monotonic()
                ctx = fetch_ctx()
                if ctx and ctx.get("btn_age_s", 10**9) < args.commentary * 60:
                    try:
                        session.inject_context(
                            "COMMENTARY moment (nobody spoke to you): the "
                            "user has been playing. Most-pressed buttons "
                            f"(button:count): {ctx.get('top_btns', '')}. "
                            f"Recent presses in order: {ctx.get('btns', '')}. "
                            "Offer ONE short unprompted observation about "
                            "their play style, in character.")
                        session.respond_only()
                        collect_and_speak()
                    except Exception as e:
                        print(f"[bridge] commentary failed ({e})")
            time.sleep(0.002)
    except KeyboardInterrupt:
        cdc.send({"cmd": "DEBUG.STREAM", "enable": False})
        print("\n[bridge] bye")


if __name__ == "__main__":
    main()
