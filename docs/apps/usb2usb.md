# usb2usb

USB/BT controllers to USB HID gamepad output.

## Overview

Translates any supported USB or Bluetooth controller into a standard USB gamepad. Supports 13 USB output modes (SInput, XInput, PS3, PS4, Switch, Keyboard/Mouse, and more) that can be switched at runtime. Includes web-based configuration at [config.joypad.ai](https://config.joypad.ai) for mode switching, profile management, input monitoring, and firmware updates.

## Input

- [USB HID](../input/usb-hid.md), [XInput](../input/xinput.md), [Bluetooth](../input/bluetooth.md) controllers
- USB mouse (mapped to right analog stick)

## Output

[USB Device Output](../output/usb-device.md) -- USB HID gamepad with multiple emulation modes.

## Core Configuration

| Setting | Value |
|---------|-------|
| Routing mode | MERGE (all inputs blended to one output) |
| Player slots | 4 (fixed assignment) |
| Max USB devices | 4 |
| Mouse transform | Mouse X/Y mapped to right analog stick |

## USB Output Modes

Double-click the board button to cycle modes. Triple-click to reset to SInput.

**Cycle order:** SInput, XInput, PS3, PS4, Switch, Keyboard/Mouse

Additional modes available via web config: PS Classic, Xbox Original, Xbox One, Xbox Adaptive Controller, Wii U GC Adapter, Generic HID.

XInput mode works on real Xbox 360 consoles (XSM3 authentication). Mode is saved to flash.

See [USB Device Output](../output/usb-device.md) for full mode details, feature matrix, and CDC command reference.

## Key Features

- **Web configuration** -- [config.joypad.ai](https://config.joypad.ai) via WebSerial (Chrome/Edge). Mode switching, profile editor, input monitor, rumble test, BT management.
- **Feedback forwarding** -- Rumble, player LEDs, and RGB (PS4 lightbar) from the host are forwarded back to the input controller.
- **Xbox 360 console support** -- XInput mode authenticates with real Xbox 360 hardware via XSM3.

## Supported Boards

| Board | Build Command |
|-------|---------------|
| Pico | `make usb2usb_pico` |
| Pico W | `make usb2usb_pico_w` |
| Pico 2 W | `make usb2usb_pico2_w` |
| Feather RP2040 | `make usb2usb_feather_rp2040` |
| Feather RP2040 USB Host | `make usb2usb_feather_rp2040_usb_host` |
| RP2040-Zero | `make usb2usb_rp2040zero` |
| RP2350-USB-A | `make usb2usb_rp2350usba` |
| Feather ESP32-S3 | `make usb2usb_feather_esp32s3` |
| Feather nRF52840 | `make usb2usb_feather_nrf52840` |

## Hardware

![USB2USB Pico Host Wiring](../images/usb2usb_pico_host.png)

## Build and Flash

```bash
make usb2usb_feather_rp2040
make flash-usb2usb_feather_rp2040
```
