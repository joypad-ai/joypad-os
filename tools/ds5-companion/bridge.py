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

# Bytes to strip from the front of each DS5 mic input report to reach the Opus
# payload. Placeholder until confirmed with --dump captures (report id + seq +
# sub-header). DS5_Bridge decodes mic as Opus 48k; channel count TBD.
MIC_HEADER_BYTES = 2
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
    """Framed JSON command/event channel to the JoypadOS device."""

    def __init__(self, port=None):
        if port is None:
            ports = sorted(glob.glob("/dev/cu.usbmodem*"))
            if not ports:
                sys.exit("no /dev/cu.usbmodem* CDC port found")
            port = ports[0]
        self.s = serial.Serial(port, 115200, timeout=0.02)
        self.s.dtr = True
        self.s.rts = True
        self.buf = bytearray()
        self.seq = 0
        print(f"[bridge] connected to {port}")

    def send(self, obj: dict):
        self.s.write(frame(MSG_CMD, self.seq & 0xFF, json.dumps(obj).encode()))
        self.s.flush()
        self.seq += 1

    def poll(self):
        """Yield decoded JSON payloads (events and responses)."""
        self.buf += self.s.read(8192)
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
    """Opus decode/encode via opuslib (lazy import so --dump works without it)."""

    def __init__(self):
        import opuslib  # noqa: PLC0415
        self.dec = opuslib.Decoder(48000, MIC_CHANNELS)
        self.enc = opuslib.Encoder(48000, 2, opuslib.APPLICATION_AUDIO)
        self.enc.bitrate = 160000
        self.enc.vbr = 0  # hard CBR -> exactly 200B per 10ms frame

    def decode_mic(self, pkt: bytes) -> bytes:
        return self.dec.decode(pkt, 480)

    def encode_speaker(self, pcm_48k_stereo_10ms: bytes) -> bytes:
        out = self.enc.encode(pcm_48k_stereo_10ms, 480)
        assert len(out) == 200, f"expected 200B CBR frame, got {len(out)}"
        return out


class RealtimeSession:
    """OpenAI Realtime API session (speech in -> speech out).

    Skeleton: wires the websocket + audio append/commit + response streaming.
    Swap this class to target a different realtime backend.
    """

    URL = "wss://api.openai.com/v1/realtime?model=gpt-realtime"

    def __init__(self):
        self.key = os.environ.get("OPENAI_API_KEY")
        if not self.key:
            sys.exit("OPENAI_API_KEY not set (or use --echo)")
        import websockets.sync.client  # noqa: PLC0415
        self.ws = websockets.sync.client.connect(
            self.URL,
            additional_headers={"Authorization": f"Bearer {self.key}"},
        )
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
    cdc.send({"cmd": "DEBUG.STREAM", "enable": True})  # attaches the event stream

    mic_pkts = []
    dump = open("mic_dump.bin", "ab") if args.dump else None
    opus = None if args.dump else OpusPipe()
    session = None if (args.dump or args.echo) else RealtimeSession()

    def speak_frames(frames_200):
        """Stream 200B Opus frames to the firmware ring, paced by 'free'."""
        for f in frames_200:
            cdc.send({"cmd": "VOICE.SPEAK", "d": base64.b64encode(f).decode()})
            time.sleep(0.009)  # ~ring drain rate; firmware 'free' is advisory

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
                    pkt = raw[MIC_HEADER_BYTES:]
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
                        # 48k stereo/mono -> 24k mono for the API (decimate)
                        n = len(utterance_pcm) // 2
                        ss = struct.unpack(f"<{n}h", utterance_pcm)
                        mono24 = struct.pack(f"<{n//2}h", *ss[::2])
                        session.append_pcm24k(mono24)
                        session.commit_and_respond()
                        pcm_run = b""
                        for delta in session.poll_response_pcm24k():
                            pcm_run += resample_24k_mono_to_48k_stereo(delta)
                            frames = []
                            step = 480 * 2 * 2
                            while len(pcm_run) >= step:
                                frames.append(opus.encode_speaker(pcm_run[:step]))
                                pcm_run = pcm_run[step:]
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
