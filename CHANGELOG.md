# Changelog

All notable changes to Joypad OS are documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/).

---

## [Unreleased]

*No unreleased changes.*

---

## [1.7.0] — 2026-02-09

### Added
- **Xbox 360 console authentication (XSM3)** — adapters now authenticate with Xbox 360 consoles via XInput mode
- **PC Engine Mini USB output mode** — emulates HORI PCEngine PAD (VID 0x0F0D / PID 0x0138) for PC Engine Mini / TG-16 Mini consoles, with turbo fire support (10/15/20 Hz)
- **SInput USB host driver** — full-fidelity controller passthrough for SInput-compatible devices
- **SInput composite USB device** — gamepad, keyboard, and mouse interfaces in a single device
- **SNES rumble support** via LRG protocol
- **SNES d-pad mode toggle** and Home button combo in SNES host driver
- **Debug log streaming** over data CDC instead of separate debug port
- **Flash dual-sector journal** for BT-safe settings persistence with `flash_save_force()` for pre-reset saves
- LGPL-2.1 compliance for libxsm3 (modification notice, attribution, THIRD_PARTY_LICENSES)
- USB output interface documentation with web config and Xbox 360 details
- Neo Geo added to README with links to USB output docs and web config

### Fixed
- **TRIGGER_LIGHT_PRESS** now caps analog proportionally at all trigger values — fixes SSBM light shield being all-or-nothing (PR #68)
- **SInput host report parsing** off-by-one — memcpy destination was shifting all fields by one byte, causing SInput devices to be misidentified as DirectInput
- **XSM3 auth routing** so Xbox 360 console authentication actually works end-to-end
- **DS5 USB lightbar** RGB not reflecting feedback system colors
- **DS4 lightbar** feedback — set default player LED colors on assignment
- **DS3 gyro/accel** normalized to SInput convention for consistent IMU output
- **3DO profile switching** combo detection
- SSBM profile: L2 digital threshold set to 0 so light shield never produces a digital press
- Skip log ring buffer writes when debug streaming is off (performance)

### Changed
- XInput product string changed to "Xbox 360 Controller" for better host compatibility

### Docs
- Neo Geo: latency test results and diagram (PR #67, community contribution by @herzmx)
- PC Engine: clarified pinout naming (CLR vs OE) and code variable mapping
- Updated wiring diagram images for NGC-2-USB, USB-2-3DO, USB-2-NGC

---

## [1.6.0] — 2026-02-04

### Added
- Microsoft SideWinder Dual Strike USB HID driver with hat D-pad/analog mode toggle
- ANALOG_RZ as 7th analog axis for twist/spinner inputs
- Full shoulder button and stick click mappings to keyboard input
- **SInput IMU passthrough** with dynamic motion capability reporting
- **SInput player LED support** for controller identification
- SInput auto-sends feature report on controller connect

### Fixed
- Bluetooth pairing regression for DualSense and other gamepads (Wiimote COD detection was too broad)
- XInput feedback latency — added change detection and throttle
- Input-to-output latency — disabled debug logging, gated BTstack loop, reordered main loop
- Disabled chatpad keepalive until chatpad support is functional
- SInput feature response now matches 24-byte spec with proper input device type detection

### Changed
- Removed duplicate HID_KEY_* defines from kbmouse.h (uses TinyUSB's definitions)

### Performance
- Router: reduced input_event copies for tap-based outputs
- Neo Geo: push-based output via router tap for lower latency

---

## [1.5.0] — 2026-02-02

### Added
- **WiFi controller input** via JOCP protocol (`wifi2usb` app) — connect Joypad iOS app wirelessly
- **WiFi pairing mode** with keyboard controls for test client
- **BLE beacon** for iOS WiFi SSID discovery
- **Neo Geo output** (`usb2neogeo`) — community-contributed adapter support (PR #60)
  - Docs, profiles (default + fighting), button mapping
- **SInput USB output mode** as new default HID output
- **SInput feature response** and RGB LED passthrough to WiFi controllers
- **SOCD cleaning modes** added to custom profiles and web config UI
- **GameCube Adapter USB output mode** — emulates official GC adapter over USB
- Feedback visualization in test client (rumble, player LED, RGB LED)
- Extra PS3/PS4 controller VID/PIDs (mainly fight sticks)
- User-contributed wiring diagrams for USB-2-GC and USB-2-3DO
- GP2040-CE to acknowledgements

### Fixed
- GC button mapping: A=B2, B=B1 (matches gc_host input convention)
- Trigger threshold: 0 now correctly means "disabled"
- Light shielding: removed xinput trigger threshold, added output-side threshold
- TRIGGER_LIGHT_PRESS: analog only, no digital + fixed L2/R2 mapping
- GC Adapter: fixed rumble output, status byte cleanup, extended HID descriptor for 4 ports
- GC Adapter: use 0 for analog values on unconnected ports
- Profile threshold overrides for input L2/R2 digital
- Button label inconsistencies in docs (GAMECUBE.md, NEOGEO docs)
- `MAX_OUTPUTS` bumped to 12 — Neo Geo addition pushed UART out of bounds

### Changed
- Unified trigger mapping: L2/R2 for all triggers, R1 for Z buttons
- Refactored USB device output: extracted modes and drivers
- `MAX_OUTPUTS` now derived from `OUTPUT_TARGET_COUNT` enum
- Disabled TinyUSB debug logging by default (add `.env` for local overrides)

---

## [1.4.1] — 2026-01-17

### Fixed
- PCEngine analog-to-dpad Y-axis mapping

### Changed
- CI: build matrix for parallel app builds with auto-detected CPU cores
- CI: use PAT token for version bump push
- Updated joybus-pio submodule with GamecubeController C implementation
- Updated CLAUDE.md with new apps and native hosts

---

## [1.4.0] — 2026-01-16

### Added
- **Dreamcast output** (`usb2dc`) — Maple Bus protocol with Puru Puru rumble support
- **N64 controller input** (`n642usb`, `n642dc`) — native N64 controller as USB HID or Dreamcast adapter
  - Dual stick profile for right-stick C-buttons
  - Rumble pak auto-init and feedback
- **GameCube controller input** (`gc2usb`) — native GC controller to USB HID adapter
- **Nintendo Wii U Pro Controller** Bluetooth support with reconnection and player LEDs
- **Nintendo Wiimote** Bluetooth support — motion, Nunchuk, Classic Controller, Classic Controller Pro
  - Accelerometer-based orientation detection
  - Extension hot-swap support
  - Guitar Hero Wii guitar extension
  - Rumble passthrough
- **Waveshare RP2350A USB-A** board support
- **CDC binary protocol** and web config tool for runtime configuration
  - Profile editor, input test, rumble test, BOOTSEL command
  - Unified profile API (built-in + custom profiles)
  - Device name tracking and PLAYERS.LIST command
- **RP2350 support** — BOOTSEL button fix and flash storage
- **Journaled flash storage** for reliable settings persistence
- Wiimote orientation hotkeys (D-pad Left for auto orientation)
- Triple-click button to reset to HID mode
- L2/R2 as standard HID analog axes for DInput compatibility
- Raphnet PCEngine to USB adapter support
- GitHub Actions workflow to deploy web config to Pages

### Fixed
- USB2GC regression from PIO sharing changes (joybus-pio)
- Switch 2 BLE device name showing generic/truncated name
- Switch 2 GameCube controller rumble/LED initialization
- GameCube analog stick range: clamped to 1–255
- Core 1 synchronization: `__wfe`/`__sev` instead of `__wfi`
- Wii U Pro Controller reconnection with direct L2CAP sending
- Release workflow to use VERSION instead of commit hash
- Build warnings: guard against macro redefinitions
- bt2usb_pico_w: added ENABLE_BTSTACK for CDC Wiimote commands
- Removed call to custom BTstack function causing build failures
- N64 host: removed incorrect analog trigger values from L/R
- N64 host: send cleared input on disconnect to prevent stuck buttons

### Changed
- Excluded usb2loopy, snes23do from builds until more mature
- Router: increased MAX_OUTPUTS to 10 for UART target
- Dreamcast: configurable Core TX mode per app for PIO compatibility
- Simplified button mode cycle to 5 common modes
- Standardized analog array format to contiguous 6 elements
- Unified USB output mode switching across apps
- Renamed waveshare_rp2350a to rp2350usba for consistent board naming
- Updated 3DO docs with level shifter requirements

---

## [1.3.0] — 2025-12-28

### Added
- **Nintendo Switch 2 Pro Controller** BLE (Bluetooth Low Energy) support

### Fixed
- XInput Y-axis inversion

### Changed
- CI: reuse build artifacts in release job instead of rebuilding

---

## [1.2.0] — 2025-12-23

This was a massive release — the biggest in Joypad OS history. It represents the transformation from a collection of single-purpose adapters into a unified, modular firmware platform.

### Added

#### Bluetooth Input (Major)
- **Full Bluetooth stack** via BTstack (replaced old BTD stack entirely)
  - Classic BT HID Host — DS3, DS4, DS5 Bluetooth support
  - BLE HID — Xbox Series, Stadia, Switch 2 Pro controller support
  - TinyUSB HCI transport for USB BT dongles
  - SMP pairing, ATT/GATT/HOGP layers
  - SDP VID/PID query for device identification
  - Broadcom dongle compatibility
- **BT2USB app** for Pico W with built-in Bluetooth
- **Google Stadia** controller support (BLE)
- **Nintendo Switch 2 Pro** controller driver (USB + BLE), extending USB HID to 18 buttons
- User button hold to clear all BT bonds

#### USB Output Modes (Major)
- **Xbox Original (XID)** USB device output with mode switching
- **XInput** (Xbox 360/One compatible) output
- **PlayStation 3** output with SHANWAN VID/PID for DInput compatibility
- **PlayStation 4** output with authentication passthrough via connected DS4
- **PlayStation Classic** output
- **Nintendo Switch** output with position-based button mapping
- **Xbox Adaptive Controller (XAC)** compatible output mode
- **Xbox One** authentication passthrough
- **Xbox 360 chatpad** support
- **PIO USB host** support for Adafruit Feather RP2040 USB Host board

#### Architecture Overhaul (Major)
- **Universal router system** — N:M input-to-output mapping with routing tables
- **App/product layer** — each adapter is now a self-contained app (`usb2pce`, `usb2gc`, `usb2dc`, etc.)
- **InputInterface** abstraction for modular input handling
- **OutputInterface** pattern with standardized naming
- **Universal profile system** with per-player switching, flash persistence, and multi-modal LED feedback
  - 4-profile system with button combo switching
  - LED profile indicator state machine
  - Fighting game, SSBM, and custom profiles for GameCube
- **Unified input event system** with 8-axis analog support
- **Transport type system** for BT/USB player isolation
- **Canonical feedback system** for per-player rumble and LED
- **Configurable player management** system

#### Controller Features
- Switch Pro rumble passthrough (HD Rumble encoding via OGX-Mini format)
- Switch Pro player LED passthrough
- DS3 Bluetooth with rumble, player LED, and pressure-sensitive button passthrough
- DS4 Bluetooth reconnection and SSP pairing
- DS4/DS5 touchpad left/right detection as L4/R4 buttons
- DualSense adaptive triggers decoupled via GameCube profile system
- Motion data passthrough for DS3/DS4/DS5
- Joy-Con Grip instance merging at device driver level
- Stick modifier system for button-triggered sensitivity changes
- Event-driven USB output with Pico W LED status indicators
- Exclusive combo support

#### Console Output Improvements
- **3DO** — full console support with 8-player PBUS protocol, profile system, extension detection, silly pad mode for JAMMA
- **PCEngine** — 6-button mode fix with FIFO-synchronized state cycling, analog stick to D-pad mapping, mouse fix
- **Nuon** — spinner input decoupled from device drivers
- Loopy — restored with app layer
- SNES2USB app with native SNES input
- SNES23DO app for native SNES/NES controller to 3DO

#### Hardware & Build
- RP2040-Zero board support (usb2usb_rp2040zero with BOOTSEL button)
- MacroPad RP2040 support (OLED, speaker/buzzer, per-key NeoPixel, UART link via QWIIC)
- I2C expander support and Alpakka controller configs
- GPIO input interface for universal controller app (custom DIY controllers)
- Fisher Price Analog target with D-pad toggle switch
- Konami code detection easter egg

#### Documentation & Branding
- **Renamed USBRetro → Joypad** (codebase, buttons, docs)
- **Renamed joypad-core → joypad-os**
- Rebranded README with ecosystem context and dark/light mode logos
- Comprehensive 3DO PBUS, GameCube Joybus, PCEngine, and Nuon protocol documentation
- W3C Gamepad API standard button ordering
- ASCII controller diagram in buttons.h
- Windows build instructions
- Dual USB CDC support for data and debug channels

### Fixed
- BLE controller reconnection for non-advertising devices
- BT disconnect: clear held buttons and free player slots
- USB device output flickering and BT driver selection
- PS3 SIXAXIS neutral value (512 for zero pitch/roll)
- PS3 output report parsing for WebHID report ID offset
- L1/R1/L2/R2 button mapping in PSC output interface
- Right stick Y axis reading
- XInput trigger-to-button threshold (100 → 16)
- Dual-core flash write crash using flash_safe_execute
- Switch Pro ZL/ZR trigger detection for third-party controllers
- R button drift release with atomic report updates
- 3DO protocol timing and PIO resource conflicts
- Sticky buttons: process all events from registered players
- CI artifact naming and organization

### Changed
- Y-axis standardized to HID convention (0=up, 128=center, 255=down)
- Internal button representation changed from active-low to active-high
- Complete codebase reorganization (transport-based architecture)
- Removed old BTD Bluetooth stack (BTstack exclusively)
- Removed CONFIG_* conditional compilation in favor of app layer
- Removed DragonRise from supported devices
- Removed Xbox One S from supported consoles
- CI artifacts changed from board-based to app-based organization
- App-based build system with refactored CMakeLists.txt

---

## [1.1.0] — 2025-11-17

Initial tagged release. Represents the modernization of the firmware with a proper build system and CI/CD pipeline.

### Added
- **Automated CI/CD** — GitHub Actions with Docker builds, matrix strategy for all boards
- **Automated releases** for USB2PCE, GCUSB, and NUONUSB
- pico-sdk as git submodule for self-contained builds
- macOS build support
- `make flash` commands for easy firmware deployment
- Version tracking with commit hash in firmware names
- Docker layer caching for faster CI builds

### Fixed
- GameCube communication with pico-sdk 2.2+
- TinyUSB compatibility (updated to 0.19.0)
- XInput library restored after SDK compatibility issues
- Switch Pro analog-to-dpad translations
- Switch mode controller compatibility improvements
- Nuon spinner output with mice detected as DInput devices
- PCEngine mouse on multitap (Lemmings detection fixes)

### Changed
- Modernized build system with pico-sdk submodule workflow
- Updated GitHub Actions to v4
- Standardized firmware release naming
- Repository structure reorganization

### Supported at Release
**Input:** Xbox 360/One/Series, PS3/PS4/PS5, Switch Pro, Joy-Con, 8BitDo (PCE/M30/Neo), Hori, Logitech, keyboards, mice, USB hubs  
**Output:** PCEngine/TG16 (5-player), GameCube/Wii, Nuon, 3DO (8-player), Casio Loopy, USB HID  
**Boards:** KB2040, Raspberry Pi Pico, RP2040-Zero

---

[Unreleased]: https://github.com/joypad-ai/joypad-os/compare/v1.6.0...HEAD
[1.6.0]: https://github.com/joypad-ai/joypad-os/compare/v1.5.0...v1.6.0
[1.5.0]: https://github.com/joypad-ai/joypad-os/compare/v1.4.1...v1.5.0
[1.4.1]: https://github.com/joypad-ai/joypad-os/compare/v1.4.0...v1.4.1
[1.4.0]: https://github.com/joypad-ai/joypad-os/compare/v1.3.0...v1.4.0
[1.3.0]: https://github.com/joypad-ai/joypad-os/compare/v1.2.0...v1.3.0
[1.2.0]: https://github.com/joypad-ai/joypad-os/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/joypad-ai/joypad-os/releases/tag/v1.1.0
