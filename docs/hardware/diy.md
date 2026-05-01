# DIY Builds

Everything you need to build your own Joypad adapter.

## Step-by-Step Build Guides

Pick a guide for your target adapter — these walk through parts, wiring tables, build, flash, and testing:

- [USB → GameCube (KB2040 / Pi Pico / RP2040-Zero)](builds/usb2gc.md)
- [SNES → USB (KB2040)](builds/snes2usb-kb2040.md)
- [N64 → Dreamcast (KB2040)](builds/n642dc-kb2040.md)
- [Bluetooth → USB (Pico W)](builds/bt2usb-pico-w.md)
- [LodgeNet → USB (Pico)](builds/lodgenet2usb-pico.md)

For an adapter not listed above, use the general wiring guide and console-specific pinouts below — every Joypad adapter follows the same pattern: USB host on GPIO 16/17 (or onboard), plus the console-specific output pins.

## General Requirements

1. **Microcontroller board** (see [Supported Boards](boards.md))
2. **USB cable** (USB-C or Micro-USB, depending on board)
3. **Console connector** (specific to target console)
4. **Wires** (22-26 AWG)
5. **Soldering iron** and solder
6. **Optional**: Level shifters, resistors, capacitors

## USB Host Port

Most boards need a USB-A connector wired to GPIO pins for controller input. See the [Wiring Guide](wiring.md) for complete pin assignments and diagrams.

**Exception**: The Adafruit Feather RP2040 USB Host has a built-in USB-A port — no wiring needed.

## Console-Specific Pinouts

Each adapter has its own wiring diagram in its documentation:

- [PCEngine Pinout](../output/pcengine.md)
- [GameCube Pinout](../output/gamecube.md)
- [Dreamcast Pinout](../output/dreamcast.md)
- [Nuon Pinout](../output/nuon.md)
- [3DO Pinout](../output/3do.md)
- [Neo Geo Wiring](../apps/usb2neogeo.md)
- [LodgeNet Pinout](../input/lodgenet.md)

## Common Mistakes

- Reversed power polarity
- Wrong voltage (5V vs 3.3V)
- Cold solder joints
- Crossed data lines (especially D+ and D- on USB host)
- Missing pullup resistors
- Incorrect GPIO pin assignments
- Using a charge-only USB cable (no data lines)

## Where to Buy

### Microcontroller Boards

- [Adafruit](https://www.adafruit.com/) - KB2040, Feather, QT Py
- [Raspberry Pi](https://www.raspberrypi.com/) - Pico, Pico W
- [Waveshare](https://www.waveshare.com/) - RP2040-Zero
- [Seeed Studio](https://www.seeedstudio.com/) - XIAO ESP32-S3, XIAO nRF52840
- [Pimoroni](https://shop.pimoroni.com/) - Various RP2040 boards

### Pre-Built Adapters

- [Joypad Shop](https://joypad.ai/shop) - Ready-to-use products
  - USB2PCE
  - USB2GC (GCUSB)
  - USB2Nuon (NUONUSB)
  - USB23DO

### Console Connectors

- **eBay** - Replacement controller cables
- **AliExpress** - Bulk connectors
- **Console5** - Retro console parts
- **Retro Game Cave** - Specialty connectors

## Community Builds

Share your build on Discord: [community.joypad.ai](http://community.joypad.ai/)

See what others have built and get help with your project!
