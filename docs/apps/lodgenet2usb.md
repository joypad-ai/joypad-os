# lodgenet2usb

LodgeNet hotel gaming controller to USB HID gamepad.

## Overview

Reads Nintendo LodgeNet hotel controllers via a proprietary 3-wire serial protocol over an RJ11 connector and outputs as a USB HID gamepad. Automatically detects and switches between N64, GameCube, and SNES LodgeNet controller variants with hot-swap support. Reports the detected controller's face style (Nintendo/GameCube) via SInput so the host can display correct button labels.

## Input

[LodgeNet Input](../input/lodgenet.md) -- PIO-based 3-wire serial protocol. MCU protocol for N64/GC (~60Hz), SR protocol for SNES (~131Hz). Auto-detection cycles between protocols.

## Output

[USB Device Output](../output/usb-device.md) -- USB HID gamepad with multiple emulation modes.

## Core Configuration

| Setting | Value |
|---------|-------|
| Routing mode | SIMPLE (1:1) |
| Player slots | 1 (fixed) |
| Clock pin | GPIO 3 |
| Data pin | GPIO 2 |
| VCC pin | GPIO 4 |
| Clock2 pin | GPIO 5 (SNES SR protocol) |
| Connector | RJ11 6P6C |

## Key Features

- **Auto-detection** -- N64, GameCube, and SNES controllers detected automatically. MCU fails 5x then switches to SR, and vice versa.
- **Hot-swap** -- Switch between controller types without rebooting.
- **Full analog** -- N64 stick (scaled from +/-80 to full range), GC sticks + triggers.
- **LodgeNet system buttons** -- Menu, Order, Select, Plus, Minus mapped to extended buttons (A1-A4).
- **LED indicator** -- Blinks when idle, solid when connected.
- **USB output modes** -- SInput, XInput, PS3, PS4, Switch, Keyboard/Mouse.

## Supported Boards

| Board | Build Command |
|-------|---------------|
| Pico | `make lodgenet2usb_pico` |
| Pico 2 | `make lodgenet2usb_pico2` |

## Build and Flash

```bash
make lodgenet2usb_pico
make flash-lodgenet2usb_pico
```
