# snes2usb

SNES/NES controller to USB HID gamepad.

## Overview

Reads a native SNES or NES controller directly via GPIO and outputs as a USB HID gamepad. Also supports SNES mouse and Xband keyboard. Includes USB output mode switching and web configuration.

## Input

[SNES Input](../input/snes.md) -- GPIO-based shift register polling. Reads SNES controllers, NES controllers, SNES mouse, and Xband keyboard.

## Output

[USB Device Output](../output/usb-device.md) -- USB HID gamepad with multiple emulation modes.

## Core Configuration

| Setting | Value |
|---------|-------|
| Routing mode | SIMPLE (1:1) |
| Player slots | 1 (fixed) |
| Profile system | None |
| Mouse support | SNES mouse |

## Key Features

- **Button mapping** -- B=B1, A=B2, Y=B3, X=B4, L=L1, R=R1, Select=S1, Start=S2, D-Pad=D-Pad.
- **Multi-device** -- SNES controller, NES controller, SNES mouse, Xband keyboard all auto-detected.
- **Rumble** -- Supported via LRG protocol if the controller has it.
- **USB output modes** -- SInput, XInput, PS3, PS4, Switch, Keyboard/Mouse.
- **Web config** -- [config.joypad.ai](https://config.joypad.ai) for mode switching and monitoring.

## Supported Boards

| Board | Build Command |
|-------|---------------|
| KB2040 | `make snes2usb_kb2040` |

## Build and Flash

```bash
make snes2usb_kb2040
make flash-snes2usb_kb2040
```
