# AGENTS.md

Universal AI agent instructions for working with the Joypad OS codebase. This file is tool-agnostic and works with Claude Code, Cursor, Windsurf, Copilot, Cline, Aider, and other AI coding assistants.

For detailed project architecture, see [CLAUDE.md](CLAUDE.md).

## What is Joypad OS?

Firmware for RP2040, ESP32-S3, and nRF52840 microcontrollers that converts between controller protocols. Plug any USB/Bluetooth controller into retro consoles (GameCube, Dreamcast, PCEngine, Nuon, 3DO, etc.) or use retro controllers on modern systems via USB HID.

## Getting Started

```bash
brew install --cask gcc-arm-embedded cmake git
git clone https://github.com/joypad-ai/joypad-os.git
cd joypad-os
make init          # Initialize submodules
make usb2gc_kb2040 # Build a specific app
make all           # Build all RP2040 targets
make clean         # Clean build directory
```

Build targets follow the pattern: `make <app>_<board>` (e.g., `make usb2gc_kb2040`, `make bt2usb_pico_w`).

Output: `releases/joypad_<commit>_<app>_<board>.uf2`

## Key Conventions

### Naming
- Button constants use `JP_BUTTON_*` prefix (W3C Gamepad API order)
- Old code may reference `USBR_BUTTON_*` or `usbretro` — these are legacy names
- Apps are named `<input>2<output>` (e.g., `usb2gc` = USB input to GameCube output)

### Button Mapping (W3C Order)
```
B1=A/Cross  B2=B/Circle  B3=X/Square  B4=Y/Triangle
L1/R1=Bumpers  L2/R2=Triggers  S1=Select  S2=Start
L3/R3=Stick clicks  DU/DD/DL/DR=D-pad  A1=Home/Guide
```

### Y-Axis Convention
All input drivers MUST normalize to HID standard: **0=up, 128=center, 255=down**.
- Sony/Xbox/8BitDo: Native HID, no inversion needed
- Nintendo controllers: Invert Y (Nintendo uses 0=down, 255=up)
- Native GC/N64 host: Invert Y when reading

### Dual-Core (RP2040)
- **Core 0**: USB/BT polling, input processing, main loop
- **Core 1**: Console output protocols (timing-critical PIO)
- Use `__not_in_flash_func` for timing-critical code on Core 1
- On Pico 2 W (RP2350): Core 0's CYW43 driver periodically locks flash — Core 1 functions must be RAM-only

### PIO
- 32 instruction limit per program — optimize or split
- PIO0/PIO1 assignment matters: CYW43 uses PIO1, PIO-USB uses PIO0 when CYW43 is active
- NeoPixel always claims PIO0 SM — can conflict with PIO-USB

## Architecture Quick Reference

```
Input → router_submit_input() → Router → profile_apply() → Output

Inputs: USB HID, XInput, Bluetooth, WiFi (JOCP), Native (SNES/N64/GC)
Outputs: PCEngine, GameCube, Dreamcast, Nuon, 3DO, Loopy, USB Device, UART
```

### Key Types
- `input_event_t` — Unified input event (buttons bitmap + analog axes)
- `OutputInterface` — Output abstraction (init, core1_entry, task, rumble, LEDs)
- Router modes: SIMPLE (1:1), MERGE (all→one), BROADCAST (all→all)

### Directory Layout
```
src/apps/          — App configs (one per input→output combo)
src/core/          — Shared infrastructure (router, profiles, services)
src/usb/usbh/      — USB host input drivers
src/usb/usbd/      — USB device output
src/bt/            — Bluetooth input drivers
src/native/device/ — Console output protocols (PIO)
src/native/host/   — Native controller input (SNES/N64/GC)
src/pad/           — GPIO controller input (custom builds)
esp/               — ESP32-S3 platform (ESP-IDF)
nrf/               — nRF52840 platform (Zephyr/NCS)
```

## Common Tasks

### Adding a USB Controller Driver
1. Create `src/usb/usbh/hid/devices/vendors/<vendor>/<device>.c/h`
2. Implement: `_is_device()`, `_init()`, `_process()`, `_disconnect()`
3. Register in `hid_registry.c`
4. Map vendor buttons to `JP_BUTTON_*` constants

### Adding a Bluetooth Controller Driver
1. Create `src/bt/bthid/devices/vendors/<vendor>/<device>.c/h`
2. Same interface as USB HID drivers
3. Register in BT device registry

### Adding a New App
1. Create `src/apps/<appname>/` with `app.c`, `app.h`, optional `profiles.h`
2. Add CMake target in `src/CMakeLists.txt`
3. Add Make targets in `Makefile`
4. Add to `APPS` list in Makefile for `make all`
5. Add to `.github/workflows/build.yml` for CI

### Adding a Button Remapping Profile
1. Edit `src/apps/<appname>/profiles.h`
2. Define `profile_t` with button remapping and analog thresholds
3. Profile cycling: SELECT + D-pad Up/Down (2s hold)

## Platform-Specific Notes

### ESP32-S3
- BLE only (no Classic BT) — only BLE controllers work
- `tud_task_ext(1, false)` not `tud_task()` (blocks forever on FreeRTOS)
- BTstack API calls must happen in the BTstack FreeRTOS task
- Classic BT APIs guarded with `#ifndef BTSTACK_USE_ESP32`

### nRF52840
- BLE only — same as ESP32
- BTstack runs in its own Zephyr thread
- TinyUSB owns USB peripheral (Zephyr USB disabled)
- Debug output uses UART (not CDC/USB)
- Classic BT APIs guarded with `#ifndef BTSTACK_USE_NRF`

## Build System

### RP2040 (primary)
- pico-sdk 2.2.0 + CMake under `src/`
- Submodules in `src/lib/` (TinyUSB, BTstack, joybus-pio, tusb_xinput)
- Board variants via Makefile target suffix (e.g., `_kb2040`, `_pico_w`, `_rp2040zero`)

### ESP32-S3
- ESP-IDF v6.0+ under `esp/`
- Requires `~/esp-idf` installation
- Build: `make bt2usb_xiao_esp32s3`

### nRF52840
- nRF Connect SDK v3.1.0+ under `nrf/`
- Setup: `make init-nrf`
- Build: `make bt2usb_seeed_xiao_nrf52840`

## CI/CD

GitHub Actions (`.github/workflows/build.yml`):
- Builds all apps on push to `main`
- Docker-based ARM cross-compilation
- Artifacts in `releases/` directory

## Things to Watch Out For

1. **GameCube requires 130MHz clock** — `set_sys_clock_khz(130000, true)`
2. **PIO state machine conflicts** — NeoPixel vs PIO-USB vs CYW43 all compete for PIO blocks
3. **Flash contention on RP2350** — Core 1 functions must use only inline/RAM-resident calls
4. **Digital-only triggers** — Controllers without analog triggers synthesize analog values in `profile_apply()` so threshold logic works uniformly
5. **HCI handle 0x0000 is valid in BLE** — Use `HCI_CON_HANDLE_INVALID` (0xFFFF) as sentinel, never 0
6. **BTstack run loop** — Custom run loops MUST implement `execute_on_main_thread`
