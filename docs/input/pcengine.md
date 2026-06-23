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
| Multitap (up to 5 players) | All ports read each scan -- see [Multitap](#multitap) |

## Multitap

A PCEngine multitap is read with the same routine as a direct pad (the documented protocol, per [pce-devel/PCE_Controller_Info](https://github.com/pce-devel/PCE_Controller_Info)). The key sequence:

1. Reset to port 1 by pulsing CLR **while holding SEL HIGH** (SEL high, CLR low → high → low). Port 1 is then the active port with SEL already high.
2. Read port 1: directions with SEL HIGH, buttons with SEL LOW.
3. **Toggle SEL HIGH again to advance to the next port**; repeat for up to 5 ports.

The critical detail is that the CLR reset pulse must happen with **SEL held high** -- otherwise the first SEL low→high transition is interpreted by the tap as "advance to the next port" and port 1 is skipped (it lands on port 2).

A directly-connected pad has no tap to advance, so it **echoes its data onto every slot**. The driver distinguishes the two cases by registering slot 0 as the primary controller (direct pad or port 1) and registering a higher slot only when its input **differs** from slot 0 -- which rejects a direct pad's echoes and idle/empty tap ports (both read `0xFF`, equal to slot 0).

Each port is submitted as a separate player (`dev_addr 0xF0 + N`). The [pce2usb](../apps/pce2usb.md) app merges all ports into a single USB gamepad (the USB device output presents one gamepad by default).

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

The data lines use internal **pull-downs**, and presence is **activity-based**: a port registers when it shows a real press (a read that is neither `0xFF` idle nor `0x00` floating) and stays alive while plugged. This is necessary because a multitap drives both empty and released ports to `0xFF`, so an idle port is indistinguishable from an empty one by level alone -- only a press reveals a port.

- Slot 0 is the primary controller (a directly-connected pad or multitap port 1) and registers on its own press.
- Higher slots register only when their press **differs** from slot 0, which rejects a direct pad's echoes and idle/empty tap ports.
- A `0x00` (floating) read marks a slot absent; disconnect is debounced (`PCE_DEBOUNCE_US`).
- On disconnect, the player slot is released so the status LED returns to idle.

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

- **Device address range**: 0xF0+ (port N = 0xF0 + N)
- **Max ports**: 5 (single pad, or up to 5 via multitap)
- **Transport type**: `INPUT_TRANSPORT_NATIVE`
- **Input source**: `INPUT_SOURCE_NATIVE_PCE`
- **Layout**: `LAYOUT_UNKNOWN`

## Apps Using This Input

- [pce2usb](../apps/pce2usb.md) -- PCEngine controller to USB HID
