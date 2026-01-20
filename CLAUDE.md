# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Joypad OS (formerly **USBRetro**) is firmware for RP2040-based adapters that provides universal controller I/O. Old code/commits may reference `USBR_BUTTON_*` or `usbretro` naming.

**Inputs:**
- USB HID controllers, keyboards, mice
- USB X-input (Xbox controllers)
- Bluetooth controllers (via USB BT dongle or Pico W)
- WiFi controllers (via JOCP protocol on Pico W)
- Native controllers (SNES, N64, GameCube via joybus)

**Outputs:**
- Retro consoles: PCEngine, GameCube, Dreamcast, Nuon, 3DO, Loopy
- USB Device: HID gamepad, XInput, DirectInput, PS3/PS4/Switch modes
- UART: ESP32 Bluetooth bridge

Uses TinyUSB for USB, BTstack for Bluetooth, LWIP for WiFi networking, and RP2040 PIO for timing-critical console protocols.

## Build Commands

### Quick Start

```bash
# One-time setup
brew install --cask gcc-arm-embedded cmake git

# Clone and initialize
git clone https://github.com/joypad-ai/joypad-os.git
cd joypad-os
make init

# Build apps (append _kb2040, _rp2040zero, etc. for specific boards)
make usb2pce_kb2040    # USB/BT → PCEngine
make usb2gc_kb2040     # USB/BT → GameCube
make usb2dc_kb2040     # USB/BT → Dreamcast
make usb2nuon_kb2040   # USB/BT → Nuon
make usb23do_rp2040zero # USB/BT → 3DO
make usb2usb_feather   # USB/BT → USB HID
make snes2usb_kb2040   # SNES → USB HID
make n642usb_kb2040    # N64 → USB HID
make gc2usb_kb2040     # GameCube → USB HID
make n642dc_kb2040     # N64 → Dreamcast
make bt2usb_pico_w     # BT-only → USB HID (Pico W)
make wifi2usb_pico_w   # WiFi → USB HID (Pico W)

# Build all
make all
make clean

# Flash (macOS - looks for /Volumes/RPI-RP2)
make flash              # Flash most recent build
make flash-usb2pce_kb2040  # Flash specific app
```

Output: `releases/joypad_<commit>_<app>_<board>.uf2`

### App Build Matrix

| App | Board | Input | Output |
|-----|-------|-------|--------|
| `usb2pce` | KB2040 | USB/BT | PCEngine |
| `usb2gc` | KB2040 | USB/BT | GameCube |
| `usb2dc` | KB2040 | USB/BT | Dreamcast |
| `usb2nuon` | KB2040 | USB/BT | Nuon |
| `usb23do` | RP2040-Zero | USB/BT | 3DO |
| `usb2loopy` | KB2040 | USB/BT | Loopy |
| `usb2usb` | Feather/RP2040-Zero | USB/BT | USB HID |
| `bt2usb` | Pico W/Pico 2 W | BT-only | USB HID |
| `wifi2usb` | Pico W/Pico 2 W | WiFi (JOCP) | USB HID |
| `snes2usb` | KB2040 | SNES | USB HID |
| `n642usb` | KB2040 | N64 | USB HID |
| `gc2usb` | KB2040 | GameCube | USB HID |
| `n642dc` | KB2040 | N64 | Dreamcast |
| `snes23do` | RP2040-Zero | SNES | 3DO |
| `usb2uart` | KB2040 | USB | UART/ESP32 |
| `controller_*` | Various | GPIO | USB HID |

## Architecture

### Repository Structure

```
src/
├── main.c                      # Entry point, main loop
├── CMakeLists.txt              # Build configuration
├── core/                       # Shared firmware infrastructure
│   ├── buttons.h               # JP_BUTTON_* definitions (W3C order)
│   ├── input_event.h           # Unified input event structure
│   ├── output_interface.h      # Output abstraction
│   ├── router/                 # Input→Output routing
│   │   └── router.c/h          # SIMPLE/MERGE/BROADCAST modes
│   └── services/
│       ├── players/            # Player slot management + feedback
│       ├── profiles/           # Button remapping profiles
│       ├── codes/              # Button sequence detection
│       ├── hotkeys/            # Button combo detection
│       ├── leds/               # NeoPixel LED control
│       └── storage/            # Flash persistence
├── apps/                       # App configurations
│   ├── usb2pce/                # USB/BT → PCEngine
│   ├── usb2gc/                 # USB/BT → GameCube
│   ├── usb2nuon/               # USB/BT → Nuon
│   ├── usb23do/                # USB/BT → 3DO
│   ├── usb2loopy/              # USB/BT → Loopy
│   ├── usb2usb/                # USB/BT → USB HID
│   ├── wifi2usb/               # WiFi → USB HID (JOCP protocol)
│   ├── usb2uart/               # USB → UART bridge
│   ├── snes2usb/               # SNES → USB HID
│   ├── snes23do/               # SNES → 3DO
│   └── controller/             # Custom GPIO controllers
├── usb/
│   ├── usbh/                   # USB Host (input)
│   │   ├── hid/                # HID device drivers
│   │   │   └── devices/        # Vendor-specific drivers
│   │   └── xinput/             # X-input protocol
│   └── usbd/                   # USB Device (output)
│       ├── usbd.c/h            # USB device stack
│       ├── tud_xinput.*        # XInput output mode
│       └── descriptors/        # USB descriptors
├── bt/                         # Bluetooth support
│   ├── bthid/                  # BT HID device drivers
│   │   └── devices/            # BT controller drivers
│   └── transport/              # BT transport layer
├── wifi/                       # WiFi support (Pico W)
│   └── jocp/                   # JOCP protocol implementation
│       ├── jocp.h              # Protocol definitions
│       ├── jocp_input.c        # Packet parsing/conversion
│       ├── wifi_transport.c/h  # WiFi AP, UDP/TCP servers
│       └── dhcpserver.c/h      # DHCP server
└── native/
    ├── device/                 # Console outputs (we emulate devices)
    │   ├── pcengine/           # PCEngine multitap (PIO)
    │   ├── gamecube/           # GameCube joybus (PIO)
    │   ├── dreamcast/          # Dreamcast maple bus (PIO)
    │   ├── nuon/               # Nuon polyface (PIO)
    │   ├── 3do/                # 3DO controller (PIO)
    │   ├── loopy/              # Casio Loopy (PIO)
    │   └── uart/               # UART output
    └── host/                   # Native inputs (we read controllers)
        ├── snes/               # SNES controller reading
        ├── n64/                # N64 controller reading (joybus)
        └── gc/                 # GameCube controller reading (joybus)
```

### Data Flow

```
Input Sources                    Router                      Output Targets
─────────────                    ──────                      ──────────────
USB HID ──────┐                                              ┌──→ PCEngine
USB X-input ──┤                                              ├──→ GameCube
Bluetooth ────┼──→ router_submit_input() ──→ router ──→ ────┼──→ Dreamcast
WiFi (JOCP) ──┤                              │               ├──→ Nuon, 3DO
Native SNES ──┤                              │               ├──→ USB Device
Native N64 ───┤                              │               └──→ UART
Native GC ────┘
                                    profile_apply()
                                    (button remapping)
```

### Key Abstractions

#### input_event_t (`core/input_event.h`)
```c
typedef struct {
    uint8_t dev_addr;           // Device address
    int8_t instance;            // Instance number
    input_device_type_t type;   // GAMEPAD, MOUSE, KEYBOARD
    uint32_t buttons;           // Button bitmap (JP_BUTTON_*)
    uint8_t analog[8];          // Analog axes (0-255, 128=center)
    int8_t delta_x, delta_y;    // Mouse deltas
} input_event_t;
```

#### OutputInterface (`core/output_interface.h`)
```c
typedef struct {
    const char* name;
    void (*init)(void);
    void (*core1_entry)(void);      // Runs on Core 1
    void (*task)(void);             // Periodic task on Core 0
    uint8_t (*get_rumble)(void);
    uint8_t (*get_player_led)(void);
} OutputInterface;
```

#### Router Modes
- **SIMPLE**: 1:1 mapping (device N → slot N)
- **MERGE**: All inputs merged to single output
- **BROADCAST**: All inputs to all outputs

#### Profile System
Apps can define button remapping profiles in `profiles.h`:
- SELECT + D-pad Up/Down cycles profiles (after 2s hold)
- Visual feedback via NeoPixel LED
- Haptic feedback via rumble
- Profile selection persisted to flash
- Apps without `profiles.h` pass buttons through unchanged

### Button Definitions (`core/buttons.h`)

W3C Gamepad API order - bit position = button index:
```c
#define JP_BUTTON_B1 (1 << 0)   // A         B         Cross
#define JP_BUTTON_B2 (1 << 1)   // B         A         Circle
#define JP_BUTTON_B3 (1 << 2)   // X         Y         Square
#define JP_BUTTON_B4 (1 << 3)   // Y         X         Triangle
#define JP_BUTTON_L1 (1 << 4)   // LB        L         L1
#define JP_BUTTON_R1 (1 << 5)   // RB        R         R1
#define JP_BUTTON_L2 (1 << 6)   // LT        ZL        L2
#define JP_BUTTON_R2 (1 << 7)   // RT        ZR        R2
#define JP_BUTTON_S1 (1 << 8)   // Back      -         Select
#define JP_BUTTON_S2 (1 << 9)   // Start     +         Start
#define JP_BUTTON_L3 (1 << 10)  // LS        LS        L3
#define JP_BUTTON_R3 (1 << 11)  // RS        RS        R3
#define JP_BUTTON_DU (1 << 12)  // D-Up
#define JP_BUTTON_DD (1 << 13)  // D-Down
#define JP_BUTTON_DL (1 << 14)  // D-Left
#define JP_BUTTON_DR (1 << 15)  // D-Right
#define JP_BUTTON_A1 (1 << 16)  // Guide     Home      PS
```

### Dual-Core Architecture

- **Core 0**: USB/BT polling, input processing, main loop
- **Core 1**: Console output protocol (timing-critical PIO)

### PIO State Machines

Console protocols use RP2040 PIO for precise timing:
- **PCEngine**: `plex.pio`, `clock.pio`, `select.pio`
- **GameCube**: `joybus.pio` (130MHz clock required)
- **Dreamcast**: `maple.pio` (maple bus protocol)
- **Nuon**: `polyface_read.pio`, `polyface_send.pio`
- **3DO**: `sampling.pio`, `output.pio`
- **Loopy**: `loopy.pio`
- **N64/GC Host**: `joybus.pio` (shared with GC device)

## Development Workflow

### Adding a New App

1. Create `src/apps/<appname>/` with:
   - `app.c` - App initialization, router/player config
   - `app.h` - Version, config constants
   - `profiles.h` - Button mapping profiles (optional)

2. Add to `CMakeLists.txt` and `Makefile`

3. Build: `make <appname>`

### Adding a New USB Device Driver

1. Create `src/usb/usbh/hid/devices/vendors/<vendor>/<device>.c/h`

2. Implement:
   ```c
   bool <device>_is_device(uint16_t vid, uint16_t pid);
   void <device>_init(uint8_t dev_addr, uint8_t instance);
   void <device>_process(uint8_t dev_addr, uint8_t instance,
                         uint8_t const* report, uint16_t len);
   void <device>_disconnect(uint8_t dev_addr, uint8_t instance);
   ```

3. Register in `hid_registry.c`

### Adding a Bluetooth Device Driver

1. Create `src/bt/bthid/devices/vendors/<vendor>/<device>.c/h`

2. Similar interface to USB HID drivers

3. Register in BT device registry

### Adding a Native Controller Host (like gc_host, n64_host)

1. Create `src/native/host/<protocol>/<protocol>_host.c/h`

2. Implement `HostInterface` and `InputInterface`:
   ```c
   extern const HostInterface <protocol>_host_interface;
   extern const InputInterface <protocol>_input_interface;
   ```

3. Add `INPUT_SOURCE_NATIVE_<PROTOCOL>` to `router.h`

4. Key functions:
   - `<protocol>_host_init()` - Initialize PIO/GPIO
   - `<protocol>_host_task()` - Poll controller, submit to router
   - Use `router_submit_input()` with dev_addr 0xD0+ range

5. Remember to invert Y-axis if protocol uses non-HID convention

## Common Pitfalls

- **GameCube requires 130MHz** - `set_sys_clock_khz(130000, true)`
- **PIO has 32 instruction limit** - Optimize or split programs
- **Use `__not_in_flash_func`** - For timing-critical code
- **Y-axis convention** - All input drivers MUST normalize to HID standard: 0=up, 128=center, 255=down
  - Sony/Xbox/8BitDo controllers: Native HID, no inversion needed
  - Nintendo controllers: Invert Y (Nintendo uses 0=down, 255=up)
  - Native GC/N64: Invert Y when reading

## External Dependencies

Submodules in `src/lib/`:
- **pico-sdk** (2.2.0): Raspberry Pi Pico SDK
- **tinyusb** (0.19.0): USB host/device stack
- **tusb_xinput**: X-input controller support
- **joybus-pio**: GameCube/N64 joybus protocol
- **btstack**: Bluetooth stack (for BT support)

## CI/CD

GitHub Actions (`.github/workflows/build.yml`):
- Builds all apps on push to `main`
- Docker-based for consistency
- Artifacts in `releases/` directory
