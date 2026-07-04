#!/usr/bin/env python3
"""DualSense speaker-audio asset encoder (drop-scream content feature).

Encodes WAV/AIFF clips into the exact Opus frame format the DualSense
expects inside Bluetooth output report 0x36:

    48 kHz stereo -> libopus, 160 kbps hard-CBR, 10 ms frames
    -> exactly 200 bytes per packet

Each clip gets ~20 ms of leading silence (2 frames) so the controller's
Opus decoder settles before audible content — this masks the decoder-state
discontinuity when the firmware splices between clips mid-stream.

Usage:
    python3 tools/ds5-scream/encode.py

Reads from tools/ds5-scream/assets/:
    scream.*    -> ds5_clip_scream       (the mid-air scream, sustained)
    impact_*.*  -> ds5_clips_impact[]    (hard-landing quips)
    catch_*.*   -> ds5_clips_catch[]     (caught-mid-air quips)

Writes src/bt/bthid/devices/vendors/sony/ds5_voice_assets.h
"""

import glob
import os
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ASSETS = os.path.join(REPO, "tools", "ds5-scream", "assets")
OUT = os.path.join(REPO, "src", "bt", "bthid", "devices", "vendors", "sony",
                   "ds5_voice_assets.h")

FRAME_BYTES = 200  # 160 kbps CBR x 10 ms


def parse_ogg_packets(data: bytes):
    """Minimal Ogg parser: return the list of logical packets."""
    packets = []
    pending = b""
    pos = 0
    while pos < len(data):
        assert data[pos:pos + 4] == b"OggS", f"bad Ogg page at {pos}"
        seg_count = data[pos + 26]
        lacing = data[pos + 27:pos + 27 + seg_count]
        body = pos + 27 + seg_count
        for lace in lacing:
            pending += data[body:body + lace]
            body += lace
            if lace < 255:
                packets.append(pending)
                pending = b""
        pos = body
    return packets


def encode_clip(path: str):
    """Encode one audio file -> list of 200-byte Opus packets."""
    tmp = path + ".tmp.ogg"
    subprocess.run(
        ["ffmpeg", "-y", "-loglevel", "error", "-i", path,
         # asetrate=51200,aresample=48000: pre-speed content by 16/15 — the
         # DualSense consumes one packet per 10.667ms but each Opus frame is
         # 10ms, so playback runs 16/15 slow; this exactly compensates
         # (mirrors DS5_Bridge resampler_audio.SetRates(51200, 48000)).
         # adelay: ~2 frames lead-in silence (decoder settle on splice).
         # aresample=48000 FIRST: sources arrive at native rate (say = 22kHz),
         # and asetrate relabels whatever rate it's given — must be true 48k.
         "-af", "aresample=48000,asetrate=51200,aresample=48000,loudnorm=I=-14:TP=-1.5,adelay=20:all=1",
         "-ar", "48000", "-ac", "2",
         "-c:a", "libopus", "-b:a", "160k", "-vbr", "off",
         "-frame_duration", "10", "-application", "audio",
         "-f", "ogg", tmp],
        check=True,
    )
    with open(tmp, "rb") as f:
        packets = parse_ogg_packets(f.read())
    os.remove(tmp)

    assert packets[0][:8] == b"OpusHead", "expected OpusHead first"
    audio = packets[2:]  # skip OpusHead + OpusTags
    bad = [len(p) for p in audio if len(p) != FRAME_BYTES]
    assert not bad, f"{path}: non-{FRAME_BYTES}B packets {bad[:5]} — hard CBR required"
    assert audio, f"{path}: no audio packets"
    return audio


def detect_fx_frame(path: str):
    """Find the loudest 100ms window (the burst) in the clip as encoded —
    same filter chain as encode_clip, so the frame index matches playback."""
    import array as arr
    import math
    # NO loudnorm here: its dynamic gain pumps quiet launches up to burst
    # level and the detector then mistakes the launch for the burst.
    pcm = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path,
         "-af", "aresample=48000,asetrate=51200,aresample=48000,adelay=20:all=1",
         "-ac", "1", "-ar", "8000", "-f", "s16le", "-"],
        capture_output=True, check=True).stdout
    a = arr.array('h')
    a.frombytes(pcm[:len(pcm) // 2 * 2])
    win = 800  # 100ms at 8k
    rms = []
    for i in range(0, len(a) - win, win):
        chunk = a[i:i + win:4]
        rms.append(math.fsum(x * x for x in chunk))
    if not rms:
        return 0
    peak = max(rms)
    # Launches and bursts are both bass thumps; what differs is the tail —
    # a burst trails ~1s of crackle, a launch trails ascent silence. Among
    # loud candidates, pick the one with the most energy in the next second.
    best_i, best_tail = 0, -1.0
    for i, v in enumerate(rms):
        if v >= peak * 0.70:
            tail = math.fsum(rms[i:i + 10])
            if tail > best_tail:
                best_tail, best_i = tail, i
    return best_i * 10  # 100ms windows -> 10ms frames


def emit_clip(f, cname: str, frames):
    f.write(f"static const uint8_t {cname}_frames[][{FRAME_BYTES}] = {{\n")
    for pkt in frames:
        f.write("    {" + ",".join(str(b) for b in pkt) + "},\n")
    f.write("};\n")


def main():
    def find(pattern):
        hits = []
        for ext in ("wav", "aiff", "aif", "mp3", "m4a", "flac"):
            hits += glob.glob(os.path.join(ASSETS, f"{pattern}.{ext}"))
        return sorted(hits)

    screams = find("scream")
    impacts = find("impact_*")
    catches = find("catch_*")
    fireworks = find("firework_*")
    show = find("fwshow")
    fisher_nums = find("fisher_num_*")
    fisher_abcs = find("fisher_abc_*")
    if not screams or not impacts or not catches:
        sys.exit(f"missing assets in {ASSETS}: need scream.*, impact_*.*, catch_*.* "
                 f"(run make_placeholders.sh for synthesized stand-ins)")

    clips = [("ds5_vc_scream", screams[0])]
    clips += [(f"ds5_vc_impact{i}", p) for i, p in enumerate(impacts)]
    clips += [(f"ds5_vc_catch{i}", p) for i, p in enumerate(catches)]
    clips += [(f"ds5_vc_firework{i}", p) for i, p in enumerate(fireworks)]
    if show:
        clips.append(("ds5_vc_show", show[0]))
    clips += [(f"ds5_vc_fnum{i}", p) for i, p in enumerate(fisher_nums)]
    clips += [(f"ds5_vc_fabc{i}", p) for i, p in enumerate(fisher_abcs)]
    # Synthetic silence clip (companion mode mic-keepalive stream)
    silence_tmp = "/tmp/ds5_silence_gen.wav"
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-f", "lavfi",
                    "-i", "anullsrc=r=48000:cl=stereo", "-t", "0.5", silence_tmp],
                   check=True)
    clips.append(("ds5_vc_silence", silence_tmp))

    with open(OUT, "w") as f:
        f.write("// ds5_voice_assets.h - GENERATED by tools/ds5-scream/encode.py — do not edit\n")
        f.write("// Opus frames for DualSense BT speaker audio (report 0x36):\n")
        f.write(f"// 48kHz stereo, 160kbps hard-CBR, 10ms frames, {FRAME_BYTES}B/frame\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("typedef struct {\n")
        f.write(f"    const uint8_t (*frames)[{FRAME_BYTES}];\n")
        f.write("    uint16_t frame_count;\n")
        f.write("    uint16_t fx_frame;   // burst/impact frame for LED sync (0 = none)\n")
        f.write("} ds5_voice_clip_t;\n\n")

        total = 0
        encoded = {}
        for cname, path in clips:
            frames = encode_clip(path)
            fx = detect_fx_frame(path) if "firework" in cname else 0
            encoded[cname] = (len(frames), os.path.basename(path))
            total += len(frames) * FRAME_BYTES
            f.write(f"// {os.path.basename(path)}: {len(frames)} frames "
                    f"({len(frames) * 10} ms), fx@{fx}\n")
            emit_clip(f, cname, frames)
            f.write(f"static const ds5_voice_clip_t {cname} = "
                    f"{{ {cname}_frames, {len(frames)}, {fx} }};\n\n")

        f.write("static const ds5_voice_clip_t* const ds5_clips_impact[] = {\n")
        for cname, _ in clips:
            if cname.startswith("ds5_vc_impact"):
                f.write(f"    &{cname},\n")
        f.write("};\n")
        f.write("static const ds5_voice_clip_t* const ds5_clips_catch[] = {\n")
        for cname, _ in clips:
            if cname.startswith("ds5_vc_catch"):
                f.write(f"    &{cname},\n")
        f.write("};\n\n")
        f.write("#define DS5_CLIPS_IMPACT_COUNT "
                f"{sum(1 for c, _ in clips if c.startswith('ds5_vc_impact'))}\n")
        f.write("#define DS5_CLIPS_CATCH_COUNT "
                f"{sum(1 for c, _ in clips if c.startswith('ds5_vc_catch'))}\n")
        f.write("static const ds5_voice_clip_t* const ds5_clips_firework[] = {\n")
        for cname, _ in clips:
            if cname.startswith("ds5_vc_firework"):
                f.write(f"    &{cname},\n")
        f.write("};\n")
        f.write("#define DS5_CLIPS_FIREWORK_COUNT "
                f"{sum(1 for c, _ in clips if c.startswith('ds5_vc_firework'))}\n")
        for group, prefix in (("fisher_num", "ds5_vc_fnum"), ("fisher_abc", "ds5_vc_fabc")):
            names = [c for c, _ in clips if c.startswith(prefix)]
            if names:
                f.write(f"static const ds5_voice_clip_t* const ds5_clips_{group}[] = {{\n")
                for cname in names:
                    f.write(f"    &{cname},\n")
                f.write("};\n")
                f.write(f"#define DS5_CLIPS_{group.upper()}_COUNT {len(names)}\n")
        f.write("#define DS5_CLIP_SCREAM ds5_vc_scream\n")
        f.write("#define DS5_CLIP_SILENCE ds5_vc_silence\n")
        if show:
            f.write("#define DS5_CLIP_SHOW ds5_vc_show\n")

    print(f"wrote {OUT}")
    for cname, (n, base) in encoded.items():
        print(f"  {cname:20s} {n:4d} frames  {n * 10 / 1000.0:5.2f}s  ({base})")
    print(f"  total flash: {total / 1024.0:.1f} KB")


if __name__ == "__main__":
    main()
