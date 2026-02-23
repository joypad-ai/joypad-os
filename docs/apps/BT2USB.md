# BT2USB — Bluetooth to USB Adapter

BT2USB turns a Raspberry Pi Pico W, Pico 2 W, or ESP32-S3 board into a wireless Bluetooth-to-USB controller adapter. Pair any supported Bluetooth controller and it outputs as a standard USB gamepad.

## Supported Controllers

### Pico W / Pico 2 W (Classic BT + BLE)

| Controller | Status |
|---|---|
| DualSense (PS5) | Supported |
| DualShock 4 (PS4) | Supported |
| DualShock 3 (PS3) | Supported |
| Xbox One / Series | Supported |
| Switch Pro | Supported |
| Switch 2 Pro | Supported |
| Wii U Pro | Supported |
| NSO GameCube | Supported |
| Google Stadia | Supported |
| Generic BT HID | Basic support (may need remapping) |

### ESP32-S3 (BLE only)

| Controller | Status |
|---|---|
| Xbox One / Series (BLE mode) | Supported |
| 8BitDo controllers (BLE mode) | Supported |
| Switch 2 Pro (BLE) | Supported |
| Generic BLE HID gamepads | Supported |

Classic BT controllers (DS3, DS4, DS5, Switch Pro, Wii U Pro) are not supported on ESP32-S3. See [ESP32 docs](../ESP32.md) for details.

## Hardware

- **Pico W / Pico 2 W** — built-in Bluetooth, no dongle needed
- **ESP32-S3** — built-in BLE, requires ESP-IDF to build ([setup guide](../ESP32.md))

## Firmware Files

| Board | File |
|---|---|
| Pico W | `joypad_*_bt2usb_pico_w.uf2` |
| Pico 2 W | `joypad_*_bt2usb_pico2_w.uf2` |
| ESP32-S3 | `joypad_*_bt2usb_esp32s3.uf2` (drag-and-drop via TinyUF2) |

## Pairing

1. Flash the BT2USB firmware to your Pico W / Pico 2 W
2. Put your controller into pairing mode
3. The adapter automatically enters pairing mode on boot
4. Once paired, the controller reconnects automatically on subsequent use

## USB Output Modes

BT2USB supports the same USB output modes as USB2USB. Double-click the button to cycle through modes:

- **Default** — XInput (Xbox 360 compatible)
- **Mode 2** — DInput (generic USB gamepad)
- **Mode 3** — Nintendo Switch (Pokken)
- **Mode 4** — PS3
- **Mode 5** — PS4/PS5

## Web Config

Web config is accessible in default mode only. Triple-click the button to return to default mode if you've switched to a console-specific output.

## Multiple Controllers

BT2USB can pair multiple Bluetooth controllers simultaneously. Currently, inputs from all paired controllers are merged into a single USB output.
