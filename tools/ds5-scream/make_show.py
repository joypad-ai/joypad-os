#!/usr/bin/env python3
"""Compose the 4th-of-July Grand Finale show for the DS5 fireworks mode.

Renders assets/fwshow.wav (~30s): a chiptune Star-Spangled Banner (public
domain, synthesized here) under a choreographed sequence of our firework SFX,
ending on a crackle-wall finale. Because the mix is composed programmatically,
every burst time/color is known — the script also emits the C event table
(ds5_show_events.h) that drives the lightbar slams and haptic hits in sync.

Run this, then encode.py, whenever the choreography or assets change.
"""

import array
import math
import struct
import subprocess
import os
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "assets")
SR = 48000

# Raw burst offsets (seconds) inside each firework asset, from the encoder's
# detected fx frames (comp frames * 10ms / (15/16) pitch factor).
FW = {
    1: ("firework_1.wav", 190 * 0.010 / 0.9375),
    2: ("firework_2.wav", 150 * 0.010 / 0.9375),
    3: ("firework_3.wav", 160 * 0.010 / 0.9375),
    4: ("firework_4.wav", 240 * 0.010 / 0.9375),  # second (bigger) pop
}

RED, WHITE, BLUE, GOLD = (255, 30, 30), (255, 255, 255), (60, 90, 255), (255, 150, 20)


def load_mono(path):
    pcm = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", path, "-ac", "1", "-ar", str(SR),
         "-f", "s16le", "-"], capture_output=True, check=True).stdout
    a = array.array("h")
    a.frombytes(pcm[: len(pcm) // 2 * 2])
    return [s / 32768.0 for s in a]


def note(track, t, freq, dur, vol=0.16):
    """Chip-flavored note: square+triangle blend, soft attack/decay, light vibrato."""
    n0 = int(t * SR)
    n = int(dur * SR)
    for i in range(n):
        tt = i / SR
        f = freq * (1.0 + 0.004 * math.sin(2 * math.pi * 5.5 * tt))
        ph = 2 * math.pi * f * tt
        sq = 1.0 if math.sin(ph) >= 0 else -1.0
        tri = 2.0 / math.pi * math.asin(math.sin(ph))
        env = min(1.0, tt / 0.02) * min(1.0, max(0.0, (dur - tt) / 0.12))
        v = vol * env * (0.45 * sq + 0.55 * tri)
        j = n0 + i
        if j < len(track):
            track[j] += v
        # quiet octave-down root for body
        phb = 2 * math.pi * (f / 2) * tt
        if j < len(track):
            track[j] += vol * 0.35 * env * math.sin(phb)


# Star-Spangled Banner, C major (public domain, 1814). q = quarter note.
N = {"C4": 261.6, "D4": 293.7, "E4": 329.6, "F4": 349.2, "Fs4": 370.0,
     "G4": 392.0, "A4": 440.0, "B4": 493.9, "C5": 523.3, "D5": 587.3,
     "E5": 659.3, "F5": 698.5, "G5": 784.0}

def anthem(track, t0, q=0.42):
    seq = [
        # "O say can you see, by the dawn's early light"
        ("G4", 0.75), ("E4", 0.25), ("C4", 1.0), ("E4", 1.0), ("G4", 1.0),
        ("C5", 2.0),
        ("E5", 0.75), ("D5", 0.25), ("C5", 1.0), ("E4", 1.0), ("Fs4", 1.0),
        ("G4", 2.0),
        # "O'er the land of the free..."
        ("C5", 1.0), ("C5", 1.0), ("C5", 1.0), ("B4", 1.5), ("A4", 0.5),
        ("G4", 1.0),
        ("F5", 1.0), ("E5", 1.0), ("D5", 1.5), ("E5", 3.2),   # "freeee"
        # "...and the home of the brave"
        ("C5", 0.75), ("C5", 0.25), ("G4", 1.0), ("E4", 1.0), ("C4", 2.6),
    ]
    t = t0
    for name, beats in seq:
        note(track, t, N[name], beats * q * 0.96)
        t += beats * q
    return t  # end time


def main():
    # --- timeline ---------------------------------------------------------
    # (time, fw_variant, color, big)
    shots = [
        (0.30, 1, RED,   True),    # opening: single big red (own launch+climb)
        (4.20, 3, WHITE, False),   # singles landing between anthem phrases
        (7.20, 3, BLUE,  False),
        (9.80, 2, RED,   False),
        (12.40, 3, WHITE, False),
        (15.00, 4, BLUE,  True),   # double burst under the "free" climax
        (17.20, 1, GOLD,  True),   # gold heavy into the finale
        (20.80, 3, RED,   False),  # finale rapid-fire over the crackle wall
        (21.60, 3, WHITE, False),
        (22.40, 3, BLUE,  False),
        (23.20, 2, WHITE, True),
        (24.40, 4, GOLD,  True),
    ]
    CRACKLE_AT = 20.2
    ANTHEM_AT = 3.4
    total_s = 30.0

    track = [0.0] * int(total_s * SR)

    end = anthem(track, ANTHEM_AT)
    print(f"anthem: {ANTHEM_AT:.1f}s .. {end:.1f}s")

    srcs = {k: load_mono(os.path.join(ASSETS, f)) for k, (f, _) in FW.items()}
    crackle = load_mono(os.path.join(ASSETS, "show_crackle.wav"))

    def mix(samples, t, gain):
        n0 = int(t * SR)
        for i, s in enumerate(samples):
            j = n0 + i
            if 0 <= j < len(track):
                track[j] += s * gain

    for t, var, _c, big in shots:
        mix(srcs[var], t, 0.85 if big else 0.62)
    mix(crackle, CRACKLE_AT, 0.75)

    peak = max(abs(v) for v in track) or 1.0
    g = 0.92 / peak
    out = array.array("h", (int(max(-1.0, min(1.0, v * g)) * 32767) for v in track))

    with wave.open(os.path.join(ASSETS, "fwshow.wav"), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(out.tobytes())
    print(f"fwshow.wav written ({total_s:.0f}s)")

    # --- event table (comp frames: raw_s * 100 * 15/16 + 2 lead-in) --------
    events = []
    for t, var, color, big in shots:
        burst_t = t + FW[var][1]
        frame = int(burst_t * 100 * 0.9375) + 2
        events.append((frame, color, big))
    events.sort(key=lambda e: e[0])

    hdr = os.path.join(HERE, "..", "..", "src", "bt", "bthid", "devices",
                       "vendors", "sony", "ds5_show_events.h")
    with open(os.path.abspath(hdr), "w") as f:
        f.write("// GENERATED by tools/ds5-scream/make_show.py — do not edit\n")
        f.write("// Burst choreography for the Grand Finale show (fwshow.wav)\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")
        f.write("typedef struct {\n")
        f.write("    uint16_t frame;      // 10ms content frame of the burst\n")
        f.write("    uint8_t r, g, b;\n")
        f.write("    uint8_t big;         // heavier haptic + longer fade\n")
        f.write("} ds5_show_event_t;\n\n")
        f.write("static const ds5_show_event_t ds5_show_events[] = {\n")
        for frame, (r, gg, b), big in events:
            f.write(f"    {{ {frame}, {r}, {gg}, {b}, {1 if big else 0} }},\n")
        f.write("};\n")
        f.write(f"#define DS5_SHOW_EVENT_COUNT {len(events)}\n")
    print(f"ds5_show_events.h written ({len(events)} bursts)")


if __name__ == "__main__":
    main()
