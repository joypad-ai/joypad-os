# Joypad OS Build Guide

Complete guide for building Joypad OS firmware on macOS, Linux, and Windows.

## Table of Contents

- [Quick Start](#quick-start)
- [Prerequisites](#prerequisites)
- [Building Apps](#building-apps)
- [App Reference](#app-reference)
- [Architecture Overview](#architecture-overview)
- [Troubleshooting](#troubleshooting)

---

## Quick Start

**For macOS users who just want to build:**

```bash
# 1. Install ARM toolchain (one-time)
brew install --cask gcc-arm-embedded cmake git

# 2. Clone and initialize submodules
git clone https://github.com/joypad-ai/joypad-os.git
cd joypad-os
make init

# 3. Build an app
make usb2pce       # or: usb2gc, usb2nuon, usb23do, usb2usb, etc.
```

Output: `releases/joypad_<commit>_<app>_<board>.uf2`

---

## Prerequisites

### macOS

1. **ARM GCC Toolchain** (Official from ARM)
   ```bash
   brew install --cask gcc-arm-embedded
   ```
   - Installs to: `/Applications/ArmGNUToolchain/<version>/arm-none-eabi/`
   - **Do NOT use** `brew install arm-none-eabi-gcc` (missing newlib)

2. **CMake** (3.13+)
   ```bash
   brew install cmake
   ```

3. **Git**
   ```bash
   brew install git
   ```

### Linux (Debian/Ubuntu)

```bash
sudo apt update
sudo apt install cmake gcc-arm-none-eabi libnewlib-arm-none-eabi \
                 build-essential git python3
```

### Windows

#### Option 1: Native Windows (Chocolatey)

1. **Install Chocolatey** (if not already installed):
   - Open PowerShell as Administrator
   - Follow instructions at: https://chocolatey.org/install

2. **Install Required Tools**:
   ```powershell
   choco install make gcc-arm-embedded git
   ```

3. **Add Git Unix Tools to PATH**:
   ```powershell
   # Open PowerShell as Administrator:
   [Environment]::SetEnvironmentVariable("Path", $env:Path + ";C:\Program Files\Git\usr\bin", "User")
   ```
   Close and reopen PowerShell.

4. **Set ARM Toolchain Path**:
   ```powershell
   # Find the ARM toolchain installation path:
   Get-ChildItem "C:\Program Files (x86)" -Filter "arm-none-eabi-gcc.exe" -Recurse -ErrorAction SilentlyContinue

   # Set the environment variable (adjust path based on above output):
   [Environment]::SetEnvironmentVariable("PICO_TOOLCHAIN_PATH", "C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\[version]\bin", "User")
   ```
   Replace `[version]` with your actual version number. Close and reopen PowerShell.

5. **Verify Installation**:
   ```powershell
   make --version
   arm-none-eabi-gcc --version
   rm --version
   ```

6. **Build**:
   ```powershell
   git clone https://github.com/joypad-ai/joypad-os.git
   cd joypad-os
   make init
   make usb2pce    # or other target
   ```

#### Option 2: MSYS2

1. Install MSYS2 from https://www.msys2.org/
2. Open "MSYS2 MINGW64" terminal
3. Install tools:
   ```bash
   pacman -S mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-make git
   ```
4. Follow standard build instructions

#### Option 3: WSL2 (Recommended)

Use WSL2 with Ubuntu and follow Linux instructions above.

#### Windows Troubleshooting

- **"PICO_TOOLCHAIN_PATH not set"**: Ensure the variable points to the `bin` folder containing `arm-none-eabi-gcc.exe`
- **"rm: command not found"**: Ensure `C:\Program Files\Git\usr\bin` is in your PATH
- **PATH changes not taking effect**: Close and reopen PowerShell after modifying environment variables

---

## Building Apps

### Show All Targets

```bash
make help          # Display all available targets
```

### Console Adapter Apps

Convert USB/Bluetooth controllers to retro console protocols:

```bash
make usb2pce       # USB → PCEngine/TurboGrafx-16
make usb2gc        # USB → GameCube/Wii
make usb2nuon      # USB → Nuon DVD players
make usb23do       # USB → 3DO
make usb2loopy     # USB → Casio Loopy (experimental)
make snes23do      # SNES controller → 3DO
```

### USB Output Apps

Convert USB/Bluetooth controllers to USB HID gamepad output:

```bash
make usb2usb                # USB → USB HID (Feather USB Host)
make usb2usb_rp2040zero     # USB → USB HID (RP2040-Zero)
make snes2usb               # SNES controller → USB HID
```

### Custom Controller Apps

Build custom controllers from GPIO/analog inputs:

```bash
make controller_fisherprice        # GPIO buttons → USB HID
make controller_fisherprice_analog # GPIO + analog stick → USB HID
make controller_alpakka            # Alpakka controller (Pico)
make controller_macropad           # MacroPad RP2040 → USB HID
```

### Utility Apps

```bash
make usb2uart      # USB → UART (ESP32 Bluetooth bridge)
```

### Build All

```bash
make all           # Build all apps
make releases      # Build stable release apps only
make clean         # Clean build artifacts
```

---

## App Reference

### Console Adapters

| App | Board | Input | Output | Description |
|-----|-------|-------|--------|-------------|
| `usb2pce` | KB2040 | USB/BT | PCEngine | 5-player multitap, mouse support |
| `usb2gc` | KB2040 | USB/BT | GameCube | Profiles, rumble, keyboard mode |
| `usb2nuon` | KB2040 | USB/BT | Nuon | Spinner, in-game reset |
| `usb23do` | RP2040-Zero | USB/BT | 3DO | 8-player daisy chain, mouse |
| `usb2loopy` | KB2040 | USB/BT | Loopy | 4 players (experimental) |
| `snes23do` | RP2040-Zero | SNES | 3DO | Native SNES controller input |

### USB Output

| App | Board | Input | Output | Description |
|-----|-------|-------|--------|-------------|
| `usb2usb` | Feather USB Host | USB/BT | USB HID | USB gamepad passthrough |
| `usb2usb_rp2040zero` | RP2040-Zero | USB/BT | USB HID | Compact USB passthrough |
| `snes2usb` | KB2040 | SNES | USB HID | SNES to USB adapter |

### Custom Controllers

| App | Board | Input | Output | Description |
|-----|-------|-------|--------|-------------|
| `controller_fisherprice` | KB2040 | GPIO | USB HID | Digital buttons only |
| `controller_fisherprice_analog` | KB2040 | GPIO+ADC | USB HID | With analog stick |
| `controller_alpakka` | Pico | GPIO/I2C | USB HID | Alpakka design |
| `controller_macropad` | MacroPad | 12 keys | USB HID | Macro keypad |

### Supported Boards

| Board | ID | USB Host | Notes |
|-------|-----|----------|-------|
| Adafruit KB2040 | `kb2040` | Yes | Default for most apps |
| Raspberry Pi Pico | `pico` | Yes | Standard RP2040 |
| Waveshare RP2040-Zero | `rp2040zero` | Yes | Compact form factor |
| Adafruit Feather USB Host | `feather_usbhost` | Yes | Built-in USB-A port |
| Adafruit MacroPad RP2040 | `macropad` | No | 12 keys + rotary encoder |

---

## Architecture Overview

### Input Sources

Joypad OS supports multiple input sources:

- **USB HID** - Standard USB gamepads, keyboards, mice
- **USB X-input** - Xbox 360/One/Series controllers
- **Bluetooth HID** - Wireless controllers via BT dongle (`src/bt/`)
- **Native** - Direct controller protocols (SNES via `src/native/host/`)

### Output Targets

- **Retro Consoles** - PCEngine, GameCube, Nuon, 3DO, Loopy (`src/native/device/`)
- **USB Device** - HID gamepad, XInput modes (`src/usb/usbd/`)
- **UART** - Serial bridge for ESP32 Bluetooth (`src/native/device/uart/`)

### Core Components

```
src/
├── apps/                     # App configurations
│   ├── usb2pce/              # PCEngine adapter
│   ├── usb2gc/               # GameCube adapter
│   ├── usb2usb/              # USB passthrough
│   ├── controller/           # Custom controllers
│   └── ...
├── core/                     # Shared infrastructure
│   ├── buttons.h             # JP_BUTTON_* definitions
│   ├── router/               # Input→Output routing
│   └── services/             # Profiles, players, LEDs, etc.
├── usb/
│   ├── usbh/                 # USB Host (input)
│   │   ├── hid/              # HID device drivers
│   │   └── xinput/           # X-input support
│   └── usbd/                 # USB Device (output)
├── bt/                       # Bluetooth support
│   ├── bthid/                # BT HID device drivers
│   └── transport/            # BT transport layer
└── native/
    ├── device/               # Console output protocols
    │   ├── pcengine/
    │   ├── gamecube/
    │   ├── nuon/
    │   ├── 3do/
    │   └── ...
    └── host/                 # Native controller input
        └── snes/
```

### Dual-Core Architecture

- **Core 0**: USB/BT polling, device processing, main loop
- **Core 1**: Console output protocol (timing-critical PIO)

---

## Troubleshooting

### macOS: `nosys.specs: No such file or directory`

Using wrong toolchain. Fix:
```bash
brew uninstall arm-none-eabi-gcc
brew install --cask gcc-arm-embedded
```

### Build fails with SDK errors

Re-initialize submodules:
```bash
make clean
make init
make <target>
```

### Board doesn't enumerate

1. Wrong firmware for board
2. USB cable is charge-only
3. Try different USB port

### Controllers not detected

1. Check [HARDWARE.md](HARDWARE.md) for compatibility
2. Ensure USB hub is powered (if using)
3. Try direct connection without hub

---

## Getting Help

- **Docs**: [README.md](../README.md), [HARDWARE.md](HARDWARE.md)
- **Issues**: https://github.com/joypad-ai/joypad-os/issues
- **Discord**: http://community.joypad.ai/
