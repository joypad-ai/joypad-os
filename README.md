# Joypad OS

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/images/logo_solid.svg">
    <source media="(prefers-color-scheme: light)" srcset="docs/images/logo_solid_black.svg">
    <img alt="Joypad OS" src="docs/images/logo_solid_black.svg" width="300">
  </picture>
</p>
<p align="center">
  <strong>The Open-Source Firmware Platform for Game Controllers and Adapters</strong>
</p>
<p align="center">
  Build controller adapters, custom controllers, and assistive input devices — <br>on microcontrollers, not computers.
</p>
<p align="center">
  <a href="https://github.com/joypad-ai/joypad-os/releases"><img src="https://img.shields.io/github/downloads/joypad-ai/joypad-os/total?style=for-the-badge&label=Downloads" alt="Downloads" /></a>
  <a href="https://github.com/joypad-ai/joypad-os/blob/main/LICENSE"><img src="https://img.shields.io/github/license/joypad-ai/joypad-os?style=for-the-badge" alt="License" /></a>
  <a href="https://github.com/joypad-ai/joypad-os/actions/workflows/build.yml"><img src="https://img.shields.io/github/actions/workflow/status/joypad-ai/joypad-os/.github/workflows/build.yml?style=for-the-badge" alt="CI Status" /></a>
  <a href="https://docs.joypad.ai/"><img src="https://img.shields.io/badge/Docs-docs.joypad.ai-blue?style=for-the-badge" alt="Documentation" /></a>
  <a href="http://community.joypad.ai/"><img src="https://img.shields.io/discord/1133112432684978256?style=for-the-badge&logo=discord&label=Discord" alt="Discord" /></a>
</p>

---

## What is Joypad OS?

Joypad OS is firmware that runs on small microcontroller boards (RP2040, ESP32-S3, nRF52840) — the kind you'd find inside a controller or adapter, not on a PC. You flash it onto a chip and it handles everything: reading controllers, translating protocols, routing inputs to outputs, and managing button remapping.

**What you can build with it:**

- **Controller adapters** — Use any modern USB or Bluetooth controller on retro consoles (GameCube, Dreamcast, PCEngine, 3DO, and more) or convert retro controllers to USB
- **Custom controllers** — Wire up buttons, sticks, and sensors to a microcontroller and get a USB gamepad that works everywhere
- **Assistive gaming setups** — Merge multiple input devices (switches, joysticks, eye trackers) into a single controller output using flexible input routing
- **Wireless adapters** — Turn any Bluetooth controller into a wired USB gamepad for low-latency play

Under the hood, Joypad OS normalizes every input source into a common format, routes it through configurable player slots, and translates it into whatever output protocol the target needs — from retro console protocols bit-banged via PIO to modern USB HID and XInput.

Formerly known as **USBRetro**.

---

## I Have an Adapter — How Do I Update It?

1. **Download** the latest `.uf2` for your adapter from [Releases](https://github.com/joypad-ai/joypad-os/releases)
2. **Enter bootloader**: Hold BOOT + connect USB-C (or double-tap reset)
3. **Drag** the `.uf2` file onto the `RPI-RP2` drive that appears
4. **Done** — the drive ejects and your adapter is running the new firmware

**[Full installation guide](docs/getting-started/installation.md)** — troubleshooting, LED status codes, profile switching

---

## I Want to Build Something — Where Do I Start?

### Quick Start (RP2040)

```bash
# Install ARM toolchain (macOS)
brew install --cask gcc-arm-embedded cmake git

# Clone and initialize
git clone https://github.com/joypad-ai/joypad-os.git
cd joypad-os && make init

# Build an adapter
make usb2gc_kb2040     # USB/BT → GameCube
make usb2usb_feather   # USB/BT → USB HID
make bt2usb_pico_w     # Bluetooth → USB
make snes2usb_kb2040   # SNES controller → USB
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

**[Full build guide](docs/getting-started/building.md)** — prerequisites, all targets, ESP32/nRF setup, troubleshooting

---

## What Can It Do?

### Console Adapters

Use any USB or Bluetooth controller on retro consoles:

| Console | Highlights | Docs |
|---------|-----------|------|
| **PCEngine / TurboGrafx-16** | Multitap (5 players), mouse, 2/3/6-button | [Guide](docs/adapters/pcengine.md) |
| **GameCube / Wii** | Profiles, rumble, keyboard mode | [Guide](docs/adapters/gamecube.md) |
| **Sega Dreamcast** | Rumble, analog triggers, 4 players | [Guide](docs/adapters/dreamcast.md) |
| **Nuon DVD Players** | Controller, spinner (Tempest 3000), IGR | [Guide](docs/adapters/nuon.md) |
| **3DO Interactive Multiplayer** | 8 players, mouse, extension passthrough | [Guide](docs/adapters/3do.md) |
| **Neo Geo / SuperGun** | 7 profiles, 1L6B arcade layouts | [Guide](docs/adapters/neogeo.md) |
| **Casio Loopy** | 4 players (experimental) | [Guide](docs/adapters/loopy.md) |

### USB & Wireless Adapters

| Adapter | What It Does | Platforms | Docs |
|---------|-------------|-----------|------|
| **USB2USB** | USB/BT controller → USB gamepad (XInput, PS3/4, Switch, etc.) | RP2040, nRF52840 | [Guide](docs/adapters/usb.md) |
| **BT2USB** | Bluetooth controller → wired USB gamepad | Pico W, ESP32-S3, nRF52840 | [Guide](docs/adapters/bluetooth.md) |
| **WiFi2USB** | WiFi controller (JOCP) → USB gamepad | Pico W | [Guide](docs/adapters/usb.md) |

### Native Controller Adapters

Convert retro controllers to USB or bridge them to other consoles:

| Adapter | From → To | Docs |
|---------|----------|------|
| **SNES2USB** | SNES/NES → USB | [Guide](docs/adapters/native-input.md#snes-to-usb-snes2usb) |
| **N642USB** | N64 → USB | [Guide](docs/adapters/native-input.md#n64-to-usb-n642usb) |
| **GC2USB** | GameCube → USB | [Guide](docs/adapters/native-input.md#gamecube-to-usb-gc2usb) |
| **NEOGEO2USB** | Neo Geo arcade stick → USB | [Guide](docs/adapters/native-input.md#neo-geo-to-usb-neogeo2usb) |
| **N642DC** | N64 → Dreamcast | [Guide](docs/adapters/native-input.md#cross-console-adapters) |
| **SNES23DO** | SNES → 3DO | [Guide](docs/adapters/native-input.md#cross-console-adapters) |

### Custom Controllers

Wire up GPIO buttons and analog sticks to build your own USB gamepad:

```bash
make controller_fisherprice        # Digital buttons → USB HID
make controller_fisherprice_analog # Buttons + analog stick → USB HID
make controller_macropad           # Adafruit MacroPad → USB HID
```

---

## Supported Input Devices

- **Xbox** — OG, 360, One, Series X|S (USB + Bluetooth)
- **PlayStation** — Classic, DS3, DS4, DualSense (USB + Bluetooth)
- **Nintendo** — Switch Pro, Switch 2 Pro, Joy-Con, NSO GameCube, GameCube adapter (USB + Bluetooth)
- **8BitDo** — PCE 2.4g, M30, NeoGeo, BT adapters
- **Other** — Hori, Logitech, Google Stadia, Sega Astro City, generic HID
- **Peripherals** — USB keyboards, mice, hubs, Bluetooth dongles
- **Native** — SNES, N64, GameCube, Neo Geo controllers (directly wired)

**[Full controller compatibility list](docs/hardware/controllers.md)**

---

## How It Works

```
Any Input                        Router                       Any Output
─────────                        ──────                       ──────────
USB controllers ──┐                                           ┌──→ Retro consoles
Bluetooth ────────┤                                           ├──→ USB gamepad
WiFi ─────────────┼──→  normalize  →  route  →  translate  ──┼──→ XInput / PS4 / Switch
SNES / N64 / GC ──┤         ↓                                ├──→ Keyboard + mouse
GPIO buttons ─────┘    profile_apply()                        └──→ Custom outputs
                    (button remapping)
```

- **Router** — SIMPLE (1:1), MERGE (all→one), or BROADCAST (all→all) modes
- **Profiles** — Per-app button remapping, cycled via SELECT + D-pad
- **Dual-Core** — Core 0 handles input, Core 1 handles timing-critical output (RP2040)
- **PIO** — Programmable I/O for console protocols (GameCube joybus, Dreamcast maple, etc.)

**[Architecture overview](docs/architecture/overview.md)** | **[Layers & internals](docs/architecture/layers.md)**

---

## Platforms

| Platform | Chip | What Runs On It | Bluetooth | Build System |
|----------|------|----------------|-----------|--------------|
| **RP2040** | ARM Cortex-M0+ (dual-core) | All console adapters, USB output, custom controllers | Classic BT + BLE (Pico W) or via dongle | pico-sdk |
| **ESP32-S3** | Xtensa LX7 (dual-core) | bt2usb | BLE only | ESP-IDF |
| **nRF52840** | ARM Cortex-M4 | bt2usb, usb2usb | BLE only | nRF Connect SDK |

---

## Documentation

| Section | What's There |
|---------|-------------|
| **[Installation Guide](docs/getting-started/installation.md)** | Flashing firmware, troubleshooting, LED codes |
| **[Build Guide](docs/getting-started/building.md)** | Dev setup for macOS/Linux/Windows, all build targets |
| **[Supported Controllers](docs/hardware/controllers.md)** | USB, Bluetooth, keyboards, mice, dongles |
| **[Supported Boards](docs/hardware/boards.md)** | RP2040, ESP32-S3, nRF52840 board comparison |
| **[Wiring Guide](docs/hardware/wiring.md)** | USB host port + console connector pinouts |
| **[DIY Builds](docs/hardware/diy.md)** | Build your own, where to buy parts |
| **[ESP32-S3](docs/platforms/esp32.md)** | ESP-IDF setup, TinyUF2, architecture |
| **[nRF52840](docs/platforms/nrf52840.md)** | nRF Connect SDK setup, debugging |
| **[Architecture](docs/architecture/overview.md)** | How it works — data flow, router, I/O layers |
| **[Layers & Internals](docs/architecture/layers.md)** | Developer deep dive — interfaces, router pipeline, latency |
| **[Protocol Reference](docs/protocols/)** | 3DO PBus, GameCube Joybus, Nuon Polyface, PCEngine |

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
