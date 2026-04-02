# n642usb

N64 controller to USB HID gamepad.

## Overview

Reads a native N64 controller via the joybus single-wire protocol and outputs as a USB HID gamepad. Full analog stick support with two button mapping profiles. Detects and supports N64 rumble pak for vibration feedback.

## Input

[N64 Input](../input/n64.md) -- Joybus PIO protocol on GPIO 29.

## Output

[USB Device Output](../output/usb-device.md) -- USB HID gamepad with multiple emulation modes.

## Core Configuration

| Setting | Value |
|---------|-------|
| Routing mode | SIMPLE (1:1) |
| Player slots | 1 (fixed) |
| Data pin | GPIO 29 |
| Profile system | 2 profiles |

## Profiles

| Profile | Description |
|---------|-------------|
| **Default** | A=B1, C-Down=B2, B=B3, C-Left=B4. C-buttons map to face buttons. |
| **Dual Stick** | C-buttons map to right analog stick instead of face buttons. |

### Default Profile Button Mapping

| N64 Button | USB Output |
|------------|------------|
| A | B1 |
| C-Down | B2 |
| B | B3 |
| C-Left | B4 |
| L | L1 |
| R | R1 |
| Z | L2 |
| C-Up | L3 |
| C-Right | R3 |
| Start | S2 |
| D-Pad | D-Pad |
| Stick | Left Analog |

## Key Features

- **Full analog** -- N64 stick mapped to left analog with proper range scaling.
- **Rumble pak** -- Detected and used for vibration feedback from USB host.
- **USB output modes** -- SInput, XInput, PS3, PS4, Switch, Keyboard/Mouse.
- **Web config** -- [config.joypad.ai](https://config.joypad.ai).

## Supported Boards

| Board | Build Command |
|-------|---------------|
| KB2040 | `make n642usb_kb2040` |

## Build and Flash

```bash
make n642usb_kb2040
make flash-n642usb_kb2040
```
