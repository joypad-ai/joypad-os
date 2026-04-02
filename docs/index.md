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

## Understand the System

<div class="grid cards" markdown>

- **[Architecture](overview/architecture.md)**

    The 4-layer model: inputs, core, outputs, and apps

- **[Data Flow](overview/data-flow.md)**

    How controller input travels from device to console

- **[Glossary](overview/glossary.md)**

    Key terms: input_event_t, router, profiles, PIO, and more

</div>

---

## Browse by Layer

<div class="grid cards" markdown>

- **[Input Interfaces](input/index.md)**

    USB HID, XInput, Bluetooth, WiFi, SNES, N64, GameCube, NES, Neo Geo, LodgeNet, and more

- **[Output Interfaces](output/index.md)**

    GameCube, PCEngine, Dreamcast, Nuon, 3DO, N64, Loopy, USB Device, BLE, UART

- **[Joypad Core](core/index.md)**

    Router, profiles, players, storage, LEDs, hotkeys, and other shared services

- **[Apps](apps/index.md)**

    30+ firmware configurations: usb2gc, bt2usb, snes2usb, n642dc, and more

</div>

---

## Getting Started

<div class="grid cards" markdown>

- **[Installation Guide](getting-started/installation.md)**

    Flash firmware to your adapter in minutes

- **[Build Guide](getting-started/building.md)**

    Set up a development environment and compile from source

- **[Supported Boards](hardware/boards.md)**

    RP2040, ESP32-S3, and nRF52840 boards

- **[Supported Controllers](hardware/controllers.md)**

    USB, Bluetooth, keyboards, mice, and hubs

- **[Web Config](core/web-config.md)**

    Configure your adapter from the browser -- no install required

</div>

---

## Console Adapters

| Console | App | Highlights |
|---------|-----|-----------|
| [PCEngine / TurboGrafx-16](apps/usb2pce.md) | usb2pce | Multitap (5 players), mouse, 2/3/6-button |
| [GameCube / Wii](apps/usb2gc.md) | usb2gc | Profiles, rumble, keyboard mode |
| [Sega Dreamcast](apps/usb2dc.md) | usb2dc | Rumble, analog triggers, 4 players |
| [Nuon](apps/usb2nuon.md) | usb2nuon | Controller, Tempest 3000 spinner, IGR |
| [3DO](apps/usb23do.md) | usb23do | 8-player support, mouse, extension passthrough |
| [Neo Geo / SuperGun](apps/usb2neogeo.md) | usb2neogeo | 7 profiles, 1L6B arcade layouts |
| [Casio Loopy](apps/usb2loopy.md) | usb2loopy | 4-player (experimental) |
| [N64 (wireless)](apps/bt2n64.md) | bt2n64 | BT controllers, rumble, profiles |

---

## Native Input Adapters

Convert retro controllers to USB or bridge them to other consoles:

- [**SNES to USB**](apps/snes2usb.md) -- SNES/NES controller to USB gamepad
- [**N64 to USB**](apps/n642usb.md) -- N64 controller to USB gamepad
- [**GameCube to USB**](apps/gc2usb.md) -- GameCube controller to USB gamepad
- [**NES to USB**](apps/nes2usb.md) -- NES controller to USB gamepad
- [**Neo Geo to USB**](apps/neogeo2usb.md) -- Arcade stick to USB gamepad
- [**LodgeNet to USB**](apps/lodgenet2usb.md) -- LodgeNet hotel controllers (N64/GC/SNES) to USB
- [**N64 to Dreamcast**](apps/n642dc.md), [**SNES to 3DO**](apps/snes23do.md), [**N64 to Nuon**](apps/n642nuon.md) -- Cross-console bridges

---

## Platforms

| Platform | Apps | Documentation |
|----------|------|---------------|
| RP2040 | All adapters | [Build Guide](getting-started/building.md) |
| ESP32-S3 | bt2usb (BLE) | [ESP32-S3 Docs](platforms/esp32.md) |
| nRF52840 | bt2usb, usb2usb (BLE) | [nRF52840 Docs](platforms/nrf52840.md) |

---

## For Developers

- **[Development Guide](development/index.md)** -- How to add inputs, outputs, apps, and drivers
- **[Architecture](overview/architecture.md)** -- The 4-layer model and dual-core execution
- **[Data Flow](overview/data-flow.md)** -- Input-to-output pipeline walkthrough
- **[Router](core/router.md)** -- Routing modes, merge, transforms

---

## Protocol Reference

Dive into the low-level console protocols that Joypad OS implements:

- [3DO PBus](protocols/3DO_PBUS.md)
- [GameCube Joybus](protocols/GAMECUBE_JOYBUS.md)
- [Nuon Polyface](protocols/NUON_POLYFACE.md)
- [PC Engine](protocols/PCENGINE.md)
- [LodgeNet](protocols/lodgenet.md)
- [SInput HID](protocols/sinput.md)

---

## Community & Support

- **Discord**: [community.joypad.ai](http://community.joypad.ai/) -- Community chat
- **Issues**: [GitHub Issues](https://github.com/joypad-ai/joypad-os/issues) -- Bug reports
- **Source**: [GitHub](https://github.com/joypad-ai/joypad-os) -- Contributions welcome

Joypad OS is licensed under the **Apache-2.0 License**.
