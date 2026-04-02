# neogeo2usb

Neo Geo controller/arcade stick to USB HID gamepad.

## Overview

Reads a native Neo Geo arcade stick or controller via GPIO (active-low buttons with internal pull-ups) and outputs as a USB HID gamepad. Supports 4-6 button sticks via DB15 connector. Includes D-pad mode hotkeys for mapping the joystick to different analog axes.

## Input

[Neo Geo Input](../input/neogeo.md) -- GPIO polling on DB15 connector (active-low with internal pull-ups).

## Output

[USB Device Output](../output/usb-device.md) -- USB HID gamepad with multiple emulation modes.

## Core Configuration

| Setting | Value |
|---------|-------|
| Routing mode | SIMPLE (1:1) |
| Player slots | 1 (fixed) |
| Profile system | None |

## Key Features

- **4-6 button support** -- Buttons A-D, Select, K3 all mapped.
- **D-pad mode hotkeys** -- Hold Coin + Start + direction for 2 seconds:
  - Down: D-pad mode (default)
  - Left: Left analog stick mode
  - Right: Right analog stick mode
- **Home button** -- Coin + Start pressed together acts as Home/Guide.
- **USB output modes** -- SInput, XInput, PS3, PS4, Switch, Keyboard/Mouse.

## Supported Boards

| Board | Build Command |
|-------|---------------|
| KB2040 | `make neogeo2usb_kb2040` |
| RP2040-Zero | `make neogeo2usb_rp2040zero` |

## Hardware

![NeoGeo2USB RP2040-Zero](../images/neogeo2usb_rp2040_zero_front.png)

## Build and Flash

```bash
make neogeo2usb_kb2040
make flash-neogeo2usb_kb2040
```
