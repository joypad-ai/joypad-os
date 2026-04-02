# bt2nuon

Bluetooth controllers to Nuon DVD player.

## Overview

BT-only variant of [usb2nuon](usb2nuon.md) for Pico W and Pico 2 W. Uses the board's built-in CYW43 Bluetooth to receive controllers and outputs to a Nuon DVD player via the Polyface protocol. Supports spinner emulation and IGR like usb2nuon.

## Input

[Bluetooth](../input/bluetooth.md) controllers via CYW43 (Classic BT + BLE).

## Output

[Nuon Output](../output/nuon.md) -- Polyface PIO protocol.

## Core Configuration

| Setting | Value |
|---------|-------|
| Routing mode | MERGE (all inputs blended to one output) |
| Player slots | 1 |
| Max BT connections | 4 |
| Profile system | Yes |
| Spinner support | Right stick to spinner |
| IGR | L1 + R1 + Start + Select |

## Key Features

- Same spinner emulation, IGR, and profiles as [usb2nuon](usb2nuon.md).
- Auto-scans for controllers on boot.

## Supported Boards

| Board | Build Command |
|-------|---------------|
| Pico W | `make bt2nuon_pico_w` |
| Pico 2 W | `make bt2nuon_pico2_w` |

## Build and Flash

```bash
make bt2nuon_pico_w
make flash-bt2nuon_pico_w
```
