# bt2gc

Bluetooth controllers to GameCube/Wii console.

## Overview

BT-only variant of [usb2gc](usb2gc.md) for Pico W and Pico 2 W. Uses the board's built-in CYW43 Bluetooth (Classic BT + BLE) to receive controllers and outputs to GameCube via joybus. Shares the same profile system and keyboard mode as usb2gc. No USB host -- Bluetooth input only.

## Input

[Bluetooth](../input/bluetooth.md) controllers via CYW43 (Classic BT + BLE).

## Output

[GameCube Output](../output/gamecube.md) -- joybus PIO protocol. Requires 130MHz overclock.

## Core Configuration

| Setting | Value |
|---------|-------|
| Routing mode | MERGE (all inputs blended to one output) |
| Player slots | 1 (single GC port) |
| Max BT connections | 4 |
| Clock speed | 130MHz |
| Profile system | Same 5 profiles as usb2gc |

## Key Features

- Same profiles, keyboard mode, and rumble support as [usb2gc](usb2gc.md).
- Auto-scans for controllers on boot. Click BOOTSEL to rescan.

## Supported Boards

| Board | Build Command |
|-------|---------------|
| Pico W | `make bt2gc_pico_w` |
| Pico 2 W | `make bt2gc_pico2_w` |

## Build and Flash

```bash
make bt2gc_pico_w
make flash-bt2gc_pico_w
```
