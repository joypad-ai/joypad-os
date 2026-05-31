# psx2usb

PlayStation 1 / PlayStation 2 controller to USB HID gamepad.

## Overview

Reads a native PSX/PS2 controller directly via the SIO bus and outputs as a USB HID gamepad. Auto-detects the connected controller and decodes every standard PSX peripheral. Includes USB output mode switching and web configuration.

## Input

[PSX Input](../input/psx.md) -- Hardware-paced PIO+DMA SIO transport at 500 kHz with active pull-up on DAT, so older analog pads (e.g. Sony SCPH-110) read cleanly at the fast clock.

## Output

[USB Device Output](../output/usb-device.md) -- USB HID gamepad with multiple emulation modes.

## Core Configuration

| Setting | Value |
|---------|-------|
| Routing mode | SIMPLE (1:1) |
| Player slots | 1 (fixed) |
| Profile system | None |
| Mouse support | PlayStation Mouse (SCPH-1090) |

## Key Features

- **Auto-detect** -- Digital pad, DualShock, DualShock 2 (analog button pressure), neGcon, Dual Analog flightstick (SCPH-1110), GunCon, JogCon, and PlayStation Mouse identified at runtime.
- **DualShock 2 pressure** -- 12-byte per-button pressure passed through to PS3 output mode.
- **GunCon** -- Light-gun aim mapped to right stick (works against a CRT).
- **JogCon** -- Paddle wheel mapped to left-stick X with experimental recenter force-feedback.
- **Rumble** -- DualShock motor bytes driven from the output's feedback state.
- **ANALOG button -> A1/Guide** -- detected via the analog<->digital mode toggle and held briefly so a console PS-button init registers.
- **Board user button -> A1/Guide** -- BOOTSEL (QT Py / KB2040) or the board's user GPIO emits A1 while held. Double-click still cycles USB output mode, triple-click resets to SInput.
- **USB output modes** -- SInput, XInput, PS3, PS4, Switch, Keyboard/Mouse.
- **Web config** -- [config.joypad.ai](https://config.joypad.ai) for mode switching and monitoring.

## Supported Boards

| Board | Build Command |
|-------|---------------|
| Adafruit QT Py RP2040 | `make psx2usb_qtpy` |
| Adafruit KB2040 | `make psx2usb_kb2040` |
| Raspberry Pi Pico | `make psx2usb_pico` |

## Build and Flash

```bash
make psx2usb_qtpy
make flash-psx2usb_qtpy
```

See the [PSX2USB build guide](../hardware/builds/psx2usb-qtpy.md) for the connector pinout, per-board wiring tables, and the pull-up + rumble notes.
