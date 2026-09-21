# Custom GPIO Controllers

GPIO buttons and analog inputs to USB HID gamepad (or BLE + USB).

## Overview

The `controller` app family turns custom hardware with GPIO buttons and analog inputs into USB gamepads. Each controller type is a build-time configuration that defines its board and GPIO pin mapping. The `universal` variant adds BLE peripheral output alongside USB, supports I2C sensor inputs (JoyWing seesaw), and optionally USB host input for connecting external controllers.

## Controller Types

### controller -- GPIO to USB

Single-player GPIO controller with USB HID output. Define the controller type at build time.

| Controller | Board | Build Command |
|------------|-------|---------------|
| Fisher Price V1 | KB2040 | `make controller_fisherprice_v1_kb2040` |
| Fisher Price V2 (analog) | KB2040 | `make controller_fisherprice_v2_kb2040` |
| Alpakka | Pico | `make controller_alpakka_pico` |
| MacroPad | MacroPad RP2040 | `make controller_macropad` |

### universal -- Sensor to BLE + USB

Modular sensor inputs with dual BLE peripheral + USB device output. First sensor: JoyWing (Adafruit seesaw I2C gamepad).

| Board | Build Command | BLE | USB Host |
|-------|---------------|-----|----------|
| Pico W | `make universal_pico_w` | Yes | No |
| Pico 2 W | `make universal_pico2_w` | Yes | No |
| Feather RP2040 | `make universal_feather_rp2040` | No | No |
| Feather ESP32-S3 | `make universal_feather_esp32s3` | Yes | No |
| Feather nRF52840 | `make universal_feather_nrf52840` | Yes | No |
| ABB (Passthrough) | `make universal_rp2040_abb` | No | Yes (PIO-USB) |

### ABB (RP2040 Advanced Breakout Board)

The ABB Passthrough variant is a plain RP2040 board (no CYW43, no BLE antenna) with:
- 20-pin Brook-compatible connector + screw terminals for GPIO buttons
- USB-A passthrough port on GPIO 23/24 (PIO-USB host input)
- WS2812 RGB LED header on GPIO 4 (optional, external strip)
- I2C OLED header on GPIO 0/1 (optional, external display)
- SMD slider switches (hardware circuit routing, not GPIO-readable)

**GP2040-CE board labels → Joypad button mapping:**

| Board Label | GPIO | Joypad Button | Standard Name |
|-------------|------|---------------|---------------|
| K1 | 12 | B3 | X / Square |
| K2 | 11 | B4 | Y / Triangle |
| K3 | 8 | B1 | A / Cross |
| K4 | 7 | B2 | B / Circle |
| P1 | 10 | R1 | RB |
| P2 | 9 | L1 | LB |
| P3 | 6 | R2 | RT |
| P4 | 5 | L2 | LT |
| S1 | 15 | S1 | Select / Back |
| S2 | 13 | S2 | Start |
| L3 | 21 | L3 | Left Stick Click |
| R3 | 22 | R3 | Right Stick Click |
| A1 | 14 | A1 | Home / Guide |
| A2 | 20 | A2 | Capture |
| D-Up | 19 | DU | D-Pad Up |
| D-Down | 18 | DD | D-Pad Down |
| D-Left | 16 | DL | D-Pad Left |
| D-Right | 17 | DR | D-Pad Right |

**USB Host:** Set the SMD USB/Option slider to the **USB** position (left) to route GPIO 23/24 to the USB-A port. Connect any supported USB controller for input.

**GPIO Pin Config:** Button-to-GPIO mapping is configurable at runtime via the web config GPIO Pin Configuration page.

## Core Configuration

**controller:**

| Setting | Value |
|---------|-------|
| Routing mode | SIMPLE (1:1) |
| Player slots | 1 (fixed) |
| Input | GPIO buttons/analog |
| Output | USB HID gamepad |

**universal:**

| Setting | Value |
|---------|-------|
| Routing mode | MERGE |
| Player slots | 1 (fixed) |
| Input | GPIO buttons/analog, JoyWing seesaw, USB host (ABB) |
| Output | BLE peripheral + USB HID gamepad (BLE optional per board) |
