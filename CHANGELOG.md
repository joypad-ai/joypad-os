# Changelog

All notable changes to Joypad OS are documented in this file.

Format based on [Keep a Changelog](https://keepachangelog.com/).

---

## [Unreleased]

### Added

#### 24g2usb
- **First radio input source: 8BitDo SF30 2.4G wireless receiver over nRF24L01+.** New `24g2usb` app drives an nRF24L01+ over SPI, impersonating the SF30 2.4G's OEM USB dongle closely enough that controllers pair and cold-link directly — no 8BitDo dongle needed. The receiver runs off the radio's IRQ line and a hardware alarm rather than the main polling loop, so other core-0 work (flash writes, LED updates) can't stall a dwell and drop a packet. Supports exactly one controller by design — USB output only ever surfaces a single player (same limitation `bt2usb` already carries), and two controllers hopping the same table at independent phase can starve each other's dwell indefinitely. Hold BOOTSEL ~1.5s to pair a controller. Boards: Pico 2 W, Pico W, Pico, Pico 2. See [24g2usb](docs/apps/24g2usb.md), [24G input](docs/input/24g.md), and the [24G protocol reference](docs/protocols/24g.md) (recovered by logic-analyser capture of the OEM dongle's SPI bus).

---

## [2.4.1] — 2026-08-16

Patch release. **Every adapter running 2.4.0 should update**: the universal profile hotkeys added in
2.4.0 made a long-standing settings-corruption bug reachable on 43 of the 45 apps, from a gesture the
2.4.0 notes tell users to press.

### Fixed

#### Saved settings and profiles

- 🔴 **Switching profiles could scramble every saved setting.** `profile_save_to_flash()` declared a
  256-byte `flash_t` on the stack, assigned exactly one field, and wrote the whole thing to flash —
  so the other 255 bytes were uninitialized stack. `flash_save()` stamps the magic and schema
  version itself, so the garbage record passed both load-time checks and came back as real settings:
  USB/BLE output mode, routing and merge mode, D-pad mode, shoulder swap, Wiimote orientation, BT
  scanning, the native-output pin overrides, and all four custom profiles. The save now routes
  through the live settings copy and touches only the field it means to. (#216, #217)
- 🔴 **An adapter that already carries a corrupt record now repairs itself on load.** Settings sectors
  live at the top of flash and a UF2 does not erase them, so the writer fix alone left every
  already-affected device broken — installing the fix changed nothing for them. Incoherent records
  are now sanitized once on load instead of being trusted. (#222)
- **Settings reads and writes go through the live runtime copy, not a stack copy of the flash
  record.** The Wiimote orientation hotkey (Plus + D-pad) and `WIIMOTE.ORIENT.SET` did a
  read-modify-write against the on-flash record, which silently reverted any other setting changed
  since the last save. (#221, #217)
- **The default SELECT/START combos now need a ~0.7 s hold.** In 2.4.0 they matched on the first
  frame the buttons were seen together, so a normal SELECT + D-pad press in a game could switch
  profiles or flip the D-pad slider. The hold is measured on whichever edge the controller actually
  sends: while still held once it elapses, or on the release if the pad only reports *changes* (the
  USB HID gamepad path submits nothing during a static hold, so the timer cannot advance mid-hold on
  those pads). A quick tap reaches neither and passes through. Apps with their own combo tables are
  unaffected. (#243)
- **…and the hold now belongs to the controller that started it.** The combo table has one hold
  timer per combo, not one per device, and every input event from every device ran through it — so
  on any adapter with two or more controllers connected, a second player simply playing the game
  cleared player 1's timer on each of their own events. The hold could never reach 0.7 s and all
  five built-in hotkeys were unreachable; single-controller testing passes either way. The timer now
  records which device opened it and ignores everyone else's events. Disconnecting a pad mid-hold
  also clears its timer, instead of leaving a stale timestamp that fired the next press instantly.
  (#249)
- **D-pad mode 3 (L-stick ↔ R-stick) survives a reboot.** The 4th position of 2.4.0's D-pad slider
  applied live but was rejected by the flash setter, so it silently reset on every power cycle.
  (#242)
- 🔴 **…and selecting that slider position no longer wipes your router settings.** The write side
  above was only half the fix: the load-time sanitizer kept its own copy of the valid range and still
  clamped the D-pad mode at 2, so every boot reverted it. Worse, a clamp marks the record as one that
  "was never written deliberately", and that branch also clears the saved-router flag — which gates
  the entire restore block — so the reboot after touching the slider *also* dropped the persisted
  **shoulder swap** and **un-hid every built-in profile** disabled in the web config, on all 45 apps.
  The bound is now a single constant shared by the setter and the sanitizer. Genuinely out-of-range
  values are still caught. (#250)

#### GameCube

- **`usb2gc` / `wii2gc` recover from a missed console detect instead of latching config mode.** Play
  vs. config mode was decided by a single `gpio_get()` 200 ms after boot and never re-checked, so one
  bad sample stranded the adapter on the orange LED with no console output until it was replugged in
  the right order. Detection now re-checks and self-corrects. (#241, likely relevant to #164/#165)
- **Answer the analog poll mode the console actually asked for.** Bytes 4–7 of the joybus reply
  change meaning per requested mode; the mode byte was read and discarded, so games asking for a
  non-default mode got the wrong analog fields. (#206)
- **Keyboard-mode toggle works on 65% keyboards.** The toggle now matches as a key set and adds a
  **Ctrl+Alt+K** chord, for boards that have neither Scroll Lock nor an F13–F24 row. (#236,
  discussion #220)

#### Controllers and output

- **Xbox triggers work on output modes with no analog trigger.** Switch, PS3 and the other
  digital-only output modes now derive digital L2/R2 from the analog value, instead of dropping the
  triggers entirely. (#98, #123, #152, #208)
- **N64 host stick uses the correct ±84 reference deflection.** Scaling against 80 clipped the top of
  the range, making the five furthest raw positions indistinguishable. (#211, #225)
- **`MERGE_BLEND` no longer drops 9 fields.** It is the only routing path that rebuilds the merged
  event by hand rather than assigning it, and nine fields were never copied across. (#234, #235)
- **`BT.STATUS` lists bonded Classic controllers.** A powered-off DS4/DS5/Switch Pro/Wiimote appeared
  nowhere in the web config's bond list, because the list was built from BLE Security Manager events
  only — while `BT.FORGET` needed an address that only that list could produce, making the delete
  path unreachable for Classic pads. (#230, #231)
- **PS4 output writes the accelerometer rest value to Z**, not into a reserved byte. (#232)
- **DualShock 4 / DualSense: a released touch no longer sticks.** The host-side "did this report
  change?" test looked only at finger 1 — and on DS4 only at its coordinates, not its down bit — so
  lifting the second finger, or a finger whose coordinates hadn't moved, produced no new report and
  the touch point stayed active. Both fingers' down bit and position are now part of the diff.
- **Factory reset erases Bluetooth bonds too.** `SETTINGS.RESET` erased only the settings sector,
  but BLE and Classic bonds live in a separate flash bank and survived — contradicting `flash.h`,
  which documents the call as erasing "all stored data (settings, bonds, pad config)". A factory
  reset from the web config is now a genuine clean slate on BT builds.
- **The web config's "Enable Bluetooth Host" toggle no longer reads *off* on adapters whose whole job
  is Bluetooth.** `ROUTER.GET` reported the persisted `bt_input_enabled` flag on every build, but the
  dedicated BT bridges (`bt2usb`, `bt2gc`, `bt2n64`, `bt2nuon`, `bt2loopy`, …) never read that flag —
  they always run the radio. On a fresh flash the byte is zero, so config.joypad.ai showed Bluetooth
  disabled on a board that was actively scanning for controllers. `CAPS.GET` now carries a `bt_host`
  capability pair mirroring `usb_host` (*present* = the stack is compiled in, *configurable* = the app
  honours the runtime flag), and always-on bridges report `bt_input: true` with a read-only toggle.
  Only `controller_btusb` and `bt2wiiext` actually honour the flag, and both stay user-settable.

### Added

- **iPega PG-9021 Classic Bluetooth gamepad driver**, wired into the RP2040, ESP32-S3 and nRF builds.
  (#239, thanks @Atreus171)

### Changed

- **`bt2usb` now defaults to SInput output, like every other adapter.** It was the only build that
  overrode `USBD_DEFAULT_MODE`, left at PS4 from the PS4 local-auth work, so a blank board came up
  as a DualShock 4 instead of an SInput pad. **If your bt2usb board has never had its output mode
  changed, it will come up in SInput after this update** — triple-click BOOTSEL is still the way
  back to SInput, and double-click still cycles. A board with a mode saved in flash is unaffected;
  the saved mode still wins. This also means a fresh bt2usb board is reachable from
  config.joypad.ai out of the box, which PS4 mode (no CDC interface) blocked.

### Build & CI

- 🔴 **Released firmware reports its own version correctly again.** Every 2.4.0 UF2 self-reported
  `2.3.0`, and every 2.3.0 UF2 reported `2.2.0` — in the web config, in the boot banner, and in the
  USB device descriptor, so there was no surface on which a user could confirm which build they were
  running. The release workflow bumped `VERSION` in a new commit but the build jobs did not depend on
  that job, so they compiled the tree from just before the bump. The builds now check out the bump
  commit, and the run fails outright if the tree being compiled does not carry the version being
  released — the original failure produced no error at all, which is how it survived two releases.
  (#245, #246; thanks mitsuschi for the report)
- Every driver the BTHID/device registries reference is now compiled, and CI fails if one is
  referenced but not built. (#233)
- `controller_btusb` builds for the **Seeed XIAO nRF52840**. (#219)
- `APPS` / `RELEASE_APPS` list one app per line, so adding a target is a purely additive diff. (#244)

### Documentation

- `app.h` manifests no longer advertise feature flags the build never reads — several of them stated
  the opposite of what the firmware does. (#198, #199)
- **usb2gc build guide:** removed the dead GPIO 6 "console-presence sense wire" instruction, which
  described a detection mechanism the firmware stopped using in April; documented how detection
  really works and added bt2gc. (#237)
- **psx2usb:** LED claims scoped per board, config-tool pointer corrected, real button combos
  documented. (#240)
- Build-guide controls, UF2 filenames and the output-mode cycle corrected across the guides. (#238)
- **bt2usb build guide:** the three places it told users a fresh board comes up in PS4 mode now say
  SInput, with a note for anyone updating from 2.4.0.

---

## [2.4.0] — 2026-08-14

### Added

#### Valve Steam Controller 2 (codename "Triton")
- **Full native support over USB and Bluetooth LE.** Disables Valve "lizard mode" (keyboard/mouse emulation) and streams the native gamepad state: all face/shoulder buttons, both analog sticks, Hall-effect analog triggers, the **L5/R5 lower back paddles**, and the **dual trackpads** over both transports (the SC2's pads only wake once a host subscribes to its HID service — handled on the BLE path). Reports **battery level + charging state** and drives **rumble** over USB and BT. Presents to the router as a **DualSense (PS5) SInput layout** so it renders correctly in the config UI and PS-style visualizers.
- **IMU (accel + gyro) over USB** — motion is enabled via a settings write and streamed at the SDL-canonical ranges (±2000 dps / ±2 g).

#### Valve Steam Controller 1 (original)
- **USB support** — wireless dongle (`28DE:1142`) and wired (`28DE:1102`). Disables lizard mode and parses the native vendor report: face/shoulders, analog triggers (with a partial-pull digital threshold), left stick, an **8-way D-pad derived from the left trackpad**, stick/pad clicks, grips, and **right trackpad → right analog stick**.
- **Bluetooth LE support** via the community nRF51822 BLE firmware. Brings up the shared Valve GATT service, forces a **fast connection interval** (the firmware otherwise requests a slow ~1 s interval that throttled input to a drip and stalled on a button burst), and decodes the delta-compressed BLE report. Because BLE carries the stick and left pad as **separate fields**, the left analog stick and the left-pad-D-pad work fully **independently** — an isolation the multiplexed USB report can't provide.

#### Output modes
- **PS4 output — motion + touchpad passthrough.** PS4/PS5 USB-output mode now emits the router's gyro/accel and **both touch points** (scaled to the DS4 pad, IMU frame-corrected) instead of dropping them, so a controller with motion/trackpads (e.g. the Steam Controllers) drives them through to the console.
- **Wake a sleeping PC over USB.** Remote wakeup is now reachable on **every** output mode — a held input wakes the host. SInput mode (which exposes a keyboard interface) can wake Windows/macOS; XInput cannot, as it has no keyboard interface for the host to arm wakeup.

#### Configuration & web config
- **Lower paddles and aux buttons are now fully configurable.** Custom profiles cover the whole button set through **L5/R5** (bits 0–25) — the stored profile map grew from 18 to 26 slots — so the SC2's lower back paddles (plus A3/A4/L4/R4) can be remapped, disabled, or turbo'd like any other button. The web-config profile editor and hotkeys list them, autofire timing covers them, and the input-test page now shows **L5/R5** activity.

#### Off-console web config (console-output adapters)
- **usb2pce and usb2gc now expose the web config off-console.** Plugged into a PC with no console attached, they boot as a USB **CDC** device for the web config (edit/switch profiles, view status) instead of a controller host. The mode is chosen at boot from the console's control lines — GameCube's 3.3 V rail, or the PC Engine's **SEL/CLR clock activity** — and a console powered on *with the adapter already attached* self-corrects into play mode the instant it starts scanning (the cold-boot race is handled by a runtime watch that reboots into play mode). In config mode the USB output-mode list is limited to **CDC** only.
- **USB Host page for native-USB adapters.** The web config shows a read-only **USB Host** page for adapters that host controllers on the RP2040's native USB (usb2pce/usb2gc) — presence is advertised via `CAPS.usb_host`, with the pin fixed by hardware (unlike PIO-USB controller apps, whose pin stays editable).
- **True I/O on the Info and Router pages.** A console adapter in config mode now reports its real topology — e.g. **USB Host → PCEngine · up to 5 players**, with a `USB Host → PCEngine` route — instead of the USB-CDC transport, via a new `native_input`/`native_output` `CAPS.native` object.

#### Web-config profiles
- **Per-button turbo (auto-fire), web-configurable.** Custom profiles gain a ⚡ turbo toggle per button plus one shared rate (30/20/15/12/10/7.5 Hz). The router applies it before the remap (so the pulse follows the mapping), cloning a built-in preserves its compiled auto-fire, and joypad-live drives it over the same `PROFILE.*` CDC commands.
- **Per-profile device/output mode.** Custom profiles carry a generic, app-declared output mode; usb2pce exposes **2-Button / 6-Button / 3-Button (Sel) / 3-Button (Run)** as a "PCEngine Mode" dropdown (`PROFILE.MODES` reports each app's modes), so a cloned "6-Button" plays in 6-button mode instead of falling back to 2-button.
- **Disable built-in profiles.** An Enable/Disable toggle per built-in profile — disabled ones are skipped by the SELECT+D-pad profile cycle but stay directly selectable. Cloning the **Default** profile is now allowed too.

#### usb2pce
- **Button mode and turbo now live in profiles.** The 2-/3-/6-button PC Engine mode and its per-mode turbo are built-in profiles selected by hotkey or web config (retiring the RUN+D-pad mode toggle), and the global on-the-fly rapid-fire works on PCEngine output as well.

### Changed
- **Universal profile hotkeys on every app.** `router_init()` now installs the profile/tuning hotkeys by default, so they work regardless of app: **SELECT + D-pad Up/Down** switches profiles instantly and clamps at the ends; **SELECT + D-pad Left/Right** is a 4-position D-pad↔stick swap slider (`[D-pad↔L-stick] [normal] [D-pad↔R-stick] [L-stick↔R-stick]`); **START + D-pad Up** toggles shoulder swap. On-the-fly config gestures are **SELECT + B3** (rapid-fire set — tap a button to cycle its rate) and **SELECT + B4** (live remap); neither uses START, so they don't collide with console reset combos. Apps with their own combo tables still take over on their first `router_set_combo()`.
- **Canonical touchpad normalization.** All touchpad sources (DualShock 4, DualSense, both Steam Controllers) are normalized into a device-agnostic 0–65535 space in the router, and output modes scale from there — so trackpads carry through to any output instead of being handled ad hoc per driver.
- **Flash schema v2.** The custom-profile button map expanded (18 → 26 slots) so L4/R4/F1/F2/L5/R5 become remappable. The struct stays 56 bytes (reusing former reserved space), but reinterpreting those bytes forces a **one-time wipe of saved settings/profiles** on upgrade.

### Fixed
- **SC2 Bluetooth reconnect reliability** — defer Valve service discovery until the GATT client is ready (a bonded reconnect fired it too early and hung at `VID:0000`); a bring-up watchdog that disconnects and retries on a stalled state; a forced DIS read to identify a name-less SC2 stuck on the generic HID path; a shorter connect-attempt timeout so a stale/rotated-address bond can't monopolize the radio with scanning off; and scanning resumes after clearing bonds.
- **SC2 View/Menu (S1/S2)** mapping in the USB parser corrected to match the BLE/SDL layout.
- **DualSense USB touchpad** is now normalized to the canonical 0–65535 space (matching the BT path) instead of passing raw pixel coordinates, so PS4/PS5-output touchpad passthrough is correctly scaled.
- **D-pad → analog-stick diagonals** now follow the circular stick radius instead of hitting the square corners, so a diagonal reads as a real stick deflection.
- **Nuon spinner axis** — restored the spinner (paddle) axis that was dropped in the router migration, so Nuon output drives the spinner again.
- **Bluetooth driver link safety** — the BTHID device-driver registry could be discarded by the linker's `--gc-sections` (RP2040, ESP32, nRF), silently dropping BT controller support; the registry is now link-guarded, and `CONFIG_BT_HOST` is defined for manual-BT Pico apps (a #188 regression).
- **Waveshare RP2350-USB-A LED colors** — the onboard WS2812 is an RGB WS2812B (not RGBW/GRB), so status and player-LED colors rendered wrong; the byte order and `IS_RGBW` are now board-scoped correctly, leaving every other board untouched (#218, thanks @Atreus171).
- **D-pad mode hotkey + persistence now work on every app.** The SELECT+D-pad d-pad-output-mode toggle (D-pad → left/right stick) and its save/restore-across-reboot were only wired into `gc2usb`/`controller_btusb` — every other app (`usb2usb`, `bt2usb`, …) registered no combos, so the hotkey did nothing and the saved mode never came back. The router now installs the SELECT+D-pad hotkeys by default and restores the saved d-pad mode (and shoulder-swap) on boot for all apps; apps with their own combo tables take over on their first `router_set_combo()` and are unaffected (gap diagnosed via #207, thanks @daveq86).

### Build & CI
- Release build matrix expanded to include **psx2usb**, **gc2usb_pico**, and **jag2usb**.

---

## [2.3.0] — 2026-08-04

### Added

#### Companion Face (AMOLED)
- **Procedural face engine** — a new companion "face" that renders animated eyes on an AMOLED panel (ESP32-S3), driven live over CDC or Bluetooth. Runs alongside the controller stack so a single device is both a pairable gamepad and an expressive face.
- **Full emotion matrix** — all 11 emotions rendered in each of the face styles (**Astro** LED-lattice, **eyes**, **lil**, **tab**), with **true shape morphing** between emotions via a distance-field blend on a fixed 280 ms smoothstep, plus continuous shadow gradients and per-dot rendering for smooth transitions.
- **FACE.\* command surface** — `FACE.EMO` (emotion), `FACE.STYLE` (switch styles at runtime), `FACE.COLOR` (runtime display tint), `FACE.BRIGHT` (brightness), `FACE.LOOK` (gaze direction), `FACE.OFFSET` (position trim), `FACE.STATE` (query), `FACE.SPEAK`, and `FACE.TRACK` / `FACE.TRACK.GO` (pre-shipped lip-sync played on the face's own clock). Commands are accepted over the CDC config port **and relayed over BLE NUS** to untethered faces (with self-recovery: cleanup, re-arm, and a wedge watchdog).
- **Web config — Face page** — drive the companion face (emotion, style, gaze pad) directly from the browser over CDC/BLE.
- **New board target** — `bt2usb` on the **LilyGo T-Display S3 AMOLED Plus** with animated eyes; `controller_btusb` also runs on the AMOLED face board so the eyes present as a pairable controller.

#### PS4 authentication (USB output)
- **DS4 local authentication** for PS4/PS5 USB-output modes — upload a DS4 auth key from the browser (folded into the USB Device web-config page). RSA challenge signing runs on **Core 1** (never blocking Core 0), and USB-output apps clock to **200 MHz** so signing lands inside the console's auth window. Rumble/LED output is captured from the interrupt OUT endpoint. Builds on RP2350; auth flash is stubbed on ESP/nRF. Merged from lucaslealdev's DS4 work.
- **1000 Hz continuous reporting** in PS4 mode.
- **Full DS4 v2 HID descriptor** — the report structure was rewritten for precision, fixing compatibility with EA Sports titles (FC26).
- **Hybrid triggers** — L2/R2 report binary for fighting games and analog for sports titles, with a threshold to stop noise ghosting.
- **Touchpad-click simulation** via button combos — Select+Start for a click, D-pad modifiers for left/right clicks, and Start+R1 (chosen to avoid triggering Share screenshots).
- **PS4 auth key upload tool** — `tools/ps4-auth-upload/`, plus a `PS4AUTH` CDC command and PS4 auth flash storage service.

#### New Apps
- **jag2usb** — native **Atari Jaguar** controller → USB HID. Reads the passive Jaguar switch matrix directly at 3.3 V (no level shifters), with the 12-key keypad emitted as gamepad buttons (via `aux_buttons`) rather than a HID keyboard. Builds for `jag2usb_pico` / `jag2usb_pico_w`.

#### New Controller Support
- **SInput over BLE** — a BLE HID (HOGP) driver that reads a JoypadOS SInput controller (`controller_btusb` running on nRF / ESP32 / Pico W) and submits it to the router. This is the BLE counterpart to the existing USB `sinput_host` driver, so a JoypadOS controller can now feed a JoypadOS adapter wirelessly.

#### New Board Targets
- `bt2usb_lilygo_tdisplay_s3_amoled` — the AMOLED companion-face board (16 MB partition layout + board sdkconfig).
- `controller_btusb_seeed_xiao_nrf52840`.
- `jag2usb_pico` and `jag2usb_pico_w`.

#### Platform & Tooling
- **ESP32 power telemetry** — real VBUS presence and battery level read from the charger PMU, behind a shared `core/battery.h` abstraction.
- **nRF IMU support** — `imu_nrf.c`, motion for nRF52840 controller builds.
- **nusprobe** — a BLE NUS command-line tool for talking to OS-paired devices.
- **BLE OTA page** — `tools/ble-ota.html` for over-the-air updates, plus `tools/joypad-ble.py`.

#### Experimental (opt-in, off by default)
- **DualSense drop-scream** — IMU free-fall detection on a DS5 paired over BT, playing speaker audio through the controller (extended BT output report `0x36`: state + haptic PCM + speaker Opus). Compiled only under `CONFIG_DS5_DROP_SCREAM`; asset-encoding tooling lives in `tools/ds5-scream/`.

### Fixed

#### Bluetooth
- **Pairing reliability** — coexistence-safe connection params, zombie-link cleanup, and a scan-state LED. Bonded BLE devices now re-pair while Classic BT is up, and the face advertises on USB.
- **Recovery watchdogs** — dropped the RSSI-liveness watchdog and stopped the recovery watchdog's stealth reboots; idle reconnect now recovers after non-reboot drops (with a `BLE.DROP` bench command for testing). Scoped USB dominance and NUS remote management added.
- **DS5 companion builds** — can now connect to JoypadOS faces.

#### ESP32
- **BLE bonds persist** — removed the RAM device-db that was shadowing the TLV-backed one (bonds were lost across reboot).
- **PMU I2C stack overflow** — cache the PMU I2C read instead of running it in the BTstack task, which overflowed its stack.
- **Deterministic CDC BOOTSEL** — hand the USB PHY back before download so JTAG/CDC bootsel is reliable.

#### Controllers & IMU
- **DualSense (USB) motion** — the USB report struct read gyro/accel 5 bytes early (they sit after a 4th button byte + 4 timestamp/padding bytes, per the Linux hid-playstation layout the BT driver already follows), so the SInput IMU streamed constant garbage — "rolling like crazy at rest." Motion now reads the correct offsets; struct size and touchpad alignment unchanged.

#### Native input
- **lodgenet2n64 input freeze** — a single transient controller-read glitch withheld input for ~15 polls (~240 ms); the connect debounce now gates only initial connection, so every good read submits.
- **N64 stick range** — analog sticks over-ranged to N64 ±127 where a real stick peaks ~±84, squaring off on tighter test ROMs. Scaled to an authentic range (tunable `N64_STICK_RANGE`). Affects **all** N64-output apps.
- **lodgenet2n64 stick passes through 1:1 with zero clipping** — a native N64 (LodgeNet clone) stick reaches the N64 (joybus) output verbatim: the host encodes the raw stick byte-for-byte and the N64 device decodes it with no scaling, no range clamp, and no gate reshaping. The authentic ±84 down-scale/clamp that full-range USB→N64 inputs need is bypassed for the native path, so the console sees exactly what the controller reports — including a clone that ranges past ±84 (previously truncated to a square). GC-through-lodgenet2n64 is pre-scaled host-side to stay in range; usb2n64/bt2n64 are unchanged.

#### Build & CI
- Dropped a premature `codex_mode.c` reference from the ESP build, removed a duplicate `cdc_commands_task` from a merge, and added missing shared sources/stubs so pristine CI builds link.
- **ESP `COREDUMP.SUM`** — `esp-idf`'s espcoredump component only exposes `esp_core_dump.h` when coredump is enabled, so boards without it (xiao/feather) failed to compile. Gated the command on `__has_include`, so it builds where coredump is configured and is cleanly absent elsewhere.
- **Release matrix** — added `gc2usb_pico`, `jag2usb_pico`, and `jag2usb_pico_w`. `jag2usb` had shipped as a documented app with no downloadable UF2, and `gc2usb_pico` had been listed as a board target since 2.1.0 while only the kb2040 and rp2040zero variants were ever built. *(Landed after the v2.3.0 tag — first release artifacts arrive in 2.4.0.)*

#### Known Issues
- **ESP32-S3 BLE HID drivers are not registered.** `bthid_registry_init()` has a weak stub in the same translation unit as its call site (`bt_transport.c`), so ESP-IDF's `--gc-sections` resolves the call locally and never links `bthid_registry.c`. `sinput_ble.c` and `mouthpad_ble.c` are also absent from the ESP source list. Affects ESP32-S3 `bt2usb` builds from 2026-06-19 onward. Fix proposed in [#179](https://github.com/joypad-ai/joypad-os/pull/179).

### Changed
- **Companion host tooling** moved to its own repository.
- **README** intro restored (dropped the fork description).

---

## [2.2.0] — 2026-06-23

### Added

#### Apps
- **bt2gc — USB output fallback.** The Bluetooth → GameCube adapter now auto-detects at boot by probing the joybus data pin (wii2gc pattern): a powered GameCube → GameCube output as before; no console (plugged into USB, e.g. a Switch) → USB device output. The USB side defaults to **CDC** (web config) and is **toggleable to any USB output mode** (SInput / Switch / XInput / …) via a button double-click, exactly like `bt2usb` — so one adapter serves a GameCube or a Switch, selected automatically by what it's plugged into. Reuses the existing `usbd_output_interface`; no new machinery.

#### New Apps
- **pce2usb** — native PCEngine / TurboGrafx-16 controllers → USB HID. Bit-bangs the pad's 74157 multiplexer directly (no PIO — PCE pads have no clock to track), polling at ~60 Hz. **Multitap support (up to 5 players)** read with the documented protocol (`pce-devel/PCE_Controller_Info`): hold SEL high across the CLR reset so port 1 is the active port, then advance port-to-port by toggling SEL; ports merge into the single USB gamepad. **Per-port 6-button (Avenue Pad 6)** via a two-scan-per-poll sequence (the bank alternates normal/extended on each CLR pulse, not SEL), gated by the `0000` signature so 2-button pads are never misread — works for a solo pad *and* dual 6-button pads on a multitap (Street Fighter II). The same routine handles a directly-connected pad and a multitap. Activity-based per-port presence detection with pull-down reads; LED idles when nothing is connected. Builds: `pce2usb_kb2040` (NeoPixel), `pce2usb_pico` (GP25 LED), `pce2usb_pico_w` (CYW43 LED). The 2-button paths are hardware-verified; the 6-button decode follows the device/output emulation and awaits an Avenue Pad 6 to confirm on hardware.
- **mouthpad** — Augmental MouthPad (BLE) → USB HID + Nordic UART (NUS) relay over CDC. Reuses the SInput composite (gamepad + mouse + keyboard + CDC) as a superset of the stock MouthPad. **Translation modes** — routed-gamepad by default, with a minimal NUS passthrough relay; mouse widened to 16-bit. An `MP.MODE` CDC command switches the active translation mode (passthrough / right-stick / left-stick) at runtime, per game. Relays `clear_bonds` (classify + encode + dispatch) and answers relay status/info while still scanning (no MouthPad attached). A minimal GAP + GATT ATT server on the BLE central fixes the Nordic HOGP hang (a central with no ATT server made the MouthPad time out the 30 s ATT and stream zero HID). Builds: `mouthpad_aprbrother_nrf52840`, `mouthpad_pico_w`, `mouthpad_pico2_w`.

#### New Platform
- **CH32V307 (WCH)** — `usb2usb` port on the WCH CH32V307: USBFS host-in + USBHS device-out HID gamepad + CDC config port, fully working end-to-end on hardware. Async HCD with full hub + hot-plug (replug via `ERR_USB_UNKNOWN` disconnect / `DEV_ATTACH`-poll), interrupt polling paced to `bInterval`, and bus-powered hub + broad controller enumeration. SInput is the device-out mode. Repo-local toolchain via `make init-wch`. Tracks the `joypad-ai/tinyusb` CH32 USBFS-host fork.

#### New Board Targets
- **`usb2usb_ogxm_pico`** — OGX-Mini Pico hardware (black board, USB host on GP0).
- **`usb2usb_rp2040zero`** — drives a plain status LED on GP14 (OGX-Mini green board, RP2040-Zero drop-in).

#### Core & Infrastructure
- **Configurable HID keymap** — a HID-key → `JP_BUTTON_*` keymap service so keyboards can be remapped to gamepad buttons.
- **Dual BLE HID devices** — per-connection BLE HID client state, so two BLE HID controllers can connect at once.
- **as_gamepad mouse** — emit a gamepad report for mouse-type inputs configured `as_gamepad`.
- **VERSION single source of truth** — all build/release version strings derive from the `VERSION` file.

#### Output & Device
- **Dreamcast VMU — QSPI-primary storage** — the VMU storage selector now prefers onboard QSPI flash with SD as backup, plus persistence reliability fixes. Non-SD targets build cleanly (fatfs/SD gated on `CONFIG_SD`).

### Fixed

#### USB Host & Hubs
- **Hub depth decoupled from device count** — TinyUSB ties them via `CFG_TUH_DEVICE_MAX = 4*CFG_TUH_HUB + 1`, so allowing a deeper hub *tree* silently inflated every per-device driver array (asking for 4 hubs implied 17 devices → `MAX_DEVICES=22`). Made `CFG_TUH_DEVICE_MAX` overridable and set sane independent defaults: **hub depth 4** (covers the PCE Mini's 2 cascaded chips + a chained hub) and a fixed **device cap of 10** (3DO's 8 players + margin; an 11th pad just won't enumerate) → `MAX_DEVICES=15`. `usb2pce` now inherits this (its `CFG_TUH_HUB=4 / MAX_DEVICES=22` override removed); `usb2dc` caps tighter (4 ports). Also fixed the **5th-player hang** (`xinput_task` cached LED/rumble in `[4]`-sized arrays but iterates every USB player; a 5th controller wrote out of bounds and hung Core 0 — sized to `MAX_PLAYERS`).
- **usb2dc RAM overflow** — the 128 KB Dreamcast VMU image left `usb2dc` RAM-starved; right-sized that app's arrays (`CFG_TUH_HUB=1`, `MAX_DEVICES=7`, `MAX_PLAYERS_PER_OUTPUT=4` — Dreamcast is 4 ports, no cascading) without touching the global hub defaults.

#### Generic USB HID gamepad parser
- **Signed (centered-at-0) axes** — pads that declare sticks with `logicalMin < 0` (e.g. ELO Vagabond, ±32767) were misread by the unsigned-only scaler: center read as ~1 and the negative half pegged. Now sign-extends and maps `[min,max] → [1,255]` so center lands at 128. Gated on `min < 0`, so unsigned pads are untouched (no struct growth — `max` narrowed to 16-bit to make room for `min`).
- **Simulation-Controls triggers** — analog triggers declared as Brake (`0xC5`) / Accelerator (`0xC4`) on the Simulation Controls page were dropped (the gamepad interpreter only handled Generic Desktop + Button pages). Now mapped onto the L2/R2 trigger slots.
#### Controllers
- **ELO Vagabond V1** (USB, VID `0483`/PID `A4DB`) — added a dedicated vendor driver. Its button order doesn't match the generic DInput remap (face buttons scrambled, R2 also fired Start, L1/R1 dead); the driver maps everything correctly — A/B/X/Y, L1/R1, L2/R2 (digital + analog Sim-Controls triggers), Select/Start, L3/R3, the two back paddles (→ `L4`/`R4`), and the ELO/Home button (→ `A1`). Signed sticks and Sim-Controls trigger handling were also added to the generic parser (above), benefiting any future standard HID pad with those traits.

#### Output Modes
- **Xbox Original (XID)** — forward vendor control requests to the XID handler, invert stick Y, wire up pressure-sensitive buttons, and fix the Black/White button swap.
- **Xbox One (GIP)** — auth passthrough aligned to the GP2040-CE handshake model.

#### Controllers
- **8BitDo M30 (BT)** — its digital L2/R2 shoulder buttons were also exposed as analog trigger axes, which bypassed button remapping (the analog axis isn't remapped) so users couldn't reassign L2/R2 (the analog L2 latched at 255). Detect the M30 by device name — the one identifier stable across its firmware/mode variants, since some units never resolve VID/PID over BT (matched only by class-of-device) and others report different PIDs — and zero the analog triggers so only the remappable digital buttons drive output. (USB already handled this in the dedicated driver.)
- **PSX** — pass DualShock 2 analog L2/R2 pressure through to the trigger axes; keep Select+Start intact on DualShock pads.

#### Build & CI
- **nRF `controller_btusb`** — `bthid_registry.c` calls `mouthpad_ble_register()` unconditionally, but the controller_btusb nRF source list omitted `mouthpad_ble.c` (undefined-reference link failure).
- **pce2usb in CI** — added `pce2usb_kb2040` / `_pico` / `_pico_w` to the build matrix.
- **steam_controller_2** — added to the nRF + ESP source lists.

#### Web Config
- Surface serial connect failures with an error toast instead of failing silently.

### Changed

#### Dependencies
- **tinyusb** — migrated to master to land the CH32V307 USBFS host, then rolled back to **0.20.0** after an RP2040 host regression (Core-1 TX × master timing erosion). All platforms now unify on the `joypad-ai/tinyusb` `joypad-0.20.0-ch32` fork; the CH32 host rides on top via `TINYUSB_ROOT`. `tusb_xinput` tracks a `joypad-ai` dev branch.

---

## [2.1.1] — 2026-06-02

### Added

#### New Controller Support
- **Valve Steam Controller 2** — USB host driver covering both direct wired (`VID 0x28DE PID 0x1302`) and the 2.4 GHz "puck" USB dongle (`PID 0x1304`). Decodes the 64-byte report ID `0x45` per the jfedor2/hid-remapper quirks: 13 confirmed buttons (ABXY, LB/RB, LT/RT digital, Select/Start, L3/R3, Steam/Home) into `JP_BUTTON_*`, both sticks with Valve's +Y=up inverted to HID convention, analog L2/R2 triggers (16-bit → 8-bit), and a 6-DOF IMU into `input_event_t.accel/gyro`. Routes to every USB device output mode and every console output the rest of joypad-os supports. Driver is parked at `src/usb/usbh/hid/devices/vendors/valve/steam_controller_2.{c,h}`; design doc at `.dev/docs/STEAM_CONTROLLER_2_PLAN.md`. Untested on hardware (no SC2 here at landing time) but compile-clean on every `usb2usb_*` target — see the design doc for the unmapped button bits (likely grip / paddle / trackpad-click) that still need a debug-log pass on real hardware.

#### Tooling
- **joypad-bot** — VLM agent that plays emulators through joypad-os adapters. v1 baseline: pure-software VLM-plays-NES loop with last-action context and frame-diff signal. v1.2 adds continuous emulator state + persistent knowledge field. v2 scaffolding lands the LeRobot recorder + trainer + inference path for vision-grounded play.

### Fixed
- **profile** — apps with built-in *and* custom profiles (`usb2gc`, `usb2pce`, `usb2dc`, `usb2nuon`, `usb23do`, `usb2loopy`) had two independent active-profile state machines: `profile_get_active_index(target)` for built-ins and `flash_get_active_profile_index()` for customs. The router gave custom precedence, but the CDC commands and the SELECT+D-pad hotkey only ever walked one side. Three symptoms: (a) web config could create + select a custom, but on refresh `PROFILE.LIST` returned the built-in active index so the UI showed the wrong profile; (b) switching from a custom back to a built-in via the UI left the custom flag set so the router kept applying the previously selected custom on top of the built-in; (c) the SELECT+D-pad hotkey could only reach one side per app. Fix is a unified `[built-ins, customs]` index space across `cmd_profile_list` / `cmd_profile_get` / `cmd_profile_set` (precedence + "clear custom on built-in select") and `profile_cycle_next/prev`. The cycle hotkey would have hung the firmware on usb2gc / usb2pce / etc. because `flash_set_active_profile_index` commits with `flash_save_now` (~50 ms blocking with interrupts disabled, fine for the rare `PROFILE.SET` deliberate path but not for a hot cycle loop) — added `flash_set_active_profile_index_deferred()` that uses the debounced `flash_save` instead, and pointed the cycle code at it. ESP NVS / nRF NVS already async — stubbed there to keep the link contract.

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
