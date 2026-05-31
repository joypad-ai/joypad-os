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

| QT Py pad | GPIO | PSX Pin | Signal |
|-----------|------|---------|--------|
| A3 | GP29 | 1 | DAT |
| A2 | GP28 | 2 | CMD |
| A1 | GP27 | 6 | ATT |
| A0 | GP26 | 7 | CLK |
| 3V | — | 5 | +3.3V |
| GND | — | 4 | GND |

### KB2040

Same GPIOs (26/27/28/29) — both Adafruit boards break out the A0–A3 pads to GP26–GP29.

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

Output file: `releases/joypad_<commit>_psx2usb_<board>.uf2`

## Testing

1. Wire the adapter per the table above.
2. Plug the adapter into a PC via USB.
3. The board enumerates as a Joypad SInput controller by default.
4. Plug a PSX controller into the cable. Open a gamepad tester ([gamepad-tester.com](https://gamepad-tester.com), or `config.joypad.ai` for the live joypad-web view) and verify:
   - **Digital pad** — all 12 face/d-pad/shoulder buttons + Select/Start register.
   - **DualShock** — both analog sticks center near 128, L3/R3 click, ANALOG button toggles modes (a brief A1/Guide press fires on each toggle).
   - **DualShock 2** — per-button pressure visible in joypad-web's PS3-mode pressure view.

## Output Modes

The adapter ships in SInput mode (the default — full feature set, recognized by Steam/SDL). **Double-click** the BOOTSEL button to cycle output modes: SInput → XInput → PS3 → PS4 → Switch → Keyboard/Mouse → …; **triple-click** resets to SInput. The NeoPixel reflects the active mode (white = SInput, green = XInput, blue = PS3/PS4, red = Switch, yellow = KB/Mouse).

**Single press** of BOOTSEL emits A1 (Guide/PS button) while held — useful for navigating console home menus or triggering a PS-button init without a Select+Start combo.

PS3 mode emits an authentic DS3 descriptor (works on real PS3 consoles).

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
- Rumble motor bytes are driven from the SInput/PS3 feedback state. The 7.5 V motor rail must be supplied externally (NOT through the QT Py / KB2040 / Pico).
- See [PSX input docs](../../input/psx.md) for protocol details (SIO bus, active pull-up, controller ID layout).
- See [psx2usb app docs](../../apps/psx2usb.md) for feature details + USB output mode list.
