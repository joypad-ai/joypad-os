# SNES to USB Adapter (KB2040)

SNES/NES controllers to USB HID gamepad via Adafruit KB2040.

## Parts Needed

- [Adafruit KB2040](https://www.adafruit.com/product/5302) (~$10)
- SNES controller extension cable (cut to expose wires)
- Hookup wire (22-26 AWG), soldering iron

## SNES Controller Connector

The SNES controller port is a 7-pin connector. Cut an extension cable and wire the console-end plug to the KB2040.

```
  ___________
 /  1 2 3 4  \
|  5 6 7      |
 \____________/

 1 = +5V        5 = DATA1 (second data line — multitap/keyboard only)
 2 = CLOCK      6 = IOBIT (mouse/keyboard/rumble only)
 3 = LATCH      7 = GND
 4 = DATA0 (serial data from controller)
```

A stock SNES pad wires only pins 1, 2, 3, 4 and 7. Pins 5 and 6 are unused on it, which is
exactly why the LRG rumble protocol can borrow **pin 6** without disturbing standard
controllers — see [SNES input docs](../../input/snes.md) and
[SNES rumble](../../features/SNES_RUMBLE.md), both of which identify IOBit as connector pin 6.

> ⚠️ **Pin *numbering* on your particular extension cable is still worth confirming with a
> multimeter before you connect +5V.** The signal assignment above is sound, but which physical
> position is "pin 1" depends on how you are holding the plug, and wire colours vary between
> third-party cables. Buzz out +5V (pin 1) and GND (pin 7) first; everything else follows.
> See `a15980f` for why this repo treats connector diagrams as a safety matter.

## Wiring

| KB2040 GPIO | SNES Pin | Signal | Direction |
|-------------|----------|--------|-----------|
| GPIO 5 | 2 | CLOCK | Output to controller |
| GPIO 6 | 3 | LATCH | Output to controller |
| GPIO 7 | 4 | DATA0 | Input from controller |
| GPIO 8 | 5 | DATA1 | Input (multitap/keyboard) |
| GPIO 9 | 6 | IOBIT | Output (mouse/keyboard/rumble) |
| 5V | 1 | +5V | Power to controller |
| GND | 7 | GND | Ground |

The KB2040 GPIO column is verified against `src/apps/snes2usb/app.h:37-41`.

## Build and Flash

```bash
# Build
make snes2usb_kb2040

# Flash: hold BOOTSEL while connecting USB, or double-tap reset
make flash-snes2usb_kb2040
```

Output file: `releases/joypad_<commit>_snes2usb_kb2040.uf2`

## Testing

1. Connect the SNES extension cable between the KB2040 and a SNES controller
2. Plug the KB2040 into a PC via USB
3. The controller appears as a USB HID gamepad
4. Open a gamepad tester and verify D-pad, A/B/X/Y, L/R, Start/Select all register
5. If using a SNES mouse, verify mouse movement and button clicks

## Supported Devices

| Device | Support |
|--------|---------|
| SNES controller | Full (12 buttons + D-pad) |
| NES controller | Full (auto-detected, 8 buttons + D-pad) |
| SNES mouse | Supported (requires IOBIT wiring) |
| Xband keyboard | Supported (requires DATA1 + IOBIT wiring) |

## Notes

- No CPU overclock needed (standard 125 MHz) — `CPU_OVERCLOCK_KHZ 0` in `app.h`
- **Status LED:** the KB2040's onboard NeoPixel shows a pattern chosen by the number of
  connected controllers (`NEOPIXEL_PATTERN_0..5` in `app_config.h`):

  | Controllers | Pattern | Appearance |
  |-------------|---------|------------|
  | 0 (idle) | `pattern_purples` | purple, ramping in brightness |
  | 1 | `pattern_purple` | solid purple |
  | 2 | `pattern_br` | blue / red |
  | 3 | `pattern_brg` | blue / red / green |
  | 4 | `pattern_brgp` | blue / red / green / pink |
  | 5 | `pattern_brgpy` | blue / red / green / pink / yellow |

  Both idle and one-controller states are purple, so brightness — not colour — is what
  distinguishes them. This build has **no** plain GPIO status LED: `BOARD_LED_ENABLED` needs
  `BOARD_LED_PIN` or `BOARD_LED_CYW43`, `joypad_snes2usb` defines neither, and the KB2040 board
  header has no `PICO_DEFAULT_LED_PIN`. Confirmed on the built ELF — zero `board_led` symbols.
- See [SNES input docs](../../input/snes.md) for protocol details and button mapping
- See [snes2usb app docs](../../apps/snes2usb.md) for feature details
