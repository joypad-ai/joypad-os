# Apps

An app is a build configuration that wires together one or more [input interfaces](../input/index.md), one [output interface](../output/index.md), and a set of [core services](../core/index.md). Apps are small and declarative -- typically under 200 lines of code. They define *what* to connect, not *how* to run.

Each app lives in `src/apps/<name>/` and contains:

- **`app.c`** -- Initialization, router configuration, input/output selection.
- **`app.h`** -- Version, compile-time constants (routing mode, max players, transform flags).
- **`profiles.h`** -- Button remapping profiles (optional -- apps without this file pass buttons through unchanged).

Building an app produces a standalone firmware binary: `releases/joypad_<commit>_<app>_<board>.uf2`

## How Apps Work

An app declares its configuration and the main loop handles everything else:

1. **Select inputs** -- Return an array of `InputInterface` pointers (e.g., USB host + Bluetooth).
2. **Select output** -- Return an `OutputInterface` pointer (e.g., GameCube joybus).
3. **Configure router** -- Set routing mode (SIMPLE, MERGE, BROADCAST), merge mode, max players per output.
4. **Define profiles** -- Optional button remapping tables in `profiles.h`.
5. **Register hotkeys** -- Optional callbacks for button combos (IGR, mode switch).

The main loop (`src/main.c`) calls `app_init()` during startup, then runs the service/input/output polling loop with `app_task()` called each iteration.

## All Apps

### Console Adapters (USB/BT Input)

These apps accept USB and Bluetooth controllers and output to retro consoles.

| App | Output | Routing | Players | Boards | Build Command |
|-----|--------|---------|---------|--------|---------------|
| `usb2pce` | PCEngine | SIMPLE | 5 | KB2040 | `make usb2pce_kb2040` |
| `usb2gc` | GameCube | SIMPLE | 4 | KB2040 | `make usb2gc_kb2040` |
| `usb2dc` | Dreamcast | SIMPLE | 4 | KB2040 | `make usb2dc_kb2040` |
| `usb2nuon` | Nuon | SIMPLE | 8 | KB2040 | `make usb2nuon_kb2040` |
| `usb23do` | 3DO | SIMPLE | 8 | RP2040-Zero | `make usb23do_rp2040zero` |
| `usb2loopy` | Casio Loopy | SIMPLE | 4 | KB2040 | `make usb2loopy_kb2040` |
| `usb2neogeo` | Neo Geo | SIMPLE | 1 | KB2040 | `make usb2neogeo_kb2040` |
| `usb2n64` | N64 | SIMPLE | 1 | KB2040 | `make usb2n64_kb2040` |

### Console Adapters (BT-Only Input)

These apps accept Bluetooth controllers only (no USB host) and output to consoles.

| App | Output | Routing | Players | Boards | Build Command |
|-----|--------|---------|---------|--------|---------------|
| `bt2gc` | GameCube | SIMPLE | 4 | Pico W, Pico 2 W | `make bt2gc_pico_w` |
| `bt2n64` | N64 | SIMPLE | 1 | Pico W, Pico 2 W | `make bt2n64_pico_w` |
| `bt2nuon` | Nuon | SIMPLE | 8 | Pico W, Pico 2 W | `make bt2nuon_pico_w` |
| `bt2loopy` | Casio Loopy | SIMPLE | 4 | Pico W, Pico 2 W | `make bt2loopy_pico_w` |

### USB Output Adapters

These apps output as a USB HID gamepad (or other USB device mode).

| App | Input | Routing | Boards | Build Command |
|-----|-------|---------|--------|---------------|
| `usb2usb` | USB/BT | SIMPLE | Feather, RP2040-Zero, nRF52840 | `make usb2usb_feather` |
| `bt2usb` | BT only | MERGE | Pico W, Pico 2 W, ESP32-S3, nRF52840 | `make bt2usb_pico_w` |
| `wifi2usb` | WiFi (JOCP) | MERGE | Pico W, Pico 2 W | `make wifi2usb_pico_w` |

### Native-to-USB Adapters

These apps read retro controllers directly and output as USB HID gamepads.

| App | Input | Routing | Boards | Build Command |
|-----|-------|---------|--------|---------------|
| `snes2usb` | SNES | SIMPLE | KB2040 | `make snes2usb_kb2040` |
| `n642usb` | N64 | SIMPLE | KB2040 | `make n642usb_kb2040` |
| `gc2usb` | GameCube | SIMPLE | KB2040 | `make gc2usb_kb2040` |
| `nes2usb` | NES | SIMPLE | KB2040 | `make nes2usb_kb2040` |
| `neogeo2usb` | Neo Geo | SIMPLE | KB2040 | `make neogeo2usb_kb2040` |
| `lodgenet2usb` | LodgeNet | SIMPLE | Pico | `make lodgenet2usb_pico` |
| `nuon2usb` | Nuon | SIMPLE | KB2040 | `make nuon2usb_kb2040` |

### Cross-Console Bridges

These apps read one retro controller and output to a different console.

| App | Input | Output | Boards | Build Command |
|-----|-------|--------|--------|---------------|
| `n642dc` | N64 | Dreamcast | KB2040 | `make n642dc_kb2040` |
| `snes23do` | SNES | 3DO | RP2040-Zero | `make snes23do_rp2040zero` |
| `n642nuon` | N64 | Nuon | KB2040 | `make n642nuon_kb2040` |

### Utility Apps

| App | Description | Boards | Build Command |
|-----|-------------|--------|---------------|
| `usb2uart` | USB to UART serial bridge (for ESP32 BT modules) | KB2040 | `make usb2uart_kb2040` |
| `usb2ble` | USB to BLE peripheral output | KB2040 | `make usb2ble_kb2040` |
| `nuonserial` | Nuon serial debug tool | KB2040 | `make nuonserial_kb2040` |
| `controller` | Custom GPIO controller (Fisher Price, Alpakka, etc.) | Various | `make controller_<board>` |
| `controller_btusb` | Custom GPIO controller with BT+USB output | Pico W | `make controller_btusb_pico_w` |

## Board Variants

Most apps support multiple board variants. Append the board name to the build command:

| Board | Suffix | MCU | Features |
|-------|--------|-----|----------|
| Adafruit KB2040 | `_kb2040` | RP2040 | USB-C, NeoPixel, compact |
| Waveshare RP2040-Zero | `_rp2040zero` | RP2040 | USB-C, NeoPixel, very small |
| Raspberry Pi Pico | `_pico` | RP2040 | Micro-USB, no NeoPixel |
| Raspberry Pi Pico W | `_pico_w` | RP2040 + CYW43 | WiFi + Bluetooth |
| Raspberry Pi Pico 2 W | `_pico2_w` | RP2350 + CYW43 | WiFi + BT, dual-core |
| Adafruit Feather RP2040 | `_feather` | RP2040 | USB-C, NeoPixel, battery |
| XIAO ESP32-S3 | `_xiao_esp32s3` | ESP32-S3 | BLE, USB OTG, tiny |
| Seeed XIAO nRF52840 | `_seeed_xiao_nrf52840` | nRF52840 | BLE, USB, tiny |
| Adafruit Feather nRF52840 | `_feather_nrf52840` | nRF52840 | BLE, USB, NeoPixel |

## Building

```bash
# Build a specific app for a specific board
make <app>_<board>

# Examples
make usb2gc_kb2040
make bt2usb_pico_w
make snes2usb_kb2040

# Build all RP2040 targets
make all

# Flash (macOS, looks for /Volumes/RPI-RP2)
make flash-<app>_<board>
```

Output files are placed in `releases/`.

## Next Steps

- [Architecture](../overview/architecture.md) -- How apps fit in the layer model
- [Input Interfaces](../input/index.md) -- Available inputs
- [Output Interfaces](../output/index.md) -- Available outputs
- [Joypad Core](../core/index.md) -- Services apps configure
- [Adding an App](../development/index.md) -- How to create a new app
