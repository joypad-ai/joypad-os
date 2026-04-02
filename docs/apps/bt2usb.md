# bt2usb

Bluetooth controllers to USB HID gamepad output.

## Overview

BT-only variant of [usb2usb](usb2usb.md). Pairs Bluetooth controllers and outputs as a standard USB gamepad. No USB host -- Bluetooth input only. Supports the same USB output modes and web configuration as usb2usb. Available across 4 platforms: Pico W (Classic BT + BLE), Pico 2 W (Classic BT + BLE), ESP32-S3 (BLE only), and nRF52840 (BLE only).

## Input

[Bluetooth](../input/bluetooth.md) controllers via:
- **Pico W / Pico 2 W** -- CYW43 (Classic BT + BLE). Supports DualSense, DualShock 3/4, Xbox, Switch Pro, 8BitDo, etc.
- **ESP32-S3** -- BLE only. Xbox BLE, 8BitDo BLE, Switch 2 Pro BLE, generic BLE HID.
- **nRF52840** -- BLE only. Same BLE controllers as ESP32-S3.

## Output

[USB Device Output](../output/usb-device.md) -- USB HID gamepad with multiple emulation modes.

## Core Configuration

| Setting | Value |
|---------|-------|
| Routing mode | MERGE (all inputs blended to one output) |
| Player slots | 4 (fixed assignment) |
| Max BT connections | 4 |
| Profile system | None |

## Key Features

- Same USB output modes as [usb2usb](usb2usb.md) (SInput, XInput, PS3, PS4, Switch, etc.).
- Web configuration at [config.joypad.ai](https://config.joypad.ai).
- Multiple BT controllers merge into single USB output.
- Auto-scans for controllers on boot.

## Supported Boards

| Board | Platform | BT Type | Build Command |
|-------|----------|---------|---------------|
| Pico W | RP2040 | Classic + BLE | `make bt2usb_pico_w` |
| Pico 2 W | RP2350 | Classic + BLE | `make bt2usb_pico2_w` |
| XIAO ESP32-S3 | ESP32-S3 | BLE only | `make bt2usb_xiao_esp32s3` |
| Feather ESP32-S3 | ESP32-S3 | BLE only | `make bt2usb_feather_esp32s3` |
| XIAO nRF52840 | nRF52840 | BLE only | `make bt2usb_seeed_xiao_nrf52840` |
| Feather nRF52840 | nRF52840 | BLE only | `make bt2usb_feather_nrf52840` |

## Build and Flash

```bash
# Pico W (RP2040)
make bt2usb_pico_w
make flash-bt2usb_pico_w

# ESP32-S3 (requires ESP-IDF)
make bt2usb_xiao_esp32s3
make flash-bt2usb_xiao_esp32s3

# nRF52840 (requires nRF Connect SDK)
make bt2usb_seeed_xiao_nrf52840
make flash-bt2usb_seeed_xiao_nrf52840
```

See [ESP32 platform docs](../platforms/esp32.md) and [nRF52840 platform docs](../platforms/nrf52840.md) for toolchain setup.

## Firmware Files

| Board | Firmware Filename |
|-------|-------------------|
| Pico W | `joypad_*_bt2usb_pico_w.uf2` |
| Pico 2 W | `joypad_*_bt2usb_pico2_w.uf2` |
| XIAO ESP32-S3 | `joypad_*_bt2usb_esp32s3.uf2` |
| XIAO nRF52840 | `joypad_*_bt2usb_seeed_xiao_nrf52840.uf2` |
| Feather nRF52840 | `joypad_*_bt2usb_adafruit_feather_nrf52840.uf2` |
