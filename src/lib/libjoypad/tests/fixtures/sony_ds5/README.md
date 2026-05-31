# Sony DualSense (DS5) USB fixtures

Captured raw USB HID input reports from a physical Sony DualSense. Each
`.bin` is one full HID report including the report ID byte. Replayed by
`tests/replay/ds5_fixture_replay.c` to verify the libjoypad parser against
real hardware bytes (not just hand-crafted reports).

## Capture procedure (host side, macOS / Linux)

Use the helper script:

```bash
python3 tools/capture_ds5_fixtures.py captured/
```

It enumerates HID devices, finds the DualSense (VID 0x054c / PID 0x0ce6),
asks you to hold each gesture (face buttons, sticks fully deflected,
touchpad pressed, etc.), and writes one `.bin` per snapshot to the path
given.

Requires `hidapi` Python bindings:

```bash
pip3 install hid
```

Alternative — Comrade UI:

1. Open Comrade, connect to the DualSense via HID
2. Use the log capture to record raw input bytes for each gesture
3. Save each gesture as `<state>.bin` under `captured/`

## Existing fixtures

Filled in as captures land. Expected eventual set:

| File                     | What                                  |
|--------------------------|---------------------------------------|
| `idle.bin`               | All buttons released, sticks centered |
| `face_cross.bin`         | Cross only                            |
| `face_circle.bin`        | Circle only                           |
| `face_square.bin`        | Square only                           |
| `face_triangle.bin`      | Triangle only                         |
| `dpad_north.bin`         | D-pad up                              |
| `dpad_south.bin`         | D-pad down                            |
| `dpad_northeast.bin`     | D-pad up + right                      |
| `sticks_full_left.bin`   | Both sticks pushed full left          |
| `sticks_full_up.bin`     | Both sticks pushed full up            |
| `triggers_full.bin`      | L2 + R2 fully pressed                 |
| `touchpad_press.bin`     | Touchpad clicked                      |
| `touchpad_swipe_*.bin`   | Single-finger touch at various coords |
| `motion_flat.bin`        | Controller lying flat on table        |
| `battery_charging.bin`   | Plugged in, partial battery           |
| `battery_full.bin`       | Plugged in, full battery              |

## Why fixtures?

Hand-crafted byte arrays in `tests/replay/ds5_input.c` exercise the parser
logic but encode my understanding of the report struct, which may diverge
from reality (endianness, undocumented bit ordering, vendor quirks).
Real-hardware fixtures pin behavior to actual bytes — if the parser starts
producing different `input_event_t` for a captured snapshot, the change
is observable in CI rather than at deploy time.
