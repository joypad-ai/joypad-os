# Pico SDK (RP2040 / RP2350)

The Pico SDK is the primary platform for Joypad OS. All 30+ apps build against it. It provides bare-metal dual-core execution with PIO (Programmable I/O) for cycle-accurate console protocol timing.

## Supported Chips

| Chip | Architecture | Flash | RAM | USB | PIO | Boards |
|------|-------------|-------|-----|-----|-----|--------|
| RP2040 | Dual Cortex-M0+ @ 133MHz | External (2-16MB) | 264KB SRAM | 1.1 Host+Device | 2 blocks, 4 SM each | Pico, Pico W, KB2040, Feather RP2040, RP2040-Zero, MacroPad |
| RP2350 | Dual Cortex-M33 @ 150MHz | External (2-16MB) | 520KB SRAM | 1.1 Host+Device | 3 blocks, 4 SM each | Pico 2, Pico 2 W |

## Dual-Core Architecture

- **Core 0**: Main loop -- USB host polling, Bluetooth (CYW43 on Pico W), input processing, core services, app_task()
- **Core 1**: Timing-critical console output -- PIO program management, joybus/maple/polyface response loops

Core 1 code must be placed in RAM (`__not_in_flash_func`) to avoid XIP cache misses during time-critical protocol responses. On RP2350 (Pico 2 W), Core 0's CYW43 driver periodically locks flash for BT bond storage, which would hang any Core 1 code that isn't RAM-resident.

## PIO (Programmable I/O)

PIO is what makes console protocol support possible. Each PIO block has 32 instruction slots shared across 4 state machines. Console protocols use PIO for sub-microsecond timing:

| Protocol | PIO Programs | Instructions | Clock |
|----------|-------------|-------------|-------|
| GameCube Joybus | joybus.pio | 22 | 130MHz (overclock required) |
| Dreamcast Maple | maple.pio | ~20 | 125MHz |
| PCEngine | plex.pio + clock.pio + select.pio | ~24 total | 125MHz |
| Nuon Polyface | polyface_read.pio + polyface_send.pio | ~20 total | 125MHz |
| 3DO PBUS | sampling.pio + output.pio | ~20 total | 125MHz |
| LodgeNet | lodgenet.pio (MCU + SR) | ~25 total | 125MHz (div 125 = 1us) |
| NeoPixel LED | ws2812.pio | 4 | 125MHz |

PIO resource allocation must be managed carefully. NeoPixel and LodgeNet both use PIO0. CYW43 (Pico W Bluetooth) claims PIO1. See [LEDs](../core/leds.md) for conflict details.

## Bluetooth (Pico W / Pico 2 W)

Pico W boards have a CYW43 WiFi+BT radio. Joypad OS uses BTstack for both Classic Bluetooth and BLE:

- **Classic BT**: DualShock 3/4, Switch Pro, Wiimote, 8BitDo (Classic mode)
- **BLE**: DualSense, Xbox Series, 8BitDo (BLE mode), modern controllers
- **Transport**: `bt_transport_cyw43.c` -- SPI over PIO1

Non-W Pico boards have no wireless capability.

## WiFi (Pico W / Pico 2 W)

The CYW43 radio also provides WiFi for the JOCP (Joypad Open Controller Protocol) input interface. The adapter runs as a WiFi access point. See [WiFi JOCP Input](../input/wifi-jocp.md).

## USB

RP2040/RP2350 have a native USB 1.1 controller supporting both host and device modes:

- **USB Host**: Reads USB controllers (HID, XInput). On some boards, PIO-USB provides a second USB host port.
- **USB Device**: Emulates gamepads (SInput, XInput, PS3/4, Switch, etc.)

Both cannot run simultaneously on the native controller. Apps that need both (like usb2usb) use PIO-USB for the host port and native USB for the device port, or an external MAX3421E chip.

## Build Setup

```bash
# One-time setup (macOS)
brew install --cask gcc-arm-embedded cmake

# Clone and initialize
git clone https://github.com/joypad-ai/joypad-os.git
cd joypad-os
make init

# Build any app
make usb2gc_kb2040
make lodgenet2usb_pico
make bt2usb_pico_w
```

Board selection is handled by board scripts in `src/boards/`:

| Board Script | PICO_BOARD | Chip |
|-------------|-----------|------|
| `build_rpi_pico.sh` | pico | RP2040 |
| `build_pico_w.sh` | pico_w | RP2040 + CYW43 |
| `build_pico2.sh` | pico2 | RP2350 |
| `build_pico2_w.sh` | pico2_w | RP2350 + CYW43 |
| `build_kb2040.sh` | adafruit_kb2040 | RP2040 |
| `build_feather.sh` | adafruit_feather_rp2040 | RP2040 |
| `build_rp2040zero.sh` | waveshare_rp2040_zero | RP2040 |

## Flashing

1. Hold BOOTSEL while connecting USB (or double-tap reset on boards with UF2 bootloader)
2. A USB drive appears (`RPI-RP2` for RP2040, `RP2350` for RP2350)
3. Copy the `.uf2` file to the drive
4. Drive auto-ejects when complete

```bash
make flash-usb2gc_kb2040      # Auto-copies to mounted drive
```

## Common Pitfalls

- **GameCube requires 130MHz**: `set_sys_clock_khz(130000, true)` -- joybus timing fails at default 125MHz
- **PIO has 32 instruction limit**: Programs must fit in 32 slots per PIO block
- **`__not_in_flash_func`**: Required for all Core 1 timing-critical code
- **CYW43 flash contention on RP2350**: Core 1 must never call flash-resident functions (see [RP2350 Core 1 notes](../core/platform-hal.md))
- **NeoPixel GPIO conflict**: Default WS2812 pin (GPIO 2) may conflict with data pins on Pico boards -- auto-disabled when no board-specific pin is defined

## See Also

- [Building from Source](../getting-started/building.md) -- Full build setup guide
- [Supported Boards](../hardware/boards.md) -- Board comparison and features
- [Platform HAL](../core/platform-hal.md) -- Cross-platform abstraction layer
