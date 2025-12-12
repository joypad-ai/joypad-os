# Joypad Core

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/images/logo_solid.svg">
    <source media="(prefers-color-scheme: light)" srcset="docs/images/logo_solid_black.svg">
    <img alt="Joypad Core" src="docs/images/logo_solid_black.svg" width="300">
  </picture>
</p>
<p align="center">
  <img src="https://img.shields.io/github/license/joypad-ai/joypad-core" />
  <img src="https://img.shields.io/github/actions/workflow/status/joypad-ai/joypad-core/.github/workflows/build.yml" />
</p>

**Universal controller firmware foundation for adapters, controllers, and input systems.**

Joypad Core is a modular, high-performance firmware platform for building controller adapters, custom controllers, and input/output bridges across USB, Bluetooth, and native game console protocols.

Formerly known as **USBRetro**, this project now serves as the foundational firmware layer of the **Joypad** ecosystem — a universal platform for extending how controllers work, connect, and evolve.

Joypad Core focuses on real-time controller I/O, protocol translation, and flexible routing, making it easy to build everything from classic console adapters to next-generation, assistive, and AI-augmented input devices.

---

## What Joypad Core Enables

- **Universal input/output translation** — Convert USB HID devices into native console protocols and vice versa.
- **Modular firmware apps** — Build specific bridges like `usb2usb`, `usb2gc`, `snes2usb`, passthrough adapters, merged inputs, and hybrid devices — all on a shared core.
- **Flexible routing & passthrough** — Support multi-output controllers, input merging, chaining devices, and advanced mods.
- **Hardware-agnostic foundation** — Designed to run across RP2040 today, with future portability to ESP32 and nRF platforms.
- **Foundation for accessibility & assistive play** — Enables custom controllers and input extensions for gamers with diverse needs.

Joypad Core is the real-time nervous system of the Joypad platform.

---

## Supported Outputs

| Output | Features | Documentation |
|--------|----------|---------------|
| **USB Device** | HID Gamepad, XInput, Xbox OG, Xbox One, PS3, PS4, PS Classic, Switch | [Docs](docs/BUILD.md) |
| **PCEngine / TurboGrafx-16** | Multitap (5 players), Mouse, 2/3/6-button | [Docs](docs/consoles/PCENGINE.md) |
| **GameCube / Wii** | Profiles, Rumble, Keyboard mode | [Docs](docs/consoles/GAMECUBE.md) |
| **Nuon DVD Players** | Controller, Spinner (Tempest 3000), IGR | [Docs](docs/consoles/NUON.md) |
| **3DO Interactive Multiplayer** | 8 players, Mouse, Extension passthrough | [Docs](docs/consoles/3DO.md) |
| **Casio Loopy** | 4 players (experimental) | [Docs](docs/consoles/LOOPY.md) |

---

## Products

Pre-built adapters available at **[controlleradapter.com](https://controlleradapter.com)**:

- **[USB-2-PCE](https://controlleradapter.com/products/usb-2-pce)** - PCEngine/TurboGrafx-16 adapter
- **[GC USB](https://controlleradapter.com/products/gc-usb)** - GameCube/Wii adapter with profiles
- **[NUON USB](https://controlleradapter.com/products/nuon-usb)** - Nuon DVD player adapter

---

## Supported USB Devices

**Controllers:**
- Xbox (OG/360/One/Series X|S)
- PlayStation (Classic/DS3/DS4/DualSense)
- Nintendo Switch (Pro/Joy-Cons)
- 8BitDo (PCE 2.4g, M30, Adapters)
- Generic HID gamepads

**Peripherals:**
- USB Keyboards (full HID support)
- USB Mice (PCEngine mouse, Nuon spinner, 3DO mouse)
- USB Hubs (up to 8 devices for 3DO)

**[Complete hardware compatibility list](docs/HARDWARE.md)**

---

## For Users: Updating Firmware

### Quick Flash

1. **Download** latest `.uf2` from [Releases](https://github.com/joypad-ai/joypad-core/releases)
2. **Enter bootloader**:
   - **USB-2-PCE / NUON / 3DO**: Hold BOOT + connect USB-C
   - **GC USB**: Just connect USB-C (no button)
3. **Drag** `.uf2` file to `RPI-RP2` drive
4. **Done!** Drive auto-ejects when complete

**[Full installation guide](docs/INSTALLATION.md)**

---

## For Developers: Building Firmware

### Quick Start

```bash
# Install ARM toolchain (macOS)
brew install --cask gcc-arm-embedded cmake git

# Clone and initialize
git clone https://github.com/joypad-ai/joypad-core.git
cd joypad-core && make init

# Build specific product
make usb2gc         # GameCube adapter
make usb2pce        # PCEngine adapter
make usb2nuon       # Nuon adapter
make usb23do        # 3DO adapter
```

### Build Commands

```bash
make init          # Initialize submodules (one-time setup)
make all           # Build all products
make clean         # Clean build artifacts

# Build specific products
make usb2pce       # USB2PCE (KB2040 + PCEngine)
make usb2gc        # USB2GC (KB2040 + GameCube)
make usb2nuon      # USB2Nuon (KB2040 + Nuon)
make usb23do       # USB23DO (KB2040 + 3DO)
make usb2loopy     # USB2Loopy (KB2040 + Casio Loopy)
make snes23do      # SNES23DO (SNES→3DO bridge)
```

Output firmware files appear in `releases/` directory.

**[Complete build guide](docs/BUILD.md)**

---

## Documentation

- **[Installation Guide](docs/INSTALLATION.md)** - Flashing firmware, troubleshooting
- **[Hardware Compatibility](docs/HARDWARE.md)** - Supported controllers, boards, DIY builds
- **[Build Guide](docs/BUILD.md)** - Developer setup, architecture

### Console-Specific Guides

- **[GameCube/Wii](docs/consoles/GAMECUBE.md)** - Profiles, keyboard mode, rumble
- **[PCEngine/TurboGrafx-16](docs/consoles/PCENGINE.md)** - Multitap, mouse, button modes
- **[Nuon](docs/consoles/NUON.md)** - Controller, Tempest 3000 spinner, IGR
- **[3DO](docs/consoles/3DO.md)** - 8-player support, mouse, profiles
- **[Casio Loopy](docs/consoles/LOOPY.md)** - Experimental support

---

## Architecture

Joypad Core uses a modular architecture:

- **RP2040** - Dual-core ARM Cortex-M0+ microcontroller
- **TinyUSB** - USB host stack for polling USB devices
- **PIO** - Programmable I/O for timing-critical console protocols
- **Dual-Core**: Core 0 handles USB input, Core 1 runs console output
- **Router** - Flexible input→output routing (SIMPLE/MERGE/BROADCAST)
- **Apps** - Per-product configuration (router, profiles, features)

**See [CLAUDE.md](CLAUDE.md) for detailed architecture**

---

## Community & Support

- **Discord**: [discord.joypad.com](https://discord.joypad.com/) - Community chat
- **Issues**: [GitHub Issues](https://github.com/joypad-ai/joypad-core/issues) - Bug reports
- **Email**: support@controlleradapter.com - Product support

---

## Acknowledgements

- [Ha Thach](https://github.com/hathach/) - [TinyUSB](https://github.com/hathach/tinyusb)
- [David Shadoff](https://github.com/dshadoff) - [PCEMouse](https://github.com/dshadoff/PC_Engine_RP2040_Projects/tree/main/PCEMouse) foundation
- [FCare](https://github.com/FCare) - [USBTo3DO](https://github.com/FCare/USBTo3DO) 3DO protocol implementation
- [Ryzee119](https://github.com/Ryzee119) - [tusb_xinput](https://github.com/Ryzee119/tusb_xinput/)
- [SelvinPL](https://github.com/SelvinPL/) - [lufa-hid-parser](https://gist.github.com/SelvinPL/99fd9af4566e759b6553e912b6a163f9)
- [JonnyHaystack](https://github.com/JonnyHaystack/) - [joybus-pio](https://github.com/JonnyHaystack/joybus-pio)

---

## License

Joypad Core is licensed under the **Apache-2.0 License**.

The **Joypad** name and branding are trademarks of Joypad Inc.
