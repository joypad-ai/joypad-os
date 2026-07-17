# How It Works

Joypad OS is a modular firmware that translates between any input device and any output protocol. It runs on RP2040, ESP32-S3, and nRF52840 microcontrollers.

## Data Flow

```
Input Sources                    Router                      Output Targets
─────────────                    ──────                      ──────────────
USB HID ──────┐                                              ┌──→ PCEngine
USB XInput ───┤                                              ├──→ GameCube
Bluetooth ────┼──→ router_submit_input() ──→ router ──→ ────┼──→ Dreamcast
WiFi (JOCP) ──┤                              │               ├──→ Nuon
SNES / N64 ───┤                              ▼               ├──→ 3DO / Loopy
GameCube ─────┘                     profile_apply()          ├──→ Neo Geo
                                 (button remapping)          ├──→ USB Device
                                                             └──→ UART
```

Every controller input — whether USB, Bluetooth, WiFi, or a native retro controller — is normalized into a common `input_event_t` structure, routed through the router, and translated into the output protocol.

## Input Layer

Joypad OS accepts input from multiple sources:

**USB HID** — Standard USB gamepads, keyboards, and mice. Vendor-specific drivers handle controllers from Xbox, PlayStation, Nintendo, 8BitDo, Hori, and more. Generic HID parsing covers everything else.

**USB XInput** — Xbox 360/One/Series controllers using the XInput protocol.

**Bluetooth** — Wireless controllers via USB Bluetooth dongle (RP2040), built-in radio (Pico W), or BLE (ESP32-S3, nRF52840). Uses BTstack for Classic BT and BLE HID.

**WiFi (JOCP)** — The Joypad Open Controller Protocol allows controllers to connect over WiFi on Pico W boards. The adapter runs as a WiFi access point with UDP/TCP servers.

**Native Controllers** — Direct reading of SNES, N64, and GameCube controllers using PIO-based protocols. Useful for building retro-to-modern adapters (e.g., SNES→USB, N64→Dreamcast).

## Router

The router connects inputs to outputs. Every app configures which routing mode to use:

**SIMPLE** — 1:1 mapping. Controller N goes to player slot N. Used by most console adapters.

**MERGE** — All inputs merge into a single output. Used for copilot/accessibility modes and BT2USB (multiple BT controllers → one USB gamepad).

**BROADCAST** — All inputs go to all outputs. Used for specialized multi-output setups.

The router manages player slots, handles connect/disconnect events, and forwards feedback (rumble, LEDs) from outputs back to inputs.

## Output Layer

Outputs translate the common input format into console-specific or USB protocols:

**Console Protocols** — PCEngine, GameCube, Dreamcast, Nuon, 3DO, Neo Geo, and Casio Loopy. Each uses RP2040 PIO (Programmable I/O) state machines for cycle-accurate timing. Console output runs on Core 1 to avoid interference from USB/BT processing on Core 0.

**USB Device** — Emulates various USB gamepads: HID Gamepad, XInput (Xbox 360), PS3, PS4, Switch, and more. Supports real Xbox 360 console authentication (XSM3). Configurable via [config.joypad.ai](https://config.joypad.ai).

**UART** — Serial bridge for ESP32 Bluetooth modules (legacy).

## Services

Built-in services provide cross-cutting features available to all apps:

**Profiles** — Button remapping. Each app can define multiple profiles (e.g., Default, SSBM, MKWii for GameCube). Users cycle profiles by holding SELECT + D-pad Up/Down for 2 seconds. Selection persists to flash.

**Player Management** — Tracks connected controllers, assigns player slots, and manages feedback routing (rumble, player LEDs, RGB).

**Hotkeys** — Detects button combos for special actions (e.g., L1+R1+Start+Select for Nuon in-game reset).

**LED Feedback** — NeoPixel RGB LED shows connection status and profile changes.

**Storage** — Flash persistence for settings, profiles, and Bluetooth bonds.

## Platform Support

Joypad OS runs on three microcontroller platforms:

### RP2040

The primary platform. Dual-core ARM Cortex-M0+ with hardware PIO for cycle-accurate console protocol timing.

- **Core 0**: USB host polling, Bluetooth, input processing, main loop
- **Core 1**: Console output protocol (timing-critical PIO programs)
- **PIO**: Each console protocol has dedicated PIO programs (e.g., `joybus.pio` for GameCube, `maple.pio` for Dreamcast)
- **All apps supported**: Console adapters, USB output, BT/WiFi input, native controller reading

### ESP32-S3

Runs `bt2usb` only (BLE to USB adapter). Uses FreeRTOS with separate tasks for BTstack (BLE) and USB device output.

- **BLE only** — no Classic Bluetooth. Modern controllers (Xbox BLE, 8BitDo BLE) work; DS4/Switch Pro require Pico W
- **USB OTG** — native USB device support (no PIO needed)
- **TinyUF2** — drag-and-drop firmware updates
- See [ESP32-S3 platform docs](../platforms/esp32.md) for setup

### nRF52840

Runs `bt2usb` and `usb2usb` (with MAX3421E USB host chip). Uses Zephyr RTOS with BTstack running in its own thread.

- **BLE only** — same controller support as ESP32-S3
- **USB host via MAX3421E** — SPI-connected USB host IC for `usb2usb` on Feather nRF52840
- **UF2 bootloader** — drag-and-drop firmware updates
- See [nRF52840 platform docs](../platforms/nrf52840.md) for setup

## Apps

An "app" is a build configuration that selects which inputs, outputs, and features to enable. Each app lives in `src/apps/<name>/` and defines:

- Which input sources to enable (USB host, Bluetooth, native controller, WiFi)
- Which output interface to use (console protocol, USB device, UART)
- Router mode (SIMPLE, MERGE, BROADCAST)
- Button remapping profiles
- Hardware pin assignments

For example, `usb2gc` enables USB host + Bluetooth input, GameCube joybus output, SIMPLE routing, and 5 GameCube-specific button profiles. Meanwhile, `bt2usb` enables Bluetooth input, USB device output, and MERGE routing.

See the [adapter docs](../adapters/pcengine.md) for per-app details, or the [layers & internals](layers.md) doc for the full developer architecture reference.
