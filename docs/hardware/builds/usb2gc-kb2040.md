# USB to GameCube Adapter (KB2040)

USB/Bluetooth controllers to GameCube/Wii via Adafruit KB2040.

## Parts Needed

- [Adafruit KB2040](https://www.adafruit.com/product/5302) (~$10)
- GameCube controller extension cable (cut to expose wires)
- USB-A female breakout or cut USB-A extension cable (for controller input)
- Hookup wire (22-26 AWG), soldering iron

## Wiring

### GameCube Connector

The GameCube controller port uses a proprietary connector. Cut a GC extension cable and wire the console-end plug to the KB2040.

| KB2040 Pin | GC Signal | Notes |
|------------|-----------|-------|
| GPIO 7 | Data | Bidirectional joybus data line |
| GND | GND | Common ground |

Cable wire colors vary by manufacturer — verify with a multimeter against the [GameCube Pinout reference](../../output/gamecube.md) before soldering.

The console provides the joybus pull-up internally, so KB2040's 3V3 rail does not need to be wired to the cable. Play/config mode is detected by reading the data line on boot — if it idles HIGH, the console is connected and play mode runs; if LOW, the adapter falls back to USB device + CDC config mode.

### USB Host Port

Wire a USB-A female connector for controller input. See [Wiring Guide](../wiring.md) for details.

KB2040 uses the same pins as Pico for PIO-USB:

| KB2040 Pin | USB-A Pin | Signal |
|------------|-----------|--------|
| GPIO 16 | 3 | D+ (green) |
| GPIO 17 | 2 | D- (white) |
| 5V | 1 | VBUS (red) |
| GND | 4 | GND (black) |

## Wiring Diagram

![USB2GC Wiring Diagram](../../images/Joypad_USB-2-NGC.png)

## Build and Flash

```bash
# Build
make usb2gc_kb2040

# Flash (hold BOOT on KB2040 while connecting USB, or double-tap reset)
make flash-usb2gc_kb2040
```

Output file: `releases/joypad_<commit>_usb2gc_kb2040.uf2`

## Testing

1. Connect the GC cable to a GameCube or Wii console
2. Plug a USB controller into the USB-A port
3. The NeoPixel LED should turn solid purple (1 controller connected)
4. Press a button on the controller -- the GameCube should register input
5. Verify analog sticks, triggers, and rumble feedback

## Important Notes

- The KB2040 runs at **130 MHz** (overclocked from 125 MHz) for precise joybus timing
- All USB inputs are merged to a single GC output (MERGE_BLEND mode)
- Profile cycling: hold SELECT + D-pad Up/Down for 2 seconds
- See [usb2gc app docs](../../apps/usb2gc.md) for profiles, keyboard mode, and feature details
