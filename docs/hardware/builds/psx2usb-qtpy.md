# PSX2USB Adapter (QT Py / KB2040 / Pi Pico)

PlayStation 1 / PlayStation 2 controllers to USB HID gamepad. Auto-detects digital pads, DualShock, DualShock 2 (with pressure), neGcon, Dual Analog flightstick, GunCon, JogCon, and PlayStation Mouse.

## Parts Needed

- One of:
  - [Adafruit QT Py RP2040](https://www.adafruit.com/product/4900) (~$10)
  - [Adafruit KB2040](https://www.adafruit.com/product/5302) (~$10)
  - [Raspberry Pi Pico](https://www.raspberrypi.com/products/raspberry-pi-pico/) (~$4)
- PlayStation controller extension cable (cut to expose wires), or a salvaged PSX/PS2 console-side socket
- One 1 kΩ to 10 kΩ resistor (pull-up on DAT)
- Hookup wire (22–26 AWG), soldering iron

## PSX Controller Connector

The PSX/PS2 controller plug is 9-pin. Looking into the **console-side socket** (or at the back of a cut extension cable):

```
  _________________________
 |   1   2   3 | 4   5   6 | 7   8   9   |
  \__|___|___|_|_|___|___|_|_|___|___|__/

 1 = DAT  (data from controller, open-drain)     6 = ATT  (attention / chip select, active-low)
 2 = CMD  (commands to controller)               7 = CLK  (clock, ~250 kHz - 500 kHz)
 3 = +7V  (rumble motor power, OPTIONAL)         8 = (not connected for slot 1)
 4 = GND                                         9 = ACK  (acknowledge, not required)
 5 = +3.3V (controller logic power)
```

Pin 3 (+7V) only matters if you want rumble on a DualShock — wire it to a separate 7.5 V supply *not* through the MCU. **Do not** wire pin 3 to a USB 5 V rail; the rumble motors brown-out the controller and corrupt analog reads.

## Wiring

The active-pull-up PIO needs `CLK` and `ATT` on **consecutive GPIOs** (it's a 2-bit side-set with CLK on the lower pin). The other two lines can be on any free pin. Per-board pin assignments:

### QT Py RP2040 (primary target)

> ⚠️ The QT Py RP2040 numbers its analog pads in **descending** GPIO order — **A0 is GP29, A3 is GP26** (the reverse of the KB2040). Wire by the pad label in the table below, not by assuming A0 is the lowest GPIO.

| QT Py pad | GPIO | PSX Pin | Signal |
|-----------|------|---------|--------|
| A0 | GP29 | 1 | DAT |
| A1 | GP28 | 2 | CMD |
| A2 | GP27 | 6 | ATT |
| A3 | GP26 | 7 | CLK |
| 3V | — | 5 | +3.3V |
| GND | — | 4 | GND |

### KB2040

Same GPIOs as the QT Py (CLK=GP26, ATT=GP27, CMD=GP28, DAT=GP29), but the KB2040 numbers its A0–A3 pads in **ascending** GPIO order (A0 is GP26), so the pad labels are the reverse of the QT Py.

| KB2040 pad | GPIO | PSX Pin | Signal |
|------------|------|---------|--------|
| A0 | GP26 | 7 | CLK |
| A1 | GP27 | 6 | ATT |
| A2 | GP28 | 2 | CMD |
| A3 | GP29 | 1 | DAT |
| 3.3V | — | 5 | +3.3V |
| GND | — | 4 | GND |

### Raspberry Pi Pico

| Pico GPIO | PSX Pin | Signal |
|-----------|---------|--------|
| GP20 | 7 | CLK |
| GP21 | 6 | ATT |
| GP19 | 2 | CMD |
| GP22 | 1 | DAT |
| 3V3 OUT | 5 | +3.3V |
| GND | 4 | GND |

### Pull-up on DAT

DAT is open-drain; the controller pulls it low for `0` bits and lets it float for `1`. The RP2040's internal pull-up works for most modern pads, but old analog pads (e.g. Sony SCPH-110) read cleaner with an external **1 kΩ to 10 kΩ resistor from DAT to +3.3V**. The firmware's active-pull-up PIO trick handles the rest.

## Build and Flash

```bash
# Build
make psx2usb_qtpy            # or psx2usb_kb2040 / psx2usb_pico

# Flash: hold BOOTSEL while connecting USB
make flash-psx2usb_qtpy      # or psx2usb_kb2040 / psx2usb_pico
```

Output file: `releases/joypad_<commit>_psx2usb_<board>.uf2` (a local build is tagged with the short commit hash; release builds are tagged with the version instead, e.g. `joypad_2.4.0_psx2usb_qtpy.uf2`).

> ℹ️ **There is no prebuilt UF2 to download yet.** `psx2usb` was only added to the release build matrix on 2026-08-05, after v2.3.0 shipped — no release up to and including v2.3.0 contains a `psx2usb` asset. Until the next release, building from source with the commands above is the only way to get this firmware.

## Testing

1. Wire the adapter per the table above.
2. Plug the adapter into a PC via USB.
3. The board enumerates as a Joypad SInput controller by default.
4. Plug a PSX controller into the cable. Open a gamepad tester ([gamepad-tester.com](https://gamepad-tester.com), or [joypad.ai](https://joypad.ai), which reads the pad over WebHID and therefore works in **any** output mode) and verify:
   - **Digital pad** — all 12 face/d-pad/shoulder buttons + Select/Start register.
   - **DualShock** — both analog sticks center near 128, L3/R3 click, ANALOG button toggles modes (a brief A1/Guide press fires on each toggle).
   - **DualShock 2** — per-button pressure visible in the PS3-mode pressure view.

> ⚠️ **`config.joypad.ai` is the configuration tool, not the tester, and it needs the USB serial (CDC) interface.** CDC is only present in the **SInput** (default), **Keyboard/Mouse** and CDC-only configuration descriptors. Cycle the adapter to XInput, PS3, PS4 or Switch and the CDC interface disappears from the descriptor, so config.joypad.ai can no longer see the board — triple-click BOOTSEL to return to SInput before configuring.

## Output Modes

The adapter ships in SInput mode (the default — full feature set, recognized by Steam/SDL). **Double-click** the BOOTSEL button to cycle output modes: SInput → XInput → PS3 → PS4 → Switch → Keyboard/Mouse → …; **triple-click** resets to SInput.

**Single press** of BOOTSEL emits A1 (Guide/PS button) while held — useful for navigating console home menus or triggering a PS-button init without a Select+Start combo.

PS3 mode emits an authentic DS3 descriptor (works on real PS3 consoles).

### Status LED — board-dependent

The mode colour is driven through `leds_set_color()`, which only ever reaches a **NeoPixel**. What you see depends on the board:

| Board | Status LED | Mode colour shown? |
|-------|-----------|--------------------|
| QT Py RP2040 | NeoPixel on GP12 (powered from GP11, driven high at init) | ✅ yes |
| KB2040 | NeoPixel on GP17 | ✅ yes |
| Raspberry Pi Pico | **none** | ❌ no |

> ⚠️ **The Pi Pico build has no status LED at all.** The Pico has no NeoPixel, and the `psx2usb` target never defines `BOARD_LED_PIN`, so the plain GP25 LED is not driven either — `neopixel_init()` compiles down to an immediate return and `leds_set_color()` has nothing to write to. **Mode switches on a Pico are completely silent**; confirm the active mode from the USB device name on the host instead of looking for a light.

On the two NeoPixel boards the colours are: white = SInput, green = XInput, blue = PS3/PS4 (dim/bright), red = Switch, yellow = KB/Mouse.

### Controller-side combos

These come from the shared USB-device layer and are not documented elsewhere for this adapter:

- **Select + Start → A1 (Guide/PS)** — only on **genuine digital-only pads**. DualShock-family pads (DualShock, DS2-pressure, Dual Analog flight) reach A1 through their physical ANALOG button, so their Start+Select is deliberately left intact — otherwise output modes with no A1 (e.g. Xbox Original) would lose the ability to press the two together.
- **Select + D-pad Up** (previous profile) or **Select + D-pad Down** (next) — press both together and hold for about **0.7 s**; pressing them one after the other does nothing, and a quick tap passes straight through to the game. The selection stops at the ends rather than wrapping. These are the router's built-in defaults (`router_install_default_combos()`), which psx2usb inherits because it registers no combo table of its own.
- Pads with **no Select button** — the Dual Analog flight stick (`0x53`), GunCon and PlayStation Mouse — therefore cannot reach either combo. Use BOOTSEL for A1 on those.

## Supported Devices

| Device | Type ID | Notes |
|--------|---------|-------|
| Digital pad (SCPH-1080) | 0x41 | Standard 12-button pad |
| DualShock | 0x73 | Analog sticks auto-enabled |
| DualShock 2 | 0x79 | + 12-byte per-button pressure |
| neGcon | 0x23 | Namco NPC-101; twist + I/II/L analog |
| Dual Analog flight | 0x53 | SCPH-1110 / Dual Analog "red" mode |
| GunCon | 0x63 | Namco light gun; aim on right stick (needs CRT) |
| JogCon | 0xE3 | Namco paddle wheel on left-stick X |
| PlayStation Mouse | 0x12 | SCPH-1090; relative cursor + 2 buttons |

## Notes

- No CPU overclock required (standard 125 MHz).
- ANALOG button → A1/Guide is detected via the analog ↔ digital mode toggle; it fires a brief A1 pulse on every press so a console PS-button init still registers.
- Rumble motor bytes are pulled from the active output's feedback state each poll (heavy/low motor → the large PWM byte, light/high motor → the small on/off byte), so any output mode that reports rumble drives them. The motors only respond after the pad's `0x4D` enable-rumble mapping has been sent, which happens during DualShock configuration. A **JogCon is excluded** — it drives the same two motor bytes itself for the wheel force-feedback recentre, not host rumble. The 7.5 V motor rail must be supplied externally (NOT through the QT Py / KB2040 / Pico).
- See [PSX input docs](../../input/psx.md) for protocol details (SIO bus, active pull-up, controller ID layout).
- See [psx2usb app docs](../../apps/psx2usb.md) for feature details + USB output mode list.
