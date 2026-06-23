# PCEngine Input Interface

Reads native PCEngine / TurboGrafx-16 controllers by bit-banging the controller's multiplexer directly. PCE pads are a 74157-style 2:1 mux with no clock to track, so no PIO is required -- the driver drives the SEL/CLR lines and samples the four data lines from a ~60Hz polling task.

## Protocol

- **Bus**: 4-bit multiplexed parallel (SEL + CLR control, D0-D3 data)
- **Method**: GPIO bit-bang, ~60Hz timestamp-throttled from `pce_host_task()`
- **Location**: `src/native/host/pcengine/`

A standard pad multiplexes two nibbles onto the four data lines, selected by SEL (all signals active-LOW, 0 = pressed):

| SEL | D0 | D1 | D2 | D3 |
|-----|----|----|----|----|
| HIGH | Up | Right | Down | Left |
| LOW | I | II | Select | Run |

CLR is held low for an active read; pulsing it advances the pad's internal bank (used for the 6-button extended read and multitap addressing). The ~60Hz poll interval also provides the idle gap the 6-button bank counter needs to reset between frames.

## Supported Controllers

| Device | Notes |
|--------|-------|
| Standard 2-button pad | I, II, Select, Run, D-pad |
| 6-button Avenue Pad 6 | Adds III, IV, V, VI (best-effort -- see below) |

## Button Mapping

| PCE Button | JP_BUTTON_* |
|------------|-------------|
| I | B2 |
| II | B1 |
| III | B3 |
| IV | B4 |
| V | L1 |
| VI | R1 |
| Select | S1 |
| Run | S2 |
| D-pad Up/Down/Left/Right | DU/DD/DL/DR |

!!! note "I/II mapping"
    PCE button **I** maps to **B2** and **II** to **B1**, matching the [PCEngine output interface](../output/pcengine.md) (`pcengine_device.c`) so a pad read here and sent back out to a real PCEngine round-trips identically.

## 6-Button Support

The 6-button Avenue Pad 6 exposes III-VI in an extended bank read after a CLR pulse. The driver reads it best-effort, gated by an all-zero signature nibble so a 2-button pad can never be misread (`PCE_ENABLE_6BUTTON`, default on). The CLR-pulse bank-advance model is derived from the PCEngine device emulation and has not yet been validated against real 6-button hardware.

## Analog Axes

PCEngine controllers are purely digital. All analog axes remain at center (128).

## Connection Detection

The data lines use internal **pull-downs**. A connected pad's 74157 mux actively drives released buttons HIGH (push-pull), while an empty port floats to all-zero -- so any high bit means a pad is present (you cannot hold all four directions plus all four buttons at once). This is what makes presence detection possible; with pull-ups, an idle pad and an empty port are indistinguishable (both read all-high).

- **Connect / Disconnect**: 300ms debounced (`PCE_DEBOUNCE_US`)
- On disconnect, the player slot is released so the status LED returns to idle
- Input is only submitted while a pad is present (an empty pull-down read of all-zero must not be treated as "all buttons pressed")

!!! warning "Push-pull assumption"
    Presence detection assumes the pad's data outputs are push-pull (the standard 74HC157 is). A hypothetical open-drain pad would read as never-connected.

## Feedback

No rumble or LED feedback (PCEngine controllers have none).

## Configuration

Default GPIO pins (overridable per-app in `app.h`):

| Pin | Default GPIO | Function |
|-----|-------------|----------|
| PCE_PIN_SEL | 5 | SEL output to controller |
| PCE_PIN_CLR | 6 | CLR output to controller |
| PCE_PIN_D0 | 8 | D0-D3 inputs (8, 9, 10, 11; pull-downs enabled) |

### PCEngine Controller Port (8-pin mini-DIN)

| Pin | Signal | Description |
|-----|--------|-------------|
| 1 | VCC | Power (use 3.3V -- see warning) |
| 2 | D0 | Up / I |
| 3 | D1 | Right / II |
| 4 | D2 | Down / Select |
| 5 | D3 | Left / Run |
| 6 | SEL | Select line (input to controller) |
| 7 | CLR | Clear/enable line (input to controller) |
| 8 | GND | Ground |

!!! warning "Power at 3.3V, not 5V"
    RP2040 GPIOs are not 5V-tolerant. Power the pad from the board's 3.3V output so the data lines idle at 3.3V. Powering at 5V requires level-shifting D0-D3.

!!! note "Verify connector numbering"
    Mini-DIN pin numbering varies between sources. Buzz out **pin 1 (VCC)** and **pin 8 (GND)** with a multimeter before powering on. SEL/CLR and D0-D3 ordering are also remappable in `app.h` if they come up swapped or scrambled.

### Pico / Pico W Wiring

GPIO numbers are identical on both boards (same physical layout).

| PCE Pin | Signal | GPIO | Physical Pin |
|---------|--------|------|--------------|
| 1 | VCC | -- | 36 (3V3 OUT) |
| 2 | D0 (Up / I) | GP8 | 11 |
| 3 | D1 (Right / II) | GP9 | 12 |
| 4 | D2 (Down / Select) | GP10 | 14 |
| 5 | D3 (Left / Run) | GP11 | 15 |
| 6 | SEL | GP5 | 7 |
| 7 | CLR | GP6 | 9 |
| 8 | GND | -- | 8 (or any GND pin) |

### KB2040 Wiring

| PCE Pin | Signal | KB2040 GPIO |
|---------|--------|-------------|
| 1 | VCC (3.3V) | 3V3 |
| 2 | D0 (Up / I) | GP8 |
| 3 | D1 (Right / II) | GP9 |
| 4 | D2 (Down / Select) | GP10 |
| 5 | D3 (Left / Run) | GP11 |
| 6 | SEL | GP5 |
| 7 | CLR | GP6 |
| 8 | GND | GND |

D0-D3 have internal pull-downs enabled in firmware. No external resistors are required.

- **Device address range**: 0xF0+ (port 0 = 0xF0)
- **Max ports**: 1
- **Transport type**: `INPUT_TRANSPORT_NATIVE`
- **Input source**: `INPUT_SOURCE_NATIVE_PCE`
- **Layout**: `LAYOUT_UNKNOWN`

## Apps Using This Input

- [pce2usb](../apps/pce2usb.md) -- PCEngine controller to USB HID
