# PCEngine / TurboGrafx-16 Controller Protocol

A technical reference for the PCEngine/TurboGrafx-16 controller protocol, covering the controller interface, multitap scanning mechanism, 6-button extension, and mouse support.

---

## Table of Contents

- [Overview](#overview)
- [Physical Layer](#physical-layer)
- [Protocol Basics](#protocol-basics)
- [2-Button Mode](#2-button-mode)
- [6-Button Mode](#6-button-mode)
- [3-Button Mode](#3-button-mode)
- [Mouse Protocol](#mouse-protocol)
- [Multitap Scanning](#multitap-scanning)
- [Timing Requirements](#timing-requirements)
- [Quick Reference](#quick-reference)
- [References](#references)

---

## Overview

The **PCEngine controller protocol** (also known as TurboGrafx-16 in North America) is a parallel 4-bit interface developed by NEC and Hudson Soft. The protocol supports:

- Standard 2-button controllers
- 6-button fighting game pads
- Mouse input devices
- Multitap for up to 5 simultaneous players

### Key Characteristics

- **Parallel interface**: 4-bit data bus (D0-D3)
- **Active LOW encoding**: 0 = pressed, 1 = released
- **Scan-based**: Console controls timing via SEL and CLR signals
- **Nibble-multiplexed**: D-pad and buttons sent as separate 4-bit nibbles
- **Multitap support**: Up to 5 players via time-division multiplexing
- **Extensible**: 6-button mode adds extended button nibbles

### Historical Context

The PCEngine was released in 1987 (Japan) and 1989 (North America as TurboGrafx-16). The controller protocol was designed for simplicity and low cost, using common 74-series logic chips in the controller hardware. The multitap accessory enabled 5-player Bomberman, which became a defining feature of the platform.

---

## Physical Layer

### Connector Pinout

The PCEngine controller port uses an **8-pin DIN connector**:

| Pin | Name | Direction | Description |
|-----|------|-----------|-------------|
| 1 | VCC | - | +5V power supply |
| 2 | D0 | Output (from controller) | Data bit 0 (active LOW) |
| 3 | D1 | Output | Data bit 1 (active LOW) |
| 4 | D2 | Output | Data bit 2 (active LOW) |
| 5 | D3 | Output | Data bit 3 (active LOW) |
| 6 | SEL | Input (to controller) | Select signal (nibble toggle) |
| 7 | CLR | Input (to controller) | Clear/Enable signal (scan reset) |
| 8 | GND | - | Ground |

> **Naming note:** Pin 7 is called **CLR** (Clear) because it resets the multitap scan to Player 1. Some references label it **OE** (Output Enable, active LOW) since it enables controller data output. They refer to the same signal -- CLR/OE are interchangeable names for pin 7.

### Electrical Characteristics

- **Logic levels**: TTL compatible (0V = LOW, 5V = HIGH)
- **Data lines**: Open-collector with pull-up resistors on console side
- **Control signals**: Driven by console, interpreted by controller
- **Power**: 5V @ ~50mA per controller

### Signal Behavior

```
CLR:  ────┐         ┌─────────────────┐
          └─────────┘                 └────  (LOW = scan active, HIGH = idle)

SEL:  ────┐   ┌───┐   ┌───┐   ┌───┐   ┌──  (toggles between nibbles)
          └───┘   └───┘   └───┘   └───┘

D0-3: ──[D-PAD]─[BTNS]─[D-PAD]─[BTNS]────  (responds to SEL transitions)
```

**Scan Sequence**:
1. CLR goes LOW -- Start of scan cycle
2. SEL alternates HIGH/LOW -- Controller outputs different nibbles
3. CLR goes HIGH -- End of scan, controller resets

---

## Protocol Basics

### Nibble Encoding (Active LOW)

All data is transmitted as 4-bit nibbles with **active LOW** encoding:

- **0** (LOW) = Button/Direction **pressed**
- **1** (HIGH) = Button/Direction **released**
- **0xF** (all HIGH) = No input (default/idle state)

### Standard 2-Button Controller Data

Each scan reads **2 nibbles** (8 bits total):

**Nibble 1 (SEL=HIGH)**: D-Pad

| Bit | Direction | Encoding |
|-----|-----------|----------|
| 3 | Left | 0 = pressed |
| 2 | Down | 0 = pressed |
| 1 | Right | 0 = pressed |
| 0 | Up | 0 = pressed |

**Nibble 2 (SEL=LOW)**: Buttons

| Bit | Button | Encoding |
|-----|--------|----------|
| 3 | Run | 0 = pressed |
| 2 | Select | 0 = pressed |
| 1 | II | 0 = pressed |
| 0 | I | 0 = pressed |

### Example Encoding

| Scenario | Nibble 1 (D-pad) | Nibble 2 (Buttons) | Full Byte |
|----------|-------------------|---------------------|-----------|
| No buttons pressed | 0xF (1111) -- no directions | 0xF (1111) -- no buttons | 0xFF |
| Up + I pressed | 0xE (1110) -- Up bit 0 = 0 | 0xE (1110) -- I bit 0 = 0 | 0xEE |
| Left + Down + II + Run | 0x3 (0011) -- Left bit 3 = 0, Down bit 2 = 0 | 0x5 (0101) -- Run bit 3 = 0, II bit 1 = 0 | 0x35 |

---

## 2-Button Mode

The standard PCEngine controller mode used by most games.

### Byte Format

```
Bit Position:  7     6      5      4    |  3    2       1   0
              Left  Down  Right   Up   | Run  Select   II  I
```

### Scan Sequence

1. **CLR LOW**: Start scan
2. **SEL HIGH**: Read D-pad nibble (bits 7-4)
3. **SEL LOW**: Read button nibble (bits 3-0)
4. **CLR HIGH**: End scan, latch data

The full byte is constructed by combining the D-pad nibble (high) with the button nibble (low):
```
Byte: [Left, Down, Right, Up, Run, Select, II, I]
```

---

## 6-Button Mode

Extended mode for fighting games (Street Fighter II Championship Edition, Art of Fighting, etc.).

### Protocol Extension

6-button mode uses a **4-scan cycle** with a rotating state counter (3, 2, 1, 0). On most states the controller sends standard 2-button data, but on one specific state it sends the extended buttons instead:

| State | Nibble 1 (SEL=HIGH) | Nibble 2 (SEL=LOW) |
|-------|----------------------|---------------------|
| 3, 1, 0 (Standard) | D-pad: Left, Down, Right, Up | Buttons: Run, Select, II, I |
| 2 (Extended) | Extended: III, IV, V, VI (bits 7-4) | Reserved: all 0 (bits 3-0) |

The state counter decrements on each scan (3 -> 2 -> 1 -> 0) and wraps back to 3 after state 0.

### Extended Button Layout

| Bit | Button |
|-----|--------|
| 7 | III |
| 6 | IV |
| 5 | V |
| 4 | VI |
| 3-0 | Reserved (all 0) |

Games detect 6-button controllers by reading the reserved low nibble: if bits 3-0 are all 0 during a scan, the controller is in 6-button mode.

---

## 3-Button Mode

Some PCEngine games (e.g., earlier Street Fighter II ports) recognize only 3 buttons. In 3-button mode, a third action is mapped onto one of the existing standard buttons:

**Select as III**: The third button press is reported as a Select press.

**Run as III**: The third button press is reported as a Run press.

This allows a 6-button controller to be used with games that read Select or Run as a third attack button, without requiring the 6-button protocol extension.

---

## Mouse Protocol

The PCEngine Mouse protocol sends 8-bit signed X/Y deltas broken into nibbles across 4 scan cycles.

### Protocol Structure

Each mouse update requires **4 scans** (states 3 -> 2 -> 1 -> 0):

**High nibble (SEL=LOW)**: Always contains buttons (Run, Select, II, I)
**Low nibble (SEL=HIGH)**: Contains movement data

| State | High Nibble (SEL=LOW) | Low Nibble (SEL=HIGH) |
|-------|------------------------|------------------------|
| 3 | Buttons | X delta bits 7-4 (high) |
| 2 | Buttons | X delta bits 3-0 (low) |
| 1 | Buttons | Y delta bits 7-4 (high) |
| 0 | Buttons | Y delta bits 3-0 (low) |

### Delta Encoding

- **X-axis**: Left = `0x01` to `0x7F`, Right = `0x80` to `0xFF`
- **Y-axis**: Up = `0x01` to `0x7F`, Down = `0x80` to `0xFF`
- **Center/No movement**: `0x00`

### Movement Example

**Mouse moved right by 45 pixels, up by 23 pixels**:

The X delta is 45 (0x2D, binary 0010 1101) and the Y delta is 23 (0x17, binary 0001 0111). These are split into nibbles and sent across four scan states, with the button state occupying the upper four bits (shown as "b") of each byte:

| State | Byte Layout | Content |
|-------|-------------|---------|
| 3 | bbbb 0010 | X high nibble |
| 2 | bbbb 1101 | X low nibble |
| 1 | bbbb 0001 | Y high nibble |
| 0 | bbbb 0111 | Y low nibble |

The "b" bits represent the button state (e.g., 1111 if no buttons are pressed).

### Delta Accumulation

The console scans the mouse at approximately 60Hz. If a host device reports movement at a higher rate (e.g., USB mice at 125-1000Hz), deltas must be accumulated between scans. After a complete 4-scan mouse read (state 0), the accumulated deltas should be cleared so the next read reports only new movement.

### Compatible Games

- **Afterburner II** - Flight combat
- **Darius Plus** - Horizontal shooter
- **Lemmings** - Puzzle platformer

---

## Multitap Scanning

The PCEngine multitap supports **up to 5 players** via time-division multiplexing.

### Multitap Protocol

The multitap is a **passive device** containing:
- 5 controller ports
- Multiplexing logic (74-series shift registers)
- Single output to console

When the console scans, the multitap:
1. Outputs Player 1 data on the first SEL toggle pair
2. Outputs Player 2 data on the second SEL toggle pair
3. Continues through Player 5
4. Resets to Player 1 on the next CLR transition

### Data Ordering

Each player occupies 8 bits (two nibbles) in the scan sequence. For a full 5-player scan:

```
SEL toggles:  P1_dpad  P1_btns  P2_dpad  P2_btns  ...  P5_dpad  P5_btns
              ─────────────────────────────────────────────────────────────
Nibble #:        1        2        3        4      ...     9       10
```

The multitap advances to the next player after each pair of nibbles (one SEL HIGH + one SEL LOW). CLR going HIGH resets the player counter back to Player 1.

---

## Timing Requirements

### Scan Cycle Timing

**Typical timing** (measured on real hardware):
- CLR LOW duration: ~400-500us
- SEL toggle period: ~50-100us per toggle
- Full scan cycle: ~600-800us

### State Transition Flow

```
Time (us)     CLR    SEL     State   Action
────────────────────────────────────────────────
0             HIGH   HIGH    Idle    Waiting for scan
50            LOW    HIGH    3       CLR falling edge, scan begins
100           LOW    LOW     3       Output Player 1 D-pad
150           LOW    HIGH    3       Output Player 1 buttons
200           LOW    LOW     2       Output Player 2 D-pad
...           ...    ...     ...     ...
600           HIGH   HIGH    Reset   CLR rising edge, scan ends
```

After the CLR rising edge, the controller/multitap resets to Player 1 and the state counter resets.

---

## Quick Reference

### Button Bit Mapping

**Standard Byte** (States 3, 1, 0):
```
Bit:  7     6      5      4    |  3    2       1   0
     Left  Down  Right   Up   | Run  Select   II  I
```

**6-Button Extended** (State 2):
```
Bit:  7     6     5    4   |  3   2   1   0
     III   IV    V    VI  |  0   0   0   0
```

**Mouse Byte** (per state):
```
Bit:  7     6       5   4   |  3    2    1    0
     Run  Select   II  I   | [Movement Nibble]
```

### Connector Pinout Summary

| Pin | Signal | Direction | Description |
|-----|--------|-----------|-------------|
| 1 | VCC | Power | +5V power supply |
| 2 | D0 | Controller -> Console | Data bit 0 (active LOW) |
| 3 | D1 | Controller -> Console | Data bit 1 (active LOW) |
| 4 | D2 | Controller -> Console | Data bit 2 (active LOW) |
| 5 | D3 | Controller -> Console | Data bit 3 (active LOW) |
| 6 | SEL | Console -> Controller | Select (nibble toggle) |
| 7 | CLR/OE | Console -> Controller | Clear/Output Enable (scan reset) |
| 8 | GND | Power | Ground |

---

## Acknowledgments

- **David Shadoff** - PCEngine controller protocol research, including [PCEMouse](https://github.com/dshadoff/PC_Engine_RP2040_Projects/tree/main/PCEMouse) documentation
- **NEC / Hudson Soft** - Original PCEngine hardware design
- **Retro community** - Protocol documentation and testing

---

## References

- [PCEngine Development Wiki](https://www.nesdev.org/wiki/PC_Engine_hardware)
- [David Shadoff's PCEngine Projects](https://github.com/dshadoff/PC_Engine_RP2040_Projects)
- [PC Engine Software Bible](http://www.magicengine.com/mkit/doc_hard_pce.html)
- [TurboGrafx-16 Technical Specifications](https://en.wikipedia.org/wiki/TurboGrafx-16)
