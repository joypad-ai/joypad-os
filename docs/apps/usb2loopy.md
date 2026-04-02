# usb2loopy

USB/BT controllers to Casio Loopy console. **Experimental.**

## Overview

Connects USB and Bluetooth controllers to the Casio Loopy, a Japan-only 32-bit console from 1995. Supports up to 4 players via PIO-based serial protocol. The Loopy protocol is partially implemented -- timing issues may exist and testing has been limited.

## Input

- [USB HID](../input/usb-hid.md), [XInput](../input/xinput.md), [Bluetooth](../input/bluetooth.md) controllers

## Output

[Loopy Output](../output/loopy.md) -- PIO-based serial protocol.

## Core Configuration

| Setting | Value |
|---------|-------|
| Routing mode | SIMPLE (1:1) |
| Player slots | 4 (shift on disconnect) |
| Max USB devices | 4 |
| Profile system | None |

## Supported Boards

| Board | Build Command |
|-------|---------------|
| KB2040 | `make usb2loopy_kb2040` |

## Build and Flash

```bash
make usb2loopy_kb2040
make flash-usb2loopy_kb2040
```

## Compatible Games

The Casio Loopy has a limited library of approximately 10 titles:

- Anime Land
- Bow-wow Puppy Love Story
- Dream Change
- HARIHARI Seal Paradise
- Little Romance
- Lupiton's Wonder Palette
- Magical Shop
- PC Collection
- Wanwan Aijou Monogatari
