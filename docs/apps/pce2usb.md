# pce2usb

PCEngine / TurboGrafx-16 controller to USB HID gamepad.

## Overview

Reads a native PCEngine controller (2-button standard pad, 6-button Avenue Pad 6, or up to 5 players via a multitap) by bit-banging the controller's multiplexer, and outputs as a USB HID gamepad. ~60Hz polling, no PIO required. The same read routine handles a direct pad and a multitap; multitap ports are merged into the single USB gamepad.

## Input

[PCEngine Input](../input/pcengine.md) -- 4-bit multiplexed parallel bus, GPIO bit-bang. SEL (GPIO 5), CLR (GPIO 6), D0-D3 (GPIO 8-11).

## Output

[USB Device Output](../output/usb-device.md) -- USB HID gamepad with multiple emulation modes.

## Core Configuration

| Setting | Value |
|---------|-------|
| Routing mode | MERGE (all ports → 1 gamepad) |
| Player slots | 5 (fixed; multitap) |
| Polling rate | ~60Hz |
| Profile system | None |

## Key Features

- **Multitap** -- Up to 5 players read each scan and merged into the single USB gamepad (GC-adapter 4-distinct-player output is a planned follow-up).
- **Auto-detection** -- Activity-based per-port presence; status LED idles when nothing is connected.
- **6-button support** -- Avenue Pad 6 III-VI read best-effort, signature-gated so 2-button pads are unaffected.
- **USB output modes** -- SInput, XInput, PS3, PS4, Switch, Keyboard/Mouse. Double-click button to cycle, triple-click to reset.
- **Web config** -- [config.joypad.ai](https://config.joypad.ai).

## Supported Boards

| Board | Build Command | Status LED |
|-------|---------------|------------|
| KB2040 | `make pce2usb_kb2040` | NeoPixel |
| Pico | `make pce2usb_pico` | GP25 |
| Pico W | `make pce2usb_pico_w` | CYW43 onboard LED |

!!! note "Pico vs Pico W builds"
    The `pico` build runs on Pico W hardware too, but its onboard LED won't light (the Pico W LED is on the CYW43 chip, not GP25). Use the `pico_w` build for the status LED. The `pico_w` build will **not** boot on a plain Pico.

## Build and Flash

```bash
make pce2usb_pico_w
make flash-pce2usb_pico_w
```

## Troubleshooting

**Controller not detected (LED keeps blinking):**
- Check the controller port wiring, especially GND and the 3.3V supply.
- Confirm VCC is wired to **3V3**, not 5V (RP2040 GPIO is not 5V-tolerant).
- Verify SEL, CLR, and D0-D3 pin assignments match your board.

**No response from buttons:**
- Verify SEL and CLR are not swapped.
- Check D0-D3 continuity from the controller port to GPIO 8-11.
- Try a different controller to rule out a faulty pad.

**Wrong or scrambled buttons:**
- D0-D3 order may be reversed -- re-order the data wires or remap in `app.h`.
- Use the [config.joypad.ai](https://config.joypad.ai) input monitor to view raw input.

**6-button buttons (III-VI) don't register:**
- 6-button support is best-effort and unvalidated on real hardware; 2-button function is unaffected.

**USB not recognized by host:**
- Double-click the board button to cycle USB output mode.
- Triple-click to reset to SInput (default HID mode).
- Try a different USB cable or port.
