# Joypad OS

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/images/logo_solid.svg">
    <source media="(prefers-color-scheme: light)" srcset="docs/images/logo_solid_black.svg">
    <img alt="Joypad OS" src="docs/images/logo_solid_black.svg" width="300">
  </picture>
</p>
<p align="center">
  <img src="https://img.shields.io/github/license/joypad-ai/joypad-os" />
  <img src="https://img.shields.io/github/actions/workflow/status/joypad-ai/joypad-os/.github/workflows/build.yml" />
</p>

**Universal controller firmware for adapters, controllers, and input systems.**

Joypad OS translates any input device into any output protocol. Connect USB, Bluetooth, or WiFi controllers to retro consoles, or bridge native retro controllers to USB. It runs on RP2040, ESP32-S3, and nRF52840 microcontrollers.

Formerly known as **USBRetro**.

---

## What Joypad OS Does

- **Any input → any output** — USB HID, XInput, Bluetooth (Classic + BLE), WiFi, and native SNES/N64/GameCube controllers as inputs. PCEngine, GameCube, Dreamcast, Nuon, 3DO, Neo Geo, and USB as outputs.
- **Multi-platform** — Runs on RP2040 (console adapters, USB output), ESP32-S3 (BLE to USB), and nRF52840 (BLE to USB, USB host via MAX3421E).
- **Flexible routing** — 1:1, merged, or broadcast input-to-output mapping. Multi-player support up to 8 players.
- **Configurable** — Button remapping profiles, USB output mode switching, web configurator at [config.joypad.ai](https://config.joypad.ai).

---

## Supported Adapters

### Console Adapters

| Console | Highlights | Documentation |
|---------|-----------|---------------|
| **PCEngine / TurboGrafx-16** | Multitap (5 players), mouse, 2/3/6-button | [Docs](docs/adapters/pcengine.md) |
| **GameCube / Wii** | Profiles, rumble, keyboard mode | [Docs](docs/adapters/gamecube.md) |
| **Sega Dreamcast** | Rumble, analog triggers, 4 players | [Docs](docs/adapters/dreamcast.md) |
| **Nuon DVD Players** | Controller, spinner (Tempest 3000), IGR | [Docs](docs/adapters/nuon.md) |
| **3DO Interactive Multiplayer** | 8 players, mouse, extension passthrough | [Docs](docs/adapters/3do.md) |
| **Neo Geo / SuperGun** | 7 profiles, 1L6B arcade layouts | [Docs](docs/adapters/neogeo.md) |
| **Casio Loopy** | 4 players (experimental) | [Docs](docs/adapters/loopy.md) |

### USB & Bluetooth Adapters

| Adapter | Input | Output | Platforms | Documentation |
|---------|-------|--------|-----------|---------------|
| **USB2USB** | USB/BT controllers | USB HID gamepad | RP2040, nRF52840 | [Docs](docs/adapters/usb.md) |
| **BT2USB** | Bluetooth controllers | USB HID gamepad | Pico W, ESP32-S3, nRF52840 | [Docs](docs/adapters/bluetooth.md) |
| **WiFi2USB** | WiFi controllers (JOCP) | USB HID gamepad | Pico W | [Docs](docs/adapters/usb.md) |

### Native Input Adapters

| Adapter | Input | Output | Documentation |
|---------|-------|--------|---------------|
| **SNES2USB** | SNES/NES controller | USB HID gamepad | [Docs](docs/adapters/native-input.md#snes-to-usb-snes2usb) |
| **N642USB** | N64 controller | USB HID gamepad | [Docs](docs/adapters/native-input.md#n64-to-usb-n642usb) |
| **GC2USB** | GameCube controller | USB HID gamepad | [Docs](docs/adapters/native-input.md#gamecube-to-usb-gc2usb) |
| **NEOGEO2USB** | Neo Geo arcade stick | USB HID gamepad | [Docs](docs/adapters/native-input.md#neo-geo-to-usb-neogeo2usb) |
| **N642DC** | N64 controller | Dreamcast | [Docs](docs/adapters/native-input.md#cross-console-adapters) |
| **SNES23DO** | SNES controller | 3DO | [Docs](docs/adapters/native-input.md#cross-console-adapters) |

---

## Supported Input Devices

- **Xbox** — OG, 360, One, Series X|S (USB + Bluetooth)
- **PlayStation** — Classic, DS3, DS4, DualSense (USB + Bluetooth)
- **Nintendo** — Switch Pro, Switch 2 Pro, Joy-Con, NSO GameCube, GameCube adapter (USB + Bluetooth)
- **8BitDo** — PCE 2.4g, M30, NeoGeo, BT adapters
- **Other** — Hori, Logitech, Google Stadia, Sega Astro City, generic HID
- **Peripherals** — USB keyboards, mice, hubs, Bluetooth dongles

**[Full controller compatibility list](docs/hardware/controllers.md)**

---

## How It Works

```
Input Sources → Router → Output Targets

USB/BT/WiFi/Native controllers are normalized into a common format,
routed through configurable player slots, and translated into the
output protocol. Console outputs run on RP2040 Core 1 using PIO
state machines for cycle-accurate timing.
```

- **Router** — SIMPLE (1:1), MERGE (all→one), or BROADCAST (all→all) modes
- **Profiles** — Per-app button remapping, cycled via SELECT + D-pad
- **Dual-Core** — Core 0 handles input, Core 1 handles timing-critical output (RP2040)
- **PIO** — Programmable I/O for console protocols (GameCube joybus, Dreamcast maple, etc.)

**[Architecture overview](docs/architecture/overview.md)**

---

## Platforms

| Platform | Chip | Apps | Bluetooth | Build System |
|----------|------|------|-----------|--------------|
| **RP2040** | ARM Cortex-M0+ (dual-core) | All console + USB adapters | Classic BT + BLE (Pico W) or via dongle | pico-sdk |
| **ESP32-S3** | Xtensa LX7 (dual-core) | bt2usb | BLE only | ESP-IDF |
| **nRF52840** | ARM Cortex-M4 | bt2usb, usb2usb | BLE only | nRF Connect SDK |

---

## For Users: Updating Firmware

### Quick Flash (RP2040)

1. **Download** latest `.uf2` from [Releases](https://github.com/joypad-ai/joypad-os/releases)
2. **Enter bootloader**: Hold BOOT + connect USB-C (or double-tap reset on some boards)
3. **Drag** `.uf2` file to `RPI-RP2` drive
4. **Done!** Drive auto-ejects when complete

**[Full installation guide](docs/getting-started/installation.md)**

---

## For Developers: Building Firmware

### Quick Start (RP2040)

```bash
# Install ARM toolchain (macOS)
brew install --cask gcc-arm-embedded cmake git

# Clone and initialize
git clone https://github.com/joypad-ai/joypad-os.git
cd joypad-os && make init

# Build specific adapter
make usb2pce_kb2040     # PCEngine adapter
make usb2gc_kb2040      # GameCube adapter
make usb2usb_feather    # USB passthrough
make bt2usb_pico_w      # Bluetooth to USB (Pico W)
```

### ESP32-S3

```bash
make init-esp                   # One-time ESP-IDF setup
make bt2usb_esp32s3             # Build
make flash-bt2usb_esp32s3       # Flash
```

### nRF52840

```bash
make init-nrf                               # One-time NCS setup
make bt2usb_seeed_xiao_nrf52840             # Build
make flash-bt2usb_seeed_xiao_nrf52840       # Flash
```

Output: `releases/joypad_<commit>_<app>_<board>.uf2`

**[Full build guide](docs/getting-started/building.md)**

---

## Documentation

### Getting Started
- **[Installation Guide](docs/getting-started/installation.md)** — Flashing firmware
- **[Build Guide](docs/getting-started/building.md)** — Developer setup

### Adapters
- **[Console Adapters](docs/adapters/pcengine.md)** — PCEngine, GameCube, Dreamcast, Nuon, 3DO, Neo Geo, Loopy
- **[USB Output](docs/adapters/usb.md)** — USB output modes, web config, Xbox 360 support
- **[Bluetooth](docs/adapters/bluetooth.md)** — BT2USB wireless adapter
- **[Native Input](docs/adapters/native-input.md)** — SNES/N64/GC/Neo Geo to USB

### Hardware
- **[Supported Controllers](docs/hardware/controllers.md)** — USB, Bluetooth, keyboards, mice
- **[Supported Boards](docs/hardware/boards.md)** — RP2040, ESP32-S3, nRF52840 boards
- **[Wiring Guide](docs/hardware/wiring.md)** — USB host port + console connectors
- **[DIY Builds](docs/hardware/diy.md)** — Build your own adapter

### Platforms
- **[ESP32-S3](docs/platforms/esp32.md)** — ESP-IDF setup, TinyUF2, architecture
- **[nRF52840](docs/platforms/nrf52840.md)** — nRF Connect SDK setup, debugging

### Reference
- **[Architecture Overview](docs/architecture/overview.md)** — How it works
- **[Layers & Internals](docs/architecture/layers.md)** — Developer reference: interfaces, router, dual-core, latency
- **[Protocol Reference](docs/protocols/)** — 3DO PBus, GameCube Joybus, Nuon Polyface, PCEngine

---

## Community & Support

- **Discord**: [community.joypad.ai](http://community.joypad.ai/) - Community chat
- **Issues**: [GitHub Issues](https://github.com/joypad-ai/joypad-os/issues) - Bug reports
- **Email**: support@controlleradapter.com - Product support

---

## Acknowledgements

- [Ha Thach](https://github.com/hathach/) - [TinyUSB](https://github.com/hathach/tinyusb)
- [David Shadoff](https://github.com/dshadoff) - [PCEMouse](https://github.com/dshadoff/PC_Engine_RP2040_Projects/tree/main/PCEMouse) foundation
- [FCare](https://github.com/FCare) - [USBTo3DO](https://github.com/FCare/USBTo3DO) 3DO protocol implementation
- [mackieks](https://github.com/mackieks) - [MaplePad](https://github.com/mackieks/MaplePad) Dreamcast Maple Bus implementation
- [Ryzee119](https://github.com/Ryzee119) - [tusb_xinput](https://github.com/Ryzee119/tusb_xinput/)
- [SelvinPL](https://github.com/SelvinPL/) - [lufa-hid-parser](https://gist.github.com/SelvinPL/99fd9af4566e759b6553e912b6a163f9)
- [JonnyHaystack](https://github.com/JonnyHaystack/) - [joybus-pio](https://github.com/JonnyHaystack/joybus-pio)
- [OpenStickCommunity](https://github.com/OpenStickCommunity) - [GP2040-CE](https://github.com/OpenStickCommunity/GP2040-CE) USB device output modes

---

## License

Joypad OS is licensed under the **Apache-2.0 License**.

The **Joypad** name and branding are trademarks of Joypad Inc.
