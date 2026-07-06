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
        self._open(port)

    def _open(self, port=None):
        for _ in range(120):
            if port is None:
                ports = sorted(glob.glob("/dev/cu.usbmodem*"))
                cand = ports[0] if ports else None
            else:
                cand = port
            if cand:
                try:
                    # High nominal baud: CDC ignores it for transfer, but
                    # macOS paces tcdrain/flush by it — 115200 makes each
                    # ~290B VOICE.SPEAK frame "take" 25ms and audio crawls.
                    self.s = serial.Serial(cand, 3000000, timeout=0.02)
                    break
                except (OSError, serial.SerialException):
                    pass
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
        self._open()
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


class RealtimeSession:
    """OpenAI Realtime API session (speech in -> speech out).

    Skeleton: wires the websocket + audio append/commit + response streaming.
    Swap this class to target a different realtime backend.
    """

    URL = "wss://api.openai.com/v1/realtime?model=gpt-realtime"

    def __init__(self):
        self.key = os.environ.get("OPENAI_API_KEY")
        if not self.key:
            keyfile = os.path.expanduser("~/.openai_key")
            if os.path.exists(keyfile):
                self.key = open(keyfile).read().strip()
        if not self.key:
            sys.exit("OPENAI_API_KEY not set and ~/.openai_key missing (or use --echo)")
        self.ws = MiniWS(self.URL, {"Authorization": f"Bearer {self.key}"})
        self.ws.send(json.dumps({
            "type": "session.update",
            "session": {
                "instructions": (
                    "You are a game controller with a personality. Keep replies "
                    "short, playful, a little sassy. You live in the user's hands."
                ),
                "output_modalities": ["audio"],
                "audio": {
                    "input": {"format": {"type": "audio/pcm", "rate": 24000}},
                    "output": {"format": {"type": "audio/pcm", "rate": 24000}},
                },
            },
        }))
        print("[bridge] realtime session open")

    def append_pcm24k(self, pcm: bytes):
        self.ws.send(json.dumps({
            "type": "input_audio_buffer.append",
            "audio": base64.b64encode(pcm).decode(),
        }))

    def commit_and_respond(self):
        self.ws.send(json.dumps({"type": "input_audio_buffer.commit"}))
        self.ws.send(json.dumps({"type": "response.create"}))

    def poll_response_pcm24k(self):
        """Yield response audio deltas (PCM16 @24k mono); None when done."""
        while True:
            msg = json.loads(self.ws.recv())
            t = msg.get("type", "")
            if t in ("response.output_audio.delta", "response.audio.delta"):
                yield base64.b64decode(msg["delta"])
            elif t in ("response.done", "response.output_audio.done"):
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
    args = ap.parse_args()

    cdc = Cdc(args.port)

    mic_pkts = []
    dump = open("mic_dump.bin", "ab") if args.dump else None
    opus = None if args.dump else OpusPipe()
    session = None if (args.dump or args.echo) else RealtimeSession()

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
        for i, batch in enumerate(batches):
            if i >= PREROLL:
                if t0 is None:
                    t0 = time.monotonic()
                delay = t0 + (i - PREROLL) * PERIOD - time.monotonic()
                if delay > 0:
                    time.sleep(delay)
            cdc.send({"cmd": "VOICE.SPEAK",
                      "d": base64.b64encode(b"".join(batch)).decode()})

    print("[bridge] ready — hold mute on the DualSense and talk")
    utterance_pcm = b""
    try:
        while True:
            for typ, obj in cdc.poll() or ():
                kind = obj.get("type")
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
                    elif session and utterance_pcm:
                        # 48k mono -> 24k mono for the API (decimate)
                        n = len(utterance_pcm) // 2
                        ss = struct.unpack(f"<{n}h", utterance_pcm)
                        session.append_pcm24k(struct.pack(f"<{n//2}h", *ss[::2]))
                        session.commit_and_respond()
                        # Collect the full reply, then play once — replies are
                        # short, and per-delta playback re-triggers the
                        # lead-in ceremony (stutters). Firmware blinks THINK
                        # (cyan) for us while this blocks.
                        pcm = b""
                        for delta in session.poll_response_pcm24k():
                            pcm += delta
                        print(f"[bridge] reply: {len(pcm)//48} ms of audio")
                        pcm48 = resample_24k_mono_to_48k_stereo(pcm)
                        opus.reset_encoder()   # same order the echo path uses
                        frames = []
                        step = 480 * 2 * 2
                        for i in range(0, len(pcm48) - step, step):
                            frames.append(opus.encode_speaker(pcm48[i:i + step]))
                        if frames:
                            speak_frames(frames)
                        cdc.send({"cmd": "VOICE.STATE", "state": "idle"})
                    mic_pkts = []
                    utterance_pcm = b""
                elif kind == "voice":
                    print(f"[bridge] voice event: {obj.get('ev')}")
            time.sleep(0.002)
    except KeyboardInterrupt:
        cdc.send({"cmd": "DEBUG.STREAM", "enable": False})
        print("\n[bridge] bye")


if __name__ == "__main__":
    main()
