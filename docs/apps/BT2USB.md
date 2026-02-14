# BT2USB — Bluetooth to USB Adapter

BT2USB turns a Raspberry Pi Pico W (or Pico 2 W) into a wireless Bluetooth-to-USB controller adapter. Pair any supported Bluetooth controller and it outputs as a standard USB gamepad.

## Supported Controllers

| Controller | Status |
|---|---|
| DualSense (PS5) | ✅ Supported |
| DualShock 4 (PS4) | ✅ Supported |
| DualShock 3 (PS3) | ✅ Supported |
| Xbox One / Series | ✅ Supported |
| Switch Pro | ✅ Supported |
| Switch 2 Pro | ✅ Supported |
| Wii U Pro | ✅ Supported |
| NSO GameCube | ✅ Supported |
| Google Stadia | ✅ Supported |
| Generic BT HID | ⚠️ Basic support (may need remapping) |

## Hardware

- **Required:** Raspberry Pi Pico W or Pico 2 W
- **No external Bluetooth dongle needed** — uses the Pico W's built-in antenna

## Firmware Files

| Board | File |
|---|---|
| Pico W | `joypad_*_bt2usb_pico_w.uf2` |
| Pico 2 W | `joypad_*_bt2usb_pico2_w.uf2` |

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
