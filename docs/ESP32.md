# ESP32-S3 Support

Joypad OS supports ESP32-S3 as a build target for the **BT2USB** app. The ESP32-S3 has both BLE and USB OTG, making it a compact standalone BLE-to-USB HID gamepad adapter.

## Overview

| Feature | Pico W / Pico 2 W | ESP32-S3 |
|---|---|---|
| Bluetooth | Classic BT + BLE | BLE only |
| USB Output | USB Device (RP2040) | USB OTG Device |
| Controllers | All BT controllers | BLE controllers only |
| Build System | pico-sdk / CMake | ESP-IDF / CMake |

**BLE-only limitation:** ESP32-S3 only supports BLE controllers. Classic Bluetooth controllers (like DualShock 3 in BT mode) will not work. Most modern controllers support BLE.

### Supported Controllers (BLE)

| Controller | Status |
|---|---|
| Xbox One / Series (BLE mode) | Supported |
| 8BitDo controllers (BLE mode) | Supported |
| Switch 2 Pro (BLE) | Supported |
| Generic BLE HID gamepads | Supported |

### Not Supported (Classic BT only)

| Controller | Why |
|---|---|
| DualShock 3 | Classic BT only |
| Wii U Pro | Classic BT only |
| Wiimote | Classic BT only |

DualShock 4, DualSense, and Switch Pro pair over Classic BT — they require the Pico W build.

## Hardware

### Tested Boards

| Board | Status | Notes |
|---|---|---|
| Seeed XIAO ESP32-S3 | Tested | User LED on GPIO 21 (active low) |
| ESP32-S3-DevKitC | Should work | Untested |

Any ESP32-S3 board with USB OTG should work. Board-specific pin configurations (LED GPIO, button GPIO) can be overridden via sdkconfig.

### Why ESP32-S3?

ESP32-S3 is the only ESP32 variant with both BLE and USB OTG:

| Chip | BLE | USB OTG | bt2usb? |
|---|---|---|---|
| ESP32 | Yes (+ Classic) | No | No |
| ESP32-S2 | No | Yes | No |
| **ESP32-S3** | **Yes** | **Yes** | **Yes** |
| ESP32-C3/C6/H2 | Yes | No | No |

## Build & Flash

### Prerequisites

```bash
# One-time setup: install ESP-IDF and tools
make init-esp
```

This clones ESP-IDF v6.0 to `~/esp-idf` and installs the ESP32-S3 toolchain. If you already have ESP-IDF installed, it will skip the clone and just install tools.

You can also set up manually by following the [ESP-IDF Getting Started Guide](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/).

### Build Commands

```bash
# From repo root
make bt2usb_esp32s3            # Build
make flash-bt2usb_esp32s3      # Flash via esptool
make monitor-bt2usb_esp32s3    # UART serial monitor (Ctrl+] to exit)
```

Or from the `esp/` directory:

```bash
cd esp
make init                      # Install ESP-IDF (if not done from root)
source env.sh                  # Activate environment
make build
make flash
make monitor
```

### Board Configurations

Board-specific sdkconfig overrides go in `esp/sdkconfig.board.<name>`:

```bash
# Example: add a custom board
echo 'CONFIG_SOME_OPTION=y' > esp/sdkconfig.board.myboard
cd esp && make BOARD=myboard build
```

### Firmware Output

The ESP32-S3 build produces a `.bin` file (not `.uf2`). Flashing is done via `esptool` over USB serial, not drag-and-drop.

## Usage

### Pairing

1. Flash the firmware
2. The adapter starts scanning for BLE devices automatically on boot
3. Put your controller into BLE pairing mode
4. Once paired, the controller reconnects automatically on subsequent use

### Button Controls

| Action | Function |
|---|---|
| Click | Start 60-second BLE scan |
| Double-click | Cycle USB output mode |
| Triple-click | Reset to default HID mode |
| Hold | Disconnect all devices and clear bonds |

### Status LED

| LED State | Meaning |
|---|---|
| Blinking | No device connected (scanning/idle) |
| Solid on | Device connected |

### USB Output Modes

Same as Pico W BT2USB — double-click the button to cycle through XInput, DInput, Switch, PS3, PS4/PS5 modes.

### DFU (Firmware Update)

The firmware can be reflashed without physical button access using the CDC serial command:

```
BOOTLOADER
```

Send this over the USB CDC serial port to enter DFU bootloader mode, then flash with `esptool`.

## Architecture

### Threading Model

ESP32-S3 uses FreeRTOS with two tasks:

```
Main Task                    BTstack Task
  USB device polling           BLE scanning/pairing
  LED status updates           HID report processing
  Button input                 Controller data -> router
  Storage persistence
```

BTstack runs in its own FreeRTOS task. All BLE operations (scanning, pairing, data) happen in that task. The main task handles USB output, LED, and button input.

### Shared Code

The ESP32-S3 build compiles the same shared source files as the Pico W build:

- Core services (router, players, profiles, hotkeys, storage, LEDs)
- All USB device output modes (HID, XInput, PS3, PS4, Switch, etc.)
- All BT HID drivers (vendor-specific and generic)
- BTstack host integration
- CDC serial command interface

Platform-specific code is abstracted through `src/platform/platform.h`.

### ESP32-Specific Files

| Location | Purpose |
|---|---|
| `esp/main/main.c` | FreeRTOS entry point |
| `esp/main/flash_esp32.c` | NVS-based settings storage |
| `esp/main/button_esp32.c` | GPIO button driver |
| `esp/main/btstack_config.h` | BLE-only BTstack configuration |
| `esp/main/tusb_config_esp32.h` | TinyUSB device configuration |
| `src/platform/esp32/platform_esp32.c` | Platform HAL (time, reboot, serial) |
| `src/bt/transport/bt_transport_esp32.c` | BLE transport layer |
