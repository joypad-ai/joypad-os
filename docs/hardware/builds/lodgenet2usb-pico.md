# LodgeNet to USB Adapter (Pico)

Nintendo LodgeNet hotel controllers to USB HID gamepad via Raspberry Pi Pico.

## Parts Needed

- [Raspberry Pi Pico](https://www.raspberrypi.com/products/raspberry-pi-pico/) (~$4)
- RJ11 6P6C breakout board or cut RJ11 cable
- Hookup wire (22-26 AWG), soldering iron

## LodgeNet Connector

LodgeNet controllers use a 6-pin RJ11-style connector (GAME port). Only 4 lines are needed:

```
 +-+-+-+-+-+-+-+       GAME
 | | | | | | | |   1   DATA (in)
 | 1 2 3 4 5 6 |   2   CLK (out)
 |             |   3   12V
 |             |   4   CLK2 (out)
 +---+---+---+-+   5   GND
     +---+         6   IR (unused)
```

## Wiring

| Pico GPIO | RJ11 Pin | Signal | Direction |
|-----------|----------|--------|-----------|
| GPIO 2 | 1 | DATA | Input (from controller, active-low with pull-up) |
| GPIO 3 | 2 | CLK | Output (idle HIGH) |
| GPIO 4 | -- | VCC | Output (powers controller from GPIO) |
| GPIO 5 | 4 | CLK2 | Output (SNES SR protocol only) |
| VBUS (5V) | 3 | Power | Powers controller (use 5V instead of 12V) |
| GND | 5 | GND | Ground |

The controller is powered via GPIO 4 (VCC output) which the firmware drives HIGH. The original 12V line (RJ11 pin 3) is not needed -- LodgeNet controllers work fine at 5V from the Pico's VBUS rail.

## Build and Flash

```bash
# Build
make lodgenet2usb_pico

# Flash: hold BOOTSEL while plugging in USB, then:
make flash-lodgenet2usb_pico
```

Output file: `releases/joypad_<commit>_lodgenet2usb_rpi_pico.uf2`

## Testing

1. Connect the RJ11 cable between the Pico and a LodgeNet controller
2. Plug the Pico into a PC via USB
3. The adapter auto-detects the controller type (N64, GameCube, or SNES variant)
4. The controller appears as a USB HID gamepad
5. Open a gamepad tester and verify all buttons and analog sticks register

## Supported Controllers

The adapter automatically detects and hot-swaps between all three LodgeNet controller types:

| Controller | Protocol | Detection |
|------------|----------|-----------|
| LodgeNet N64 | MCU (3-wire serial) | Byte 1 bit 7 set |
| LodgeNet GameCube | MCU (3-wire serial) | Byte 1 bit 6 set |
| LodgeNet SNES | SR (shift register) | Presence bit in SR read |

## Notes

- The detected controller layout (N64/GameCube face style) is reported via the SInput USB descriptor so the host can display correct button labels
- See [LodgeNet input docs](../../input/lodgenet.md) for protocol details and full button mapping
- See [lodgenet2usb app docs](../../apps/lodgenet2usb.md) for feature details
