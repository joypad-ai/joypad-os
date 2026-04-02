# usb2uart

USB controllers to UART serial bridge.

## Overview

Reads USB controllers and sends their state over UART at 1Mbaud to an ESP32 or other serial-connected device. The remote device can send feedback commands (rumble, LED) back over UART. Designed as a bridge for the Joypad AI platform where an ESP32 handles higher-level logic.

## Input

- [USB HID](../input/usb-hid.md), [XInput](../input/xinput.md) controllers

## Output

UART serial output at 1Mbaud. TX (GPIO 4), RX (GPIO 5). Qwiic-cable compatible.

## Core Configuration

| Setting | Value |
|---------|-------|
| Routing mode | SIMPLE (1:1) |
| Player slots | 8 (fixed assignment) |
| Max USB devices | 6 |
| UART baud rate | 1,000,000 |
| UART TX pin | GPIO 4 |
| UART RX pin | GPIO 5 |
| Debug UART | GPIO 12 (TX), GPIO 13 (RX) |

## Key Features

- **Per-player feedback** -- Rumble and LED commands from the ESP32 are routed back to individual controllers.
- **8-player support** -- Up to 8 USB controllers via hub, each with a fixed slot for consistent mapping.
- **Separate debug UART** -- Debug output on GPIO 12/13, independent from the bridge UART.

## Supported Boards

| Board | Build Command |
|-------|---------------|
| KB2040 | `make usb2uart_kb2040` |

## Build and Flash

```bash
make usb2uart_kb2040
make flash-usb2uart_kb2040
```
