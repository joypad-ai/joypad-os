# Changelog

All notable changes to Joypad OS are documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/).

---

## [2.1.0] — 2026-05-27

### Added

#### New Apps
- **psx2usb** — PlayStation 1 / PlayStation 2 controllers → USB HID. Hardware-paced PIO+DMA SIO transport (500 kHz, active-pull-up to read old analog pads like the SCPH-110 cleanly at fast clock). Auto-detects controller type and decodes: Digital (SCPH-1080), DualShock analog (0x73), DualShock 2 pressure (0x79), neGcon (0x23), Dual Analog flightstick / SCPH-1110 (0x53), Namco GunCon light gun (0x63) with screen X/Y → right stick, Namco JogCon (0xE3) with paddle wheel → left-stick X plus experimental recenter force-feedback, and PlayStation Mouse / SCPH-1090 (0x12) with relative cursor + 2 buttons. Board's user button (BOOTSEL on QT Py / KB2040) emits A1 / Guide while held. Outputs to all USB device modes; SInput reports authentic Sony face-style and per-protocol layout names. Build targets: `psx2usb_qtpy`, `psx2usb_kb2040`, `psx2usb_pico`.
- **gc2eth** — GameCube → Ethernet bridge (W5500 / CH9120) for relaying joybus traffic to Dolphin over TCP. Intercept-replay state machine, STATUS-poll caching, speculative pre-send / WRITE / READ caches (experimental, for Madden multiboot research).
- **joypad-mcp** — MCP server tool for driving an adapter as a synthetic player (vision pipeline, autoplay loop, web control UI, camera pause/resume).
- **joypad-live** — host-side toolkit for live controller remapping and input injection (Twitch crowd-control, streamer overlays, automation). `tools/joypad-live/` ships Python + C# REST bridges with parity tests, a web dashboard, an OBS viewer overlay, `/press` HTTP endpoints, an SSE event feed, a Twitch IRC chat-driven crowd-control bot, and a `restream-bot` unified chat firehose listener. Firmware side adds RAM-only CDC commands so live tweaks don't burn flash with thousands of switches: `PROFILE.APPLY` (button-map override), `PROFILE.SELECT` (profile index override), `OVERLAY.SET / CLEAR / GET` (runtime overlay composed on top of the active profile), and `INPUT.INJECT` (host-side button injection that merges with real controller input).

#### New Output Modes
- **GBA Link** — `gc2usb` `USB_OUTPUT_MODE_GBA_LINK` vendor-bulk transport that exposes the GBA's joybus link over USB to a forked Dolphin (10× faster than TCP); multiboot of payload onto real GBA via `tools/usbgba-multiboot`; verified end-to-end with the joypad GBA-as-controller payload. Behind a CMake opt-in.
- **GameCube GBA-as-controller** — `gc2usb` multiboots Doridian's gba-as-controller payload onto a connected GBA so it becomes a controller, with an animated eyes overlay and per-USB-mode splash text on the multiboot ROM.
- **3DO keyboard and mouse** — `usb23do` routes USB keyboards (ID 0x4B) and mice (ID 0x49) to the 3DO's keyboard/mouse pod outputs.
- **Amiga / Atari DE9 output** — Amiga/Atari CD32 + joystick output driver for XIAO RP2040 (`USB2AMI`, community contribution).

#### New Board Targets
- **HID-Remapper `remapper_v7`** — `usb2usb` dual-RP2040 host+device split board (SWD link on GP28/27, side-channel on GP23/24/25/26; power-cycle after flash is by design). MAX3421E SPI USB host variant (`usb2usb_feather_rp2040_usb_host_max3421`) for the Adafruit USB Host FeatherWing path.
- **`controller_btusb_feather_rp2040_usb_host`** — new target with tri-state pins, USB-host capability, and OLED + I2C peer.
- **`gc2usb_pico`** — Raspberry Pi Pico target with status LED, BOOTSEL button, and USB mode switching (GC data on GP28).
- **`gc2usb_rp2040zero`** — CMake target aligned with the shipped GP29 wiring.
- **`bt2usb_waveshare_rp2350b_plus_w`** — `bt2usb` for the Waveshare RP2350B-Plus-W. Waveshare wires the Raspberry Pi RM2 module to GP36/37/38/39 (REG_ON/DATA/CS/CLK) instead of the Pico 2 W's GP23/24/25/29; a custom board header in `src/boards/headers/` keeps the radio pins right and uses GP23 as LED2 instead of asserting WL_REG_ON. A stock `bt2usb_pico2_w` UF2 does not work on this board.

#### Output & Device
- **Dreamcast VMU emulation** — FT1 / FT3 (and SD-card backed) persistence; gating via `CONFIG_VMU` / `CONFIG_SD`. A freshly-preformatted virtual VMU now drops a default `ICONDATA_VMS` (Joypad OS LOGO_32) in save-area blocks 0-1 so the DC BIOS shows a logo instead of the no-icon placeholder; user saves on SD overlay it as usual. Tool: `tools/vmu/gen_default_icondata.py` to swap the default logo.
- **VMU persistence backend abstraction + QSPI flash** — a `vmu_storage` selector binds one backend by priority (SD card > onboard QSPI > RAM-only). The new opt-in QSPI backend (`CONFIG_VMU_QSPI`) reserves a 128 KB region of the RP2040's onboard flash and saves the card across power cycles via debounced, dirty-sector-only writes (Core-1-safe through `flash_safe_execute`) — no SD card or extra hardware needed. VMU is now **enabled on the KB2040 `usb2dc` target** (previously gated off); it fits the same ~247 KB SRAM as the RP2040-Zero build and gets persistent saves via QSPI.
- **PS3 power-down passthrough** — both PS3 sleep (USB bus suspend) and the PS3's *Settings → Accessory Settings → Turn off controller* menu now propagate to the bridged controller. On suspend, the adapter drops the BT link so a bridged DS4 / DS3 auto-sleeps within ~1 min instead of staying powered forever (PS3 keeps VBUS hot during sleep). The menu trigger is detected as the DS3 `0xF4` feature report with `0x42 0x0C` payload and routes to a weak `app_on_console_shutdown()` callback that `bt2usb` and `usb2usb` (with USB BT dongle) override to drop the BT ACL link (full baseband disconnect, not just the HID profile — DS4 lightbar latched solid otherwise). Closes #145.
- **SD card filesystem** — SD HAL + FatFs filesystem service (PR #1 baseline).
- **OLED menu** — tiny static-table OLED menu (USB Mode / Reboot / Bootloader) for controller-with-display builds.
- **eyes animation** — standalone two-eye animation module with per-button reactions; consumed by `controller_btusb` and `gba-as-controller`.
- **player_leds_gpio** — 4-LED player indicator driven from raw GPIOs (compile-time, opt-in).
- **uart_host** — drains synthetic input via stdio stdin (drops the unused AI inject/blend protocol).
- **CAPS.GET** — web config can query the active app's input/output capabilities.
- **CDC streaming** — single-USB-packet event format cuts streaming latency on the data CDC.
- **Runtime profile / auto-fire** — runtime button mapping and turbo/auto-fire with `usb2neogeo` adoption (community contribution by herzmx, PR #131).

#### Controller & Input
- **Mouse via gamepad** — quadrature-encoded mouse input, scroll wheel, auto-detect from device type, platform switching, per-platform DPI.
- **Xbox One console auth pass-through** (`xbone`) — completes the Xbox One console-side handshake; MAX3421E SPI clock bumped to 16 MHz for chunked-auth headroom; GIP_VIRTUAL_KEYCODE emitted for the Guide button.
- **Original Xbox per-button pressure** — XID (Duke / S-controller) reports analog pressure for A / B / X / Y / Black / White; the `tusb_xinput` parser previously threshold-quantized those bytes away. Now preserved alongside the digital bits and forwarded into the router's `pressure[]` block in canonical W3C / PS slot order. PS3 USB output mode automatically passes them through to the DS3 12-byte pressure block — verified end-to-end on real PS3 hardware. Xbox 360 / One paths unchanged (face buttons are digital on those generations).
- **Switch Pro Joy-Con Charging Grip** — works as a single player (was previously two slots).
- **Sony DS4 (USB)** — radial deadzone instead of per-axis rectangular.

### Changed
- **XInput XSM3** — per-board Xbox 360 security serial derived from the RP2040 chip unique ID (matches USB `iSerialNumber`), with the packet XOR checksum recomputed. Two adapters can now authenticate to one Xbox 360 simultaneously; previously the console accepted the first and rejected the second as a duplicate.
- **SInput feature response** — re-framed as a 64-byte packet with a command-echo byte so SDL/Steam recognize the device; without it the controller was "detected but no buttons in Steam".
- **SInput polling rate** — advertised 1 kHz to match the 1 ms HID endpoint.
- **`gc2usb`** — per-controller hotkeys (instead of global); GBA shoulder-button swap option; persistent d-pad mode; S1+S2 hotkey combos; auto-calibrating L2/R2 rest values with threshold=0 (fixes stuck-on triggers).
- **`bt2usb`** — `REQUIRE_BT_INPUT` defined so fresh boards default to BT host ON.
- **`flash`** — schema-versioned settings + pad config; auto-wipes on schema mismatch instead of mis-applying old data.
- **HID host** — only fetches the USB product string for unknown devices (skip for known VID/PID).
- **NeoPixel power pin** — drives the load-switch via `PICO_DEFAULT_WS2812_POWER_PIN` (e.g., Feather RP2040 P1.14).
- **pico-pio-usb** pinned to `d6c02ac` (pre-tightening); Docker forced to ARM GNU Toolchain 15.2.rel1; Makefile auto-detects the latest ARM GNU install instead of a hard-pinned version.
- **`controller_btusb`** — paged display modes, FeatherWing pin-mapping fix, general hardening; ESP32 / nRF pico-sdk include guards.
- **`pad`** — validate saved config + bound I2C ops + better web defaults.

### Fixed
- **GameCube keyboard** — 3-key rollover + arrow-key D-pad inversion.
- **Dreamcast** — enumeration race condition and VMU write reliability; Core-0 TX workaround restored on KB2040; upstream Core-1 TX config restored on `usb2dc`.
- **Dreamcast analog triggers** — L2/R2 snapped to full the instant they were touched, because the digital L2/R2 bit (set at the "any press" threshold of 1) forced the trigger to 255. The analog level now passes through proportionally; the digital bit only forces full for digital-only pads with no analog trigger axis (e.g. N64 L/R).
- **Router hot path** — `router_submit_input` runs on every USB controller report (~1 kHz on a native HID pad). The CDC input-streaming block was doing all of its prep — player lookup, `get_device_name()` (which reaches into the HID registry and `tuh_vid_pid_get()`), transport-name lookup — *before* calling `cdc_commands_send_player_input`, which already short-circuits when no host is listening. Gate moved to the caller, so on output modes whose USB device is in HOST mode and never enumerates CDC (`usb2gc`, `usb2dc`, etc.) all that prep is skipped — tightens the main-loop iteration for high-precision input scenarios like Melee dash dancing on `usb2gc`.
- **`switch_pro`** — flaky init by handling `0x21` reports and guarding LED OFF spam.
- **`wii_ext`** — neutral report seeded for format 0x03 (Pro default) so initial reads aren't garbage.
- **Router MERGE_BLEND** — analog stick read using local merge buffer (community PR #133, herzmx) plus a separate fix for analog stick reads from the merge buffer.
- **`xbone`** — CI link errors for non-USB-device targets.
- **`gc2usb` GBA Link** — Kawasedo cipher (multiboot) ported verbatim from `eth-multiboot.js`; aggressive cold-start RESET retry (the first joybus RESET after GBA power-cycle fails ~50% of the time); 130 MHz sys_clock set before `tusb_init`; init-order, FIFO sizing, and flow-control hardening.
- **CD32 / Amiga output** — ghost button presses during BOOTSEL reads; LED disconnect detection.
- **`profile`** — don't force L2/R2 threshold when no profile is loaded.
- **`neopixel`** — blink states behind `reset_period` no longer race.
- **`usb2neogeo`** — profile cycling fix; runtime_profile docs.

### Build / CI / Docs
- `esp/nrf` builds — fix unguarded pico-sdk headers and keep platform flash stubs in sync as new `flash_*` setters were declared (`flash_set_dpad_mode`, `flash_set_shoulder_swap`, and the joypad-live ephemeral-state batch: `flash_select_active_profile_index`, `flash_set/get/clear_overlay`, `flash_apply/clear/has_ephemeral_profile`). Each batch was caught after a CI break — `feedback_esp_nrf_flash_stubs` documents the recurring trap + the local audit one-liner that catches it before push.
- `controller_btusb` added to release artifacts on rpi / esp / nrf; `usb2usb_feather_rp2040_usb_host` added to the release matrix; `bt2usb_waveshare_rp2350b_plus_w` added so the Waveshare RM2 board ships its own UF2.
- Unified `docs/usb2gc` build guide covering KB2040 / Pi Pico / RP2040-Zero; corrected bogus pinout claims; "Build" column in adapter tables; "DIY" page surfaces guides.
- **psx2usb hardware build guide** — `docs/hardware/builds/psx2usb-qtpy.md` covers QT Py / KB2040 / Pi Pico wiring, the 9-pin PSX connector pinout, DAT pull-up and rumble-rail notes, build / flash, output-mode walkthrough, and the supported-controller table.
- `tools/dolphin-fork` build instructions for the `joypad-gba-usb` fork.
- `.dev/docs` removed from tracking — internal planning files, now gitignored.
- FUNDING switched to GitHub Sponsors.

### Community contributions
- **herzmx** — runtime mapping + auto-fire profile (PR #131), `usb2neogeo` adoption; MERGE_BLEND race fix (PR #133); `usbh_alt_ps3` driver (PR #132).
- **thgill** — `USB2AMI` Amiga/Atari output + Dreamcast VMU/SD merge (PR #140).

---

## [2.0.0] — 2026-04-18

### Added

#### New Apps
- **bt2wiiext** — Bluetooth controllers → Wii extension port (Classic Controller Pro I2C slave emulation with marcan/Dolphin extension encryption); fully functional in libogc-based homebrew controller tester apps
- **wii2usb / wii2gc / wii2n64** — Wii extension accessories (Nunchuck, Classic, Classic Pro) → USB HID, GameCube, or N64
- **bt2gc / bt2nuon / bt2loopy** — Bluetooth → GameCube, Nuon, and Casio Loopy output (Pico W)
- **nuon2usb** — Read Nuon controllers as USB HID input
- **nuonserial** — Polyface serial adapter for Nuon homebrew development
- **lodgenet2gc / lodgenet2n64** — LodgeNet hotel controllers → GameCube or N64
- **nes2usb** — NES controller → USB HID via PIO (community contribution)
- **jvs2usb** — JVS arcade I/O board → USB HID (community contribution)
- **controller_btusb** — Universal GPIO/JoyWing controller app with simultaneous BLE + USB HID output
- **usb2ble** — USB controllers → BLE gamepad output
- **btusb2usb** — Combined PIO-USB host + CYW43 Bluetooth + USB device on a single Pico W

#### New Platforms
- **Seeed XIAO nRF52840** and **Adafruit Feather nRF52840** — bt2usb and usb2usb targets
- **MAX3421E SPI USB host** — Feather RP2040 + USB Host FeatherWing support
- **Pico 2 W** — bt2n64 and n642dc targets

#### Web Config
- Complete UI redesign with sidebar navigation and dark theme
- **BT Host page** — live scan status, paired device list, per-device forget, transport details
- **USB Host page** — runtime D+ pin configuration for PIO-USB
- **Router page** — routing mode, merge mode, and D-Pad mode adjustable at runtime
- **Profiles page** — create, edit, clone, and delete custom profiles; clone from built-in profiles
- **Hotkeys page** — configure button combo actions
- **Feedback page** — onboard LED toggle, RGB LED pin/count, SInput RGB, buzzer settings
- **Native Output page** — runtime Joybus pin configuration (usb2gc, extensible to other consoles)
- **Device Info** — firmware version check and one-click OTA update via File System Access API
- BLE NUS (Web Bluetooth) wireless configuration transport — configure over Bluetooth without USB
- Dirty-state tracking for save buttons; auto-reconnect after device reboot
- **Input test** — per-player live input stream with device names and smooth RAF batching

#### Controller & Input
- F1/F2 function keys available for hotkey combos
- Configurable hotkey combos: button remap, D-Pad mode cycle, profile next/previous
- Custom profiles now apply uniformly in the router across all outputs
- BLE Central scanning for Bluetooth controllers on Pico W, nRF52840, and ESP32-S3
- Synthesize digital L2/R2 from analog triggers when no built-in profile is present

#### Output & Device
- BLE gamepad output as composite HID device (gamepad + keyboard + mouse)
- Xbox BLE gamepad mode with dual GATT service support
- Generic native-output configuration API (OUTPUT.NATIVE.GET/SET)
- CYW43 onboard LED status patterns: blinking = scanning, solid = connected, off = idle
- **gc2usb** — auto-calibrating stick range scaling (tracks min/max per axis, expands to full 0-255)
- **gc2usb_rp2040zero** — new build target (GC data on GP29, NeoPixel on GP16)
- **gc2usb_pico** — new build target for Raspberry Pi Pico (GC data on GP28; GP29 isn't broken out on standard Pico)
- **Dual Nunchuck mode** — two I2C Nunchucks merged into one input (left stick + right stick, 4 face buttons)
- **Batch flash tool** — `tools/flash-loop.sh` for flashing multiple boards in sequence
- **8BitDo Ultimate BLE button mapping** — dedicated map for controllers with back paddles (VID 0x2DC8, >14 buttons)

### Changed
- **usb2gc / wii2gc** — automatic console detection via GC_DATA pin; Joybus pin overridable at runtime via web config
- **Trigger threshold** — default changed from 128 (50% travel) to 1 (any press)
- libxsm3 converted to a maintained fork (RobertDaleSmith/libxsm3) as a submodule
- Platform HAL extended with GPIO and ADC abstractions for cross-platform pad input
- Flash initialization made idempotent to support early hardware detection paths
- BT scan now suppressed when a USB device is connected; scan duration is timed or indefinite based on context
- Stream throttle state resets on web config page refresh (device names persist)

### Fixed
- N64 pak compatibility with Everdrive, PixelFX Game ID detection, and Cruisin' USA
- N64 cold-boot detection and Core 1 flash-safety hang on Pico 2 W (RP2350)
- Bluetooth generic gamepad analog axis scaling (was 1–255, corrected to 0–255)
- Profile clone from built-in now copies actual button mappings (was copying passthrough for all buttons)
- Custom profile chaining bug where L1→B1 + B1→R3 incorrectly produced L1→R3
- XInput device naming showing "Sony DualShock 3" for Xbox controllers (HID type slots uninitialized)
- BT device names now preferred over generic driver name in input test display
- Input test transport labels: "bt classic" vs "ble" for clarity
- nRF52840: CDC serial hang caused by stack overflow; pad config NVS key conflict; GPIO HAL guard omissions
- MAX3421E SPI hang on boards using SPI1
- NeoPixel data loss on multi-LED chains
- BOOTSEL button reads throttled to prevent blocking flash access and interrupts
- Wii extension support extended to all accessories (Nunchuck, Classic, Classic Pro, and others)
- Wii extension calibration block checksum corrected to Dolphin's 8-bit format (`cal[14] = sum+0x55`, `cal[15] = cal[14]+0x55`); previous 16-bit big-endian sum was rejected by the Wii System Menu, causing it to fall back to internal defaults and mis-map analog axes
- PCEngine docs: added voltage level warning for 5V→3.3V level shifting

### Known limitations

- **bt2wiiext on Wii System Menu** — analog stick direction mapping has unresolved issues specific to the System Menu's cursor logic; the firmware reports correct format-0x01 byte values (verified in libogc-based homebrew controller tester apps) but the System Menu interprets them differently. Buttons and analog triggers work correctly in all tested contexts.

---

## [1.9.0] — 2026-02-25

### Added
- **N64 console output** — new `bt2n64` (Pico W / Pico 2 W) and `usb2n64` (KB2040) apps using joybus-pio N64Console C API (not yet in CI release builds)
- **Nuon console output** — new `bt2nuon` and `n642nuon` apps for Nuon controller output via Polyface protocol (not yet in CI release builds)
- **ESP32-S3 bt2usb support** — BLE controllers to USB HID on ESP32-S3 with TinyUF2 drag-and-drop firmware updates
- **ESP32-S3 bt2usb UF2** added to CI build and release workflow
- **Battery level reporting** for DS3 (USB + BT), DS4/DS5 (via SInput), Switch Pro Controller, and Wii U Pro Controller
- **BLE Battery Service integration** for automatic battery reporting on BLE controllers
- **DS4/DS5 touchpad pass-through** to SInput
- **neogeo2usb** — Neo Geo+ to USB adapter with D-pad mode hotkeys, RP2040-Zero support, and documentation (community contribution by herzmx)
- **Generic HID descriptor-driven Xbox BT driver** — replaces vendor-specific Xbox BT drivers with unified HID parser approach

### Fixed
- **Xbox BT overhaul** — replaced vendor drivers with generic HID descriptor-driven gamepad parsing; fixed button masks, D-pad parsing, Share button, and Elite BT parsing
- **Xbox Classic BT connection timeout** on CYW43
- **Switch Pro BT pairing, reconnection, and analog parsing** on CYW43
- **Sony BT reconnection** on CYW43 dual-path conflict
- **SET_REPORT failing** for Sony controllers on CYW43 direct L2CAP path
- **DS4 clone hanging Pico W** by skipping SDP over CYW43
- **DS5 BT battery offset** (53 → 52) and Sony battery parsing for USB offset and charging states
- **SInput feature report** not updating on BT controller swap
- **L2/R2 pressure missing** for digital-only trigger controllers
- **ESP32 button GPIO** and `tud_task()` blocking on mode switch
- **Generic HID gamepad parsing** for Xbox-style controllers
- **Broken docs links** in README, HARDWARE.md, and INSTALLATION.md
- **PCEngine protocol doc** — fixed 8 factual errors
- **CI cleanup** — delete intermediate firmware artifacts after collect

### Changed
- BLE generic gamepad driver now reuses USB HID parser for consistency

---

## [1.8.0] — 2026-02-15

### Added
- **Generic BLE gamepad detection** via GAP Appearance — auto-connects devices advertising Gamepad (0x03C4) or Joystick (0x03C3) as fallback when no name-based driver matches
- **Xbox BLE rumble support** — GATT HIDS-based output reports with strong/weak motor scaling
- **Microsoft SideWinder Strategic Commander** USB host driver — 90s RTS command controller with tilt X/Y, twist Rz, 12 buttons, 3-position toggle switch, and reactive LED feedback
- **usb2neogeo_pico** and **usb2neogeo_rp2040zero** build targets — Neo Geo adapter support for Pico and RP2040-Zero boards
- **Battery level reporting** for DS4/DS5 via SInput
- **Stadia BT rumble support** and BLE output report path fix
- **Keyboard/Mouse twist axis support** — twist (Rz) axis mapped to delta-based scroll wheel in KB/Mouse mode
- **LED mode color system** — NeoPixel shows color by active USB output mode (white=SInput, green=XInput, blue=PS3/PS4, red=Switch, yellow=KB/Mouse, purple=HID/GC Adapter), pulses when idle, solid on device connect
- **Player LED expansion** from 4 to 7 across all drivers and apps
- **Neo Geo generic GPIO device** — refactored neogeo_device into reusable gpio_device implementation
- **MkDocs Material documentation site** at docs.joypad.ai
- **Web config Vite build** — single-file HTML output with pre-commit auto-build
- **Vercel deployment** for web-config with GitHub Actions workflow
- USB host wiring guide for all supported boards
- Neo Geo RP2040-Zero wiring docs with open drain mode

### Fixed
- **DS4 v2 Bluetooth pairing** — use report mode with boot fallback to bypass SDP parsing failure (status 0x11) on CUH-ZCT2 controllers
- **DS4 BT Sony driver stability** — remove malformed ds4_enable_sixaxis, make output buffers static to fix use-after-free, skip SDP PnP query for Sony devices
- **Xbox BLE input report parsing** — strip HIDS client report ID prefix that shifted all axes and buttons by one byte
- **Switch Pro BT face button mapping** — corrected to match USB driver
- **BT disconnect recovery and BLE reconnection** improvements
- **BT remote name request failure** — handle gracefully in deferred connection flow instead of stalling
- **Analog-to-mouse conversion** — added speed cap and sub-pixel accumulation for smoother cursor movement
- SInput type fix for Switch 2 NSO GameCube controller
- Wii U Pro VID/PID set in driver init for correct SInput device type reporting
- Docs logo visibility for both dark and light themes

### Changed
- Standardized P2–P5 player LED colors to red, green, pink, yellow (PS4-style) across console output apps
- NeoPixel init changed from orange to off to eliminate stale color on boot
- Documentation reorganized: "Console Adapters" renamed to "Firmware Apps"
- Docs domain updated to docs.joypad.ai
- Protocol documentation audited and cleaned up (removed implementation details/code)

---

## [1.7.1] — 2026-02-09

### Added
- **usb2dc_rp2040zero** build target — USB4Maple-compatible Dreamcast adapter (Maple bus on GPIO 14/15), drop-in firmware replacement for existing USB4Maple hardware
- **usb2usb_pico** build target — USB adapter for Raspberry Pi Pico (PIO USB host on GP16/GP17)
- **usb2usb_pico_w** build target — USB adapter for Raspberry Pi Pico W (PIO USB host on GP16/GP17)
- **usb2usb_pico2_w** build target — USB adapter for Raspberry Pi Pico 2 W (PIO USB host on GP16/GP17)
- Dreamcast console documentation with wiring diagrams for KB2040 and RP2040-Zero

### Fixed
- **PS3 console authentication** — DS3 USB output mode now completes the multi-step HID feature report handshake (echo efByte, add GET_REPORT 0xF5 handler, generate non-zero BT addresses from board ID)
- **Wii U Pro Controller BT detection** — defer connection when inquiry name is unavailable, fix late name detection for incoming reconnections
- **XInput host player LED** — was hardcoded to player slot index instead of reading from feedback state
- Maple bus pin defines now overridable via `#ifndef` guards for board-specific pinouts

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

[1.8.0]: https://github.com/joypad-ai/joypad-os/compare/v1.7.1...v1.8.0
[1.7.1]: https://github.com/joypad-ai/joypad-os/compare/v1.7.0...v1.7.1
[1.7.0]: https://github.com/joypad-ai/joypad-os/compare/v1.6.0...v1.7.0
[1.6.0]: https://github.com/joypad-ai/joypad-os/compare/v1.5.0...v1.6.0
[1.5.0]: https://github.com/joypad-ai/joypad-os/compare/v1.4.1...v1.5.0
[1.4.1]: https://github.com/joypad-ai/joypad-os/compare/v1.4.0...v1.4.1
[1.4.0]: https://github.com/joypad-ai/joypad-os/compare/v1.3.0...v1.4.0
[1.3.0]: https://github.com/joypad-ai/joypad-os/compare/v1.2.0...v1.3.0
[1.2.0]: https://github.com/joypad-ai/joypad-os/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/joypad-ai/joypad-os/releases/tag/v1.1.0
