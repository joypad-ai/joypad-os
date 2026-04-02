# Architecture

Joypad OS is organized into four layers. Each layer has a clear responsibility, and data flows top-to-bottom through them.

```
+-----------------------------------------------------------------------+
|                              APPS                                     |
|  Each app selects its inputs, outputs, routing mode, and profiles.    |
|  src/apps/usb2gc, bt2usb, n642dc, snes2usb, wifi2usb, ...           |
+-----------------------------------------------------------------------+
         |                        |                        |
         v                        v                        v
+--------------------+  +--------------------+  +--------------------+
|   INPUT INTERFACES |  |    JOYPAD CORE     |  |  OUTPUT INTERFACES |
|                    |  |                    |  |                    |
|  USB HID           |  |  Router            |  |  GameCube (joybus) |
|  XInput            |  |  Profiles          |  |  PCEngine (multitap|
|  Bluetooth         |  |  Players           |  |  Dreamcast (maple) |
|  WiFi (JOCP)       |  |  Storage           |  |  Nuon (polyface)   |
|  SNES              |  |  LEDs              |  |  3DO (PBUS)        |
|  N64               |  |  Hotkeys           |  |  N64 (joybus)      |
|  GameCube          |  |  Codes             |  |  Loopy             |
|  NES               |  |  Display           |  |  Neo Geo (GPIO)    |
|  Neo Geo (arcade)  |  |  Button            |  |  NES               |
|  LodgeNet          |  |  Speaker           |  |  USB Device        |
|  3DO host          |  |                    |  |  BLE Peripheral    |
|  Nuon host         |  +--------------------+  |  UART              |
|  GPIO              |                          |                    |
|  UART              |                          +--------------------+
+--------------------+
         |                        |                        |
         v                        v                        v
+-----------------------------------------------------------------------+
|                         PLATFORM HAL                                  |
|  RP2040 (pico-sdk)  |  ESP32-S3 (ESP-IDF)  |  nRF52840 (Zephyr)    |
+-----------------------------------------------------------------------+
```

## The Four Layers

### Apps

An app is a build configuration that wires together one or more inputs, one output, and a set of core services. Apps live in `src/apps/` and are small -- typically under 200 lines. They declare *what* to connect, not *how* to run.

For example, `usb2gc` connects USB and Bluetooth inputs to the GameCube joybus output with SIMPLE routing and five button profiles. Meanwhile, `bt2usb` connects Bluetooth input to the USB device output with MERGE routing.

See [Apps](../apps/index.md) for the full list.

### Input Interfaces

Input interfaces read controllers and normalize their data into a common format (`input_event_t`). Every input -- whether a USB gamepad, a Bluetooth controller, a SNES pad, or a WiFi-connected phone -- produces the same event structure.

**Supported inputs:**

| Input | Protocol | Source |
|-------|----------|--------|
| USB HID | USB Host | Gamepads, keyboards, mice, hubs |
| XInput | USB Host | Xbox 360/One/Series controllers |
| Bluetooth | BT Classic + BLE | Wireless controllers via dongle, Pico W, ESP32, nRF |
| WiFi (JOCP) | UDP/TCP | Controllers over WiFi (Pico W) |
| SNES | GPIO shift register | SNES/NES controllers, SNES mouse, Xband keyboard |
| N64 | Joybus (PIO) | N64 controllers with rumble pak |
| GameCube | Joybus (PIO) | GameCube controllers with rumble |
| NES | PIO | NES controllers |
| Neo Geo | GPIO (active-low) | Neo Geo arcade sticks |
| LodgeNet | PIO | LodgeNet hotel system controllers (N64/GC/SNES) |
| 3DO Host | PIO | 3DO controllers |
| Nuon Host | PIO (polyface) | Nuon controllers (experimental) |
| GPIO | GPIO pins | Custom-wired buttons and sticks |
| UART | Serial | Input bridge from external MCUs |

See [Input Interfaces](../input/index.md) for details on each.

### Joypad Core

The core layer provides shared services that all apps use:

- **Router** -- Routes input events to output slots. Supports 1:1 (SIMPLE), many:1 (MERGE), and 1:many (BROADCAST) modes.
- **Profiles** -- Button remapping. Apps define profiles; users cycle them with SELECT + D-pad.
- **Players** -- Manages controller-to-slot assignment, connect/disconnect, and feedback routing.
- **Storage** -- Persists settings, profiles, and Bluetooth bonds to flash.
- **LEDs** -- NeoPixel status indicators and profile color feedback.
- **Hotkeys** -- Button combo detection (e.g., L1+R1+Start+Select for in-game reset).
- **Codes** -- Button sequence recognition.
- **Display** -- I2C OLED/LCD output (SSD1306).
- **Button** -- Board button events (click, double-click, hold).
- **Speaker** -- Audio feedback via PWM.

See [Joypad Core](../core/index.md) for details on each service.

### Output Interfaces

Output interfaces translate the common input format into console-specific or USB protocols. Console outputs use RP2040 PIO state machines for cycle-accurate timing and run on Core 1.

**Supported outputs:**

| Output | Protocol | Max Players |
|--------|----------|-------------|
| GameCube / Wii | Joybus (PIO, 130MHz) | 4 |
| PCEngine / TurboGrafx-16 | Multiplexed (PIO) | 5 |
| Dreamcast | Maple bus (PIO) | 4 |
| Nuon | Polyface (PIO) | 8 |
| 3DO | Parallel bus (PIO) | 8 |
| N64 | Joybus (PIO) | 1 |
| Casio Loopy | PIO | 4 |
| Neo Geo / SuperGun | GPIO | 1 |
| NES | PIO | 1 |
| USB Device | USB 2.0 (13 modes) | 1 |
| BLE Peripheral | BLE HID | 1 |
| UART | Serial | 1 |

See [Output Interfaces](../output/index.md) for details on each.

### Platform HAL

A thin hardware abstraction layer (`src/platform/platform.h`) lets the same core and service code run across three microcontroller platforms:

| Platform | SDK | RTOS | Primary Use |
|----------|-----|------|-------------|
| RP2040 / RP2350 | pico-sdk | Bare metal | All adapters (console, USB, native) |
| ESP32-S3 | ESP-IDF | FreeRTOS | BLE-to-USB (bt2usb) |
| nRF52840 | nRF Connect SDK | Zephyr | BLE-to-USB (bt2usb, usb2usb) |

## Dual-Core Execution

On RP2040, Joypad OS uses both CPU cores:

- **Core 0** runs the main loop: service tasks, input polling, output tasks, and app logic.
- **Core 1** runs the timing-critical output protocol -- a tight PIO loop that must not be interrupted by USB or Bluetooth processing.

This separation ensures console protocols meet their strict timing requirements regardless of how busy the input side is.

## Next Steps

- [Data Flow](data-flow.md) -- How input events travel through the system
- [Glossary](glossary.md) -- Key terms and definitions
- [Apps](../apps/index.md) -- Full list of applications
