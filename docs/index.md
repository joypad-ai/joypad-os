<style>
  .logo-light, .logo-dark { display: block; margin: 0 auto; }
  .logo-dark { display: none; }
  [data-md-color-scheme="slate"] .logo-light { display: none; }
  [data-md-color-scheme="slate"] .logo-dark { display: block; }
</style>
<div style="text-align: center;">
  <img class="logo-light" src="images/logo_solid_black.svg" alt="Joypad OS" width="300">
  <img class="logo-dark" src="images/logo_solid.svg" alt="Joypad OS" width="300">
</div>

# Joypad OS

**Universal controller firmware for adapters, controllers, and input systems.**

Joypad OS translates any input device into any output protocol. Connect USB, Bluetooth, or WiFi controllers to retro consoles, or bridge native retro controllers to USB. It runs on RP2040, ESP32-S3, and nRF52840 microcontrollers.

---

## Getting Started

<div class="grid cards" markdown>

- :material-download: **[Installation Guide](getting-started/installation.md)**

    Flash firmware to your adapter in minutes

- :material-wrench: **[Build Guide](getting-started/building.md)**

    Set up a development environment and compile from source

- :material-chip: **[Supported Boards](hardware/boards.md)**

    RP2040, ESP32-S3, and nRF52840 boards

- :material-gamepad-variant: **[Supported Controllers](hardware/controllers.md)**

    USB, Bluetooth, keyboards, mice, and hubs

</div>

---

## Console Adapters

| Console | Highlights |
|---------|-----------|
| [**PCEngine / TurboGrafx-16**](adapters/pcengine.md) | Multitap (5 players), mouse, 2/3/6-button |
| [**GameCube / Wii**](adapters/gamecube.md) | Profiles, rumble, keyboard mode |
| [**Sega Dreamcast**](adapters/dreamcast.md) | Rumble, analog triggers, 4 players |
| [**Nuon**](adapters/nuon.md) | Controller, Tempest 3000 spinner, IGR |
| [**3DO**](adapters/3do.md) | 8-player support, mouse, extension passthrough |
| [**Neo Geo / SuperGun**](adapters/neogeo.md) | 7 profiles, 1L6B arcade layouts |
| [**Casio Loopy**](adapters/loopy.md) | 4-player (experimental) |

---

## USB & Bluetooth Adapters

Joypad OS supports a wide range of **USB controllers** (Xbox, PlayStation, Nintendo, 8BitDo, and generic HID), **Bluetooth controllers** via USB dongle, Pico W, ESP32-S3, or nRF52840, and **WiFi controllers** via JOCP protocol on Pico W.

See the [USB Output](adapters/usb.md) and [Bluetooth](adapters/bluetooth.md) documentation for USB output modes, web configurator, and Xbox 360 console support.

---

## Native Input Adapters

Convert retro controllers to USB or bridge them to other consoles:

- [**SNES → USB**](adapters/native-input.md#snes-to-usb-snes2usb) — SNES/NES controller to USB gamepad
- [**N64 → USB**](adapters/native-input.md#n64-to-usb-n642usb) — N64 controller to USB gamepad
- [**GameCube → USB**](adapters/native-input.md#gamecube-to-usb-gc2usb) — GameCube controller to USB gamepad
- [**Neo Geo → USB**](adapters/native-input.md#neo-geo-to-usb-neogeo2usb) — Arcade stick to USB gamepad
- [**N64 → Dreamcast**](adapters/native-input.md#cross-console-adapters), [**SNES → 3DO**](adapters/native-input.md#cross-console-adapters) — Cross-console bridges

---

## Architecture

Learn how Joypad OS works under the hood:

- [**How It Works**](architecture/overview.md) — Data flow, router, input/output layers, platform support
- [**Layers & Internals**](architecture/layers.md) — Developer reference: interfaces, router pipeline, dual-core, latency design

---

## Platforms

| Platform | Apps | Documentation |
|----------|------|---------------|
| RP2040 | All adapters | [Build Guide](getting-started/building.md) |
| ESP32-S3 | bt2usb (BLE) | [ESP32-S3 Docs](platforms/esp32.md) |
| nRF52840 | bt2usb, usb2usb (BLE) | [nRF52840 Docs](platforms/nrf52840.md) |

---

## Protocol Reference

Dive into the low-level console protocols that Joypad OS implements:

- [3DO PBus](protocols/3DO_PBUS.md)
- [GameCube Joybus](protocols/GAMECUBE_JOYBUS.md)
- [Nuon Polyface](protocols/NUON_POLYFACE.md)
- [PC Engine](protocols/PCENGINE.md)

---

## Community & Support

- **Discord**: [community.joypad.ai](http://community.joypad.ai/) — Community chat
- **Issues**: [GitHub Issues](https://github.com/joypad-ai/joypad-os/issues) — Bug reports
- **Source**: [GitHub](https://github.com/joypad-ai/joypad-os) — Contributions welcome

Joypad OS is licensed under the **Apache-2.0 License**.
