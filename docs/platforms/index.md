# Platforms

Joypad OS is a cross-platform firmware that builds against three different SDKs, each supporting different microcontroller families. The SDK determines what hardware features are available, how the build system works, and what RTOS (if any) is used.

## Supported Platforms

| SDK | Chips | Boards | Apps | Build System |
|-----|-------|--------|------|-------------|
| [Pico SDK](pico-sdk.md) | RP2040, RP2350 | Pico, Pico 2, Pico W, Pico 2 W, KB2040, Feather RP2040, RP2040-Zero, MacroPad | All 30+ apps | CMake + gcc-arm-embedded |
| [ESP-IDF](esp32.md) | ESP32-S3 | Seeed XIAO ESP32-S3 | bt2usb | CMake + ESP-IDF toolchain |
| [nRF Connect SDK](nrf52840.md) | nRF52840 | Seeed XIAO nRF52840, Adafruit Feather nRF52840 | bt2usb, usb2usb | west + Zephyr + gcc-arm-zephyr |

## How Platforms Work

Each platform implements the same [Platform HAL](../core/platform-hal.md) (`platform.h`) providing time, identity, and reboot functions. Shared source code in `src/` uses this HAL rather than calling SDK functions directly.

The key architectural differences:

**Pico SDK (primary platform)** -- Bare-metal dual-core. Core 0 runs the main loop (USB host, Bluetooth, input processing, services). Core 1 runs timing-critical console output via PIO. No RTOS. All console output protocols (GameCube, Dreamcast, PCEngine, etc.) are only available on this platform because they require PIO state machines.

**ESP-IDF** -- FreeRTOS with separate tasks for BTstack (BLE) and USB device output. BLE only (no Classic Bluetooth). Native USB OTG device support. No PIO, so no console protocol output -- USB HID output only.

**nRF Connect SDK** -- Zephyr RTOS with BTstack running in its own thread. BLE only. USB host requires external MAX3421E SPI chip (no native USB host). Raw HCI passthrough from Zephyr to BTstack.

## Shared Code

All three platforms share the same core source files:

- `src/core/` -- Router, profiles, players, storage, LEDs, buttons
- `src/bt/` -- BTstack integration and BT HID device drivers
- `src/usb/usbd/` -- USB device output modes (TinyUSB)

Platform-specific code lives in:

- `src/platform/rp2040/` -- Pico SDK platform implementation
- `esp/main/` -- ESP-IDF entry point and HAL glue
- `nrf/src/` -- Zephyr entry point and HAL glue

## Choosing a Platform

- **Building a console adapter?** Use Pico SDK (RP2040/RP2350). It's the only platform with PIO for console protocols.
- **Building a BLE-to-USB adapter?** Any platform works. Pico W gives you Classic BT + BLE. ESP32-S3 and nRF52840 are BLE-only but more compact.
- **Need USB host input?** Pico SDK (native PIO-USB or RP2040 USB host) or nRF52840 (MAX3421E SPI).
- **Need WiFi?** Pico W only (JOCP protocol).
