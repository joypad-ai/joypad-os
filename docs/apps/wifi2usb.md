# wifi2usb

WiFi controllers (JOCP protocol) to USB HID gamepad output.

## Overview

Creates a WiFi access point on a Pico W or Pico 2 W and receives controller input via the JOCP (Joypad Open Controller Protocol) over UDP. Outputs as a standard USB gamepad. Up to 4 wireless controllers can connect simultaneously. Also broadcasts a BLE beacon for iOS device discovery.

## Input

[WiFi JOCP](../input/wifi-jocp.md) -- Controllers connect to the adapter's WiFi AP and send UDP packets on port 30100.

## Output

[USB Device Output](../output/usb-device.md) -- USB HID gamepad with multiple emulation modes.

## Core Configuration

| Setting | Value |
|---------|-------|
| Routing mode | MERGE (all inputs blended to one output) |
| Player slots | 4 (fixed assignment) |
| WiFi AP SSID | JOYPAD-XXXX (unique per device) |
| WiFi AP password | joypad1234 |
| WiFi channel | 6 |
| JOCP UDP port | 30100 |
| JOCP TCP port | 30101 |
| Output mode switching | Yes |

## Key Features

- **WiFi AP mode** -- Adapter creates its own network. No router needed.
- **JOCP protocol** -- Lightweight UDP-based controller protocol. Low latency.
- **BLE beacon** -- Broadcasts for iOS app discovery alongside WiFi.
- **USB output modes** -- Same mode switching as [usb2usb](usb2usb.md).

## Supported Boards

| Board | Build Command |
|-------|---------------|
| Pico W | `make wifi2usb_pico_w` |
| Pico 2 W | `make wifi2usb_pico2_w` |

## Build and Flash

```bash
make wifi2usb_pico_w
make flash-wifi2usb_pico_w
```
