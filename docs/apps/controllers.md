# Custom GPIO Controllers

GPIO buttons and analog inputs to USB HID gamepad (or BLE + USB).

## Overview

The `controller` app family turns custom hardware with GPIO buttons and analog inputs into USB gamepads. Each controller type is a build-time configuration that defines its board and GPIO pin mapping. The `controller_btusb` variant adds BLE peripheral output alongside USB, and supports I2C sensor inputs (JoyWing seesaw).

## Controller Types

### controller -- GPIO to USB

Single-player GPIO controller with USB HID output. Define the controller type at build time.

| Controller | Board | Build Command |
|------------|-------|---------------|
| Fisher Price V1 | KB2040 | `make controller_fisherprice_v1_kb2040` |
| Fisher Price V2 (analog) | KB2040 | `make controller_fisherprice_v2_kb2040` |
| Alpakka | Pico | `make controller_alpakka_pico` |
| MacroPad | MacroPad RP2040 | `make controller_macropad` |

### controller_btusb -- Sensor to BLE + USB

Modular sensor inputs with dual BLE peripheral + USB device output. First sensor: JoyWing (Adafruit seesaw I2C gamepad).

| Board | Build Command |
|-------|---------------|
| Pico W | `make controller_btusb_pico_w` |
| Pico 2 W | `make controller_btusb_pico2_w` |
| Feather RP2040 | `make controller_btusb_feather_rp2040` |
| Feather ESP32-S3 | `make controller_btusb_feather_esp32s3` |
| Feather nRF52840 | `make controller_btusb_feather_nrf52840` |

## Core Configuration

**controller:**

| Setting | Value |
|---------|-------|
| Routing mode | SIMPLE (1:1) |
| Player slots | 1 (fixed) |
| Input | GPIO buttons/analog |
| Output | USB HID gamepad |

**controller_btusb:**

| Setting | Value |
|---------|-------|
| Routing mode | MERGE |
| Player slots | 1 (fixed) |
| Input | I2C sensor (JoyWing seesaw) |
| Output | BLE peripheral + USB HID gamepad |
