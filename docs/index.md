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

Joypad OS is a modular, high-performance firmware platform for building controller adapters, custom controllers, and input/output bridges across USB, Bluetooth, and native game console protocols. It handles real-time controller I/O, protocol translation, and flexible routing — making it easy to build everything from classic console adapters to next-generation input devices.

---

## Key Capabilities

- **Universal input/output translation** — Convert USB HID devices into native console protocols and vice versa
- **Modular firmware apps** — Build specific bridges like `usb2gc`, `usb2dc`, `snes2usb`, and more on a shared core
- **Flexible routing** — Multi-output controllers, input merging, device chaining, and advanced mods
- **Hardware-agnostic** — Runs on RP2040 today, with future portability to ESP32 and nRF platforms
- **Accessibility-ready** — Enables custom controllers and input extensions for gamers with diverse needs

---

## Getting Started

<div class="grid cards" markdown>

- :material-download: **[Installation Guide](INSTALLATION.md)**

    Flash firmware to your adapter in minutes

- :material-wrench: **[Build Guide](BUILD.md)**

    Set up a development environment and compile from source

- :material-chip: **[Hardware Compatibility](HARDWARE.md)**

    Supported controllers, boards, and DIY builds

</div>

---

## Supported Console Adapters

| Console | Highlights |
|---------|-----------|
| [**3DO**](apps/USB23DO.md) | 8-player support, mouse, extension passthrough |
| [**Dreamcast**](apps/USB2DC.md) | Rumble, profiles |
| [**GameCube / Wii**](apps/USB2GC.md) | Profiles, rumble, keyboard mode |
| [**Casio Loopy**](apps/USB2LOOPY.md) | 4-player (experimental) |
| [**Neo Geo / SuperGun**](apps/USB2NEO.md) | Profiles, 1L6B layouts |
| [**Nuon**](apps/USB2NUON.md) | Controller, Tempest 3000 spinner, IGR |
| [**PC Engine / TurboGrafx-16**](apps/USB2PCE.md) | Multitap (5 players), mouse, 2/3/6-button |

---

## USB & Bluetooth

Joypad OS supports a wide range of **USB controllers** (Xbox, PlayStation, Nintendo Switch, 8BitDo, and generic HID), **Bluetooth controllers** via USB dongle or Pico W, and **USB peripherals** including keyboards, mice, and hubs.

See the [USB2USB](apps/USB2USB.md) and [BT2USB](apps/BT2USB.md) documentation for details on HID Gamepad, XInput, Switch, PS3/PS4, and other output profiles — plus the web configurator at [config.joypad.ai](https://config.joypad.ai).

---

## Protocol Reference

Dive into the low-level console protocols that Joypad OS implements:

- [Protocol Overview](protocols/README.md)
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
