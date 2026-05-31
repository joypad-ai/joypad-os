#!/usr/bin/env python3
"""Capture DualSense HID input-report fixtures.

Usage:
    python3 capture_ds5_fixtures.py <out_dir>

Connects to a Sony DualSense over USB HID, walks the operator through a
list of gestures, and saves one raw report per snapshot as `<name>.bin`
under <out_dir>. Files include the report ID byte so they match the
shape libjoypad expects.

Requires: pip3 install hid
"""

from __future__ import annotations

import os
import sys
import time
import pathlib

try:
    import hid
except ImportError:
    sys.exit("error: missing dependency. Run: pip3 install hid")

DS5_VID = 0x054c
DS5_PIDS = {0x0ce6, 0x0e6f}  # 0x0ce6 = DualSense; Victrix Pro FS uses 0x0209 under 0x0e6f

SNAPSHOTS = [
    ("idle",              "Release everything, sticks centered, touchpad untouched."),
    ("face_cross",        "Hold ONLY the Cross (X) button."),
    ("face_circle",       "Hold ONLY the Circle button."),
    ("face_square",       "Hold ONLY the Square button."),
    ("face_triangle",     "Hold ONLY the Triangle button."),
    ("dpad_north",        "Hold the D-pad UP."),
    ("dpad_south",        "Hold the D-pad DOWN."),
    ("dpad_east",         "Hold the D-pad RIGHT."),
    ("dpad_west",         "Hold the D-pad LEFT."),
    ("dpad_northeast",    "Hold the D-pad UP-RIGHT."),
    ("shoulders_all",     "Hold L1, R1, L2, R2 all the way."),
    ("sticks_full_left",  "Push BOTH sticks fully LEFT."),
    ("sticks_full_up",    "Push BOTH sticks fully UP."),
    ("triggers_full",     "Press both L2 and R2 ALL the way down."),
    ("touchpad_press",    "Press (click) the touchpad."),
    ("touchpad_touch",    "Touch the centre of the touchpad without clicking."),
    ("motion_flat",       "Place the controller flat on the table, untouched."),
    ("battery_state",     "Whatever battery state the controller is in right now."),
]


def find_ds5():
    for d in hid.enumerate(0, 0):
        if d.get("vendor_id") == DS5_VID and (d.get("product_id") in DS5_PIDS or d.get("product_id") == 0x0209):
            return d
    return None


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out_dir = pathlib.Path(sys.argv[1]).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    dev_info = find_ds5()
    if not dev_info:
        sys.exit("error: no Sony DualSense found over USB HID.")

    print(f"Found {dev_info.get('product_string', '?')} at {dev_info.get('path', b'').decode(errors='replace')}")

    h = hid.device()
    h.open_path(dev_info["path"])
    h.set_nonblocking(0)

    print("\nWill walk you through {} captures. Press Enter to record each one.\n".format(len(SNAPSHOTS)))

    for name, prompt in SNAPSHOTS:
        input(f"  [{name}] {prompt}\n  Hold the state, then press Enter to capture: ")
        # Drain any in-flight reports so we don't capture a stale one.
        h.set_nonblocking(1)
        for _ in range(8):
            h.read(64, timeout_ms=5)
        # Wait a beat for the requested state to settle.
        time.sleep(0.1)
        h.set_nonblocking(0)
        data = h.read(64, timeout_ms=500)
        if not data:
            print("    (no report — skipping)")
            continue
        # hidapi-python strips the report ID for OSes that prepend it;
        # libjoypad expects the report ID byte present, so add 0x01 back.
        if data[0] not in (0x01, 0x31):
            data = bytes([0x01]) + bytes(data)
        else:
            data = bytes(data)
        path = out_dir / f"{name}.bin"
        path.write_bytes(data)
        print(f"    wrote {path.name} ({len(data)} bytes)")

    h.close()
    print(f"\nDone. {len(SNAPSHOTS)} snapshots written to {out_dir}.")


if __name__ == "__main__":
    main()
