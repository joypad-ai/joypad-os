> **Note:** This page is being replaced by the new layer-based documentation.
> See [BT2USB](../apps/bt2usb.md) and [Bluetooth Input](../input/bluetooth.md).

# BT2USB — Bluetooth to USB Adapter

BT2USB turns a Raspberry Pi Pico W, Pico 2 W, ESP32-S3, or nRF52840 board into a wireless Bluetooth-to-USB controller adapter. Pair any supported Bluetooth controller and it outputs as a standard USB gamepad.

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

### nRF52840 (BLE only)

| Controller | Status |
|---|---|
| Xbox One / Series (BLE mode) | Supported |
| 8BitDo controllers (BLE mode) | Supported |
| Switch 2 Pro (BLE) | Supported |
| Generic BLE HID gamepads | Supported |

Classic BT controllers (DS3, DS4, DS5, Switch Pro, Wii U Pro) are not supported on ESP32-S3 or nRF52840. See [ESP32 docs](../platforms/esp32.md) and [nRF52840 docs](../platforms/nrf52840.md) for details.

## Hardware

- **Pico W / Pico 2 W** — built-in Bluetooth, no dongle needed
- **ESP32-S3** — built-in BLE, requires ESP-IDF to build ([setup guide](../platforms/esp32.md))
- **nRF52840** — built-in BLE, requires nRF Connect SDK to build ([setup guide](../platforms/nrf52840.md))

## Firmware Files

| Board | File |
|---|---|
| Pico W | `joypad_*_bt2usb_pico_w.uf2` |
| Pico 2 W | `joypad_*_bt2usb_pico2_w.uf2` |
| ESP32-S3 | `joypad_*_bt2usb_esp32s3.uf2` (drag-and-drop via TinyUF2) |
| XIAO nRF52840 | `joypad_*_bt2usb_seeed_xiao_nrf52840.uf2` |
| Feather nRF52840 | `joypad_*_bt2usb_adafruit_feather_nrf52840.uf2` |

## Pairing

1. Flash the BT2USB firmware to your Pico W / Pico 2 W
2. Put your controller into pairing mode
3. The adapter automatically enters pairing mode on boot
4. Once paired, the controller reconnects automatically on subsequent use

## USB Output Modes

BT2USB supports the same USB output modes as USB2USB. Double-click the button to cycle through modes. Triple-click to reset to default.

- **SInput** — SDL/Steam compatible (default)
- **XInput** — Xbox 360 compatible
- **PS3** — PlayStation 3
- **PS4** — PlayStation 4
- **Switch** — Nintendo Switch
- **Keyboard/Mouse** — Keyboard + Mouse composite HID

## Web Configuration

Connect the adapter to a computer and open **[config.joypad.ai](https://config.joypad.ai)** in a WebSerial-capable browser (Chrome, Edge) for mode switching, input monitoring, and firmware updates.

## Multiple Controllers

BT2USB can pair multiple Bluetooth controllers simultaneously. Currently, inputs from all paired controllers are merged into a single USB output.
