# Glossary

Key terms used throughout the Joypad OS documentation.

---

## Architecture

**App** -- A build configuration that selects which inputs, outputs, and core services to enable. Each app lives in `src/apps/<name>/` and produces a separate firmware binary. Examples: `usb2gc`, `bt2usb`, `snes2usb`.

**Input Interface** -- A module that reads controller data from a specific source (USB, Bluetooth, SNES, etc.) and normalizes it into `input_event_t`. Implements the `InputInterface` struct. Located in `src/usb/usbh/`, `src/bt/`, `src/native/host/`, or `src/wifi/`.

**Output Interface** -- A module that translates `input_event_t` data into a console-specific or USB protocol. Implements the `OutputInterface` struct. Located in `src/native/device/` or `src/usb/usbd/`.

**Router** -- The central data plane that connects inputs to outputs. Manages player slot assignment, applies profiles, and handles merge logic. Located in `src/core/router/`.

**Profile** -- A button remapping table defined per-app. Users cycle profiles at runtime with SELECT + D-pad. The active profile is applied by the router before data reaches the output.

**Player Slot** -- A numbered position in the output (e.g., Player 1 through Player 4 on GameCube). The router maps physical controllers to player slots based on the routing mode.

**Platform HAL** -- A thin abstraction layer (`src/platform/platform.h`) that provides time, identity, and reboot functions across RP2040, ESP32-S3, and nRF52840.

---

## Data Structures

**input_event_t** -- The universal input event structure. Every input driver normalizes controller data into this format before submitting to the router. Contains buttons (bitmap), analog axes (0-255, 128=center), mouse deltas, device identity, and transport type. Defined in `src/core/input_event.h`.

**JP_BUTTON_\*** -- Button constants following W3C Gamepad API order. Bit positions in a `uint32_t` bitmap:

| Constant | Bit | Xbox | Nintendo | PlayStation |
|----------|-----|------|----------|-------------|
| JP_BUTTON_B1 | 0 | A | B | Cross |
| JP_BUTTON_B2 | 1 | B | A | Circle |
| JP_BUTTON_B3 | 2 | X | Y | Square |
| JP_BUTTON_B4 | 3 | Y | X | Triangle |
| JP_BUTTON_L1 | 4 | LB | L | L1 |
| JP_BUTTON_R1 | 5 | RB | R | R1 |
| JP_BUTTON_L2 | 6 | LT | ZL | L2 |
| JP_BUTTON_R2 | 7 | RT | ZR | R2 |
| JP_BUTTON_S1 | 8 | Back | - | Select |
| JP_BUTTON_S2 | 9 | Start | + | Start |
| JP_BUTTON_L3 | 10 | LS | LS | L3 |
| JP_BUTTON_R3 | 11 | RS | RS | R3 |
| JP_BUTTON_DU | 12 | D-Up | D-Up | D-Up |
| JP_BUTTON_DD | 13 | D-Down | D-Down | D-Down |
| JP_BUTTON_DL | 14 | D-Left | D-Left | D-Left |
| JP_BUTTON_DR | 15 | D-Right | D-Right | D-Right |
| JP_BUTTON_A1 | 16 | Guide | Home | PS |

Defined in `src/core/buttons.h`.

**InputInterface** -- The struct that input drivers implement:
```c
typedef struct {
    const char* name;
    input_source_t source;
    void (*init)(void);
    void (*task)(void);
    bool (*is_connected)(void);
    uint8_t (*get_device_count)(void);
} InputInterface;
```

**OutputInterface** -- The struct that output drivers implement:
```c
typedef struct {
    const char* name;
    output_target_t target;
    void (*init)(void);
    void (*task)(void);
    void (*core1_task)(void);
    uint8_t (*get_rumble)(void);
    uint8_t (*get_player_led)(void);
} OutputInterface;
```

---

## Routing

**SIMPLE routing** -- 1:1 mapping where device N goes to player slot N. Used by most console adapters.

**MERGE routing** -- All input devices merge into a single output slot. Button states are OR'd together. Used by `bt2usb` and copilot/accessibility configurations.

**BROADCAST routing** -- Every input is sent to every output slot. Used for specialized multi-output setups.

**Merge mode** -- When multiple inputs target the same slot, the merge mode determines how they combine: BLEND (OR buttons), PRIORITY (highest-priority wins), or ALL (most recent wins).

**Transform** -- A pre-profile processing step in the router. Transforms include mouse-to-analog conversion, spinner accumulation, and instance merging. Configured per-app via transform flags.

---

## Protocols and Hardware

**PIO** -- Programmable I/O, a hardware feature of RP2040. PIO state machines execute small programs (up to 32 instructions) with cycle-accurate timing, independent of the CPU. Joypad OS uses PIO for all console protocols (joybus, maple bus, polyface, etc.).

**Joybus** -- The single-wire serial protocol used by Nintendo 64 and GameCube controllers. Requires precise bit timing (4us per bit at 250kHz). Implemented via PIO in `src/lib/joybus-pio/`.

**Maple bus** -- The serial protocol used by Sega Dreamcast controllers and peripherals. Implemented via PIO in `src/native/device/dreamcast/maple.pio`.

**Polyface** -- The protocol used by Nuon DVD players to communicate with controllers. Implemented via PIO in `src/native/device/nuon/`.

**PBUS** -- The parallel bus protocol used by 3DO consoles for controller communication. Implemented via PIO in `src/native/device/3do/`.

**SInput** -- Joypad OS's default USB HID gamepad output mode. A standard HID gamepad descriptor designed for broad compatibility.

**XInput** -- The USB protocol used by Xbox 360 controllers. Joypad OS can both read XInput controllers (input) and emulate an Xbox 360 controller (output). The output mode supports real console authentication via XSM3.

**Native controller** -- A retro console controller connected directly to the adapter's GPIO/PIO pins (as opposed to USB or Bluetooth). Examples: SNES pad, N64 controller, GameCube controller.

**Transport** -- The physical communication method a controller uses to connect. Input events carry a transport tag: USB, BT_CLASSIC, BLE, WIFI, or NATIVE.

---

## Platforms

**RP2040** -- The primary microcontroller platform. Dual-core ARM Cortex-M0+ with PIO, used for all adapter types. Boards include KB2040, RP2040-Zero, Pico W, and Pico 2 W.

**RP2350** -- The next-generation RP2040 successor, used in Pico 2 W boards. Backward-compatible with RP2040 PIO programs.

**ESP32-S3** -- Espressif microcontroller used for BLE-to-USB adapters. Runs FreeRTOS with ESP-IDF. BLE only (no Classic Bluetooth).

**nRF52840** -- Nordic Semiconductor microcontroller used for BLE-to-USB adapters. Runs Zephyr RTOS with nRF Connect SDK. BLE only.

**TinyUSB** -- The USB host and device stack used by Joypad OS. Handles USB enumeration, HID parsing, and device emulation.

**BTstack** -- The Bluetooth stack used for Classic BT and BLE HID host functionality. Handles scanning, pairing, bonding, and HID report parsing.

---

## Build System

**Board variant** -- A specific hardware board that an app can target. Appended to the app name in build commands (e.g., `usb2gc_kb2040`, `bt2usb_pico_w`). Different boards may have different pin assignments or features.

**UF2** -- The firmware file format used to flash RP2040 and nRF52840 boards. Drag-and-drop to the board's USB mass storage bootloader.

## Next Steps

- [Architecture](architecture.md) -- The four-layer model
- [Data Flow](data-flow.md) -- How input events travel through the system
