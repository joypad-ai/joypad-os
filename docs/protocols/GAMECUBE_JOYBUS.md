# GameCube Joybus Protocol & Keyboard Reference

**Comprehensive Joybus protocol reference with reverse-engineered keyboard support**

Documented by Robert Dale Smith (2022-2025)
Based on [joybus-pio](https://github.com/JonnyHaystack/joybus-pio) by JonnyHaystack

This document provides a comprehensive technical reference for the GameCube Joybus controller protocol, with detailed coverage of the **reverse-engineered keyboard protocol**.

---

## Table of Contents

- [Overview](#overview)
- [Physical Layer](#physical-layer)
- [Joybus Protocol Basics](#joybus-protocol-basics)
- [Controller Protocol](#controller-protocol)
- [Keyboard Protocol (Reverse-Engineered)](#keyboard-protocol-reverse-engineered)
- [Timing Requirements](#timing-requirements)
- [Implementation Notes](#implementation-notes)

---

## Overview

The **GameCube Joybus protocol** (also known as "GC-Joybus" or "SI protocol") is a bidirectional serial protocol developed by Nintendo for communication between the GameCube console and its peripherals. The protocol is used for:

- Standard GameCube controllers
- WaveBird wireless controllers (with receiver)
- GameCube Keyboard (for Phantasy Star Online Episode I & II)
- GameCube to GBA link cable
- Other licensed peripherals

### Key Characteristics

- **Single-wire bidirectional**: Data line is tri-state (console or controller drives)
- **250 kbit/s nominal bitrate**: 4us per bit
- **9-bit byte encoding**: 8 data bits + 1 stop bit
- **Command-response protocol**: Console sends command, controller responds
- **Multiple report modes**: 6 different analog/button configurations
- **Rumble support**: Integrated motor control via stop bit

### Historical Context

The Joybus protocol was originally developed for the Nintendo 64 and adapted for GameCube with higher bandwidth and more sophisticated features. The keyboard accessory was released only in Japan for Phantasy Star Online Episode I & II, making it a rare peripheral with minimal documentation.

---

## Physical Layer

### Connector Pinout

The GameCube controller port uses a **proprietary 6-pin connector**:

| Pin | Name | Direction | Description |
|-----|------|-----------|-------------|
| 1 | VCC | - | +5V power (from console) |
| 2 | DATA | Bidirectional | Tri-state data line (3.3V logic) |
| 3 | GND | - | Ground |
| 4 | GND | - | Ground (shield) |
| 5 | N/C | - | Not connected |
| 6 | 3.3V | - | +3.3V power (from console, optional) |

### Electrical Characteristics

- **Logic levels**: 3.3V CMOS (0V = LOW, 3.3V = HIGH)
- **Data line**: Open-drain with ~1k pull-up resistor on console side
- **Idle state**: Data line HIGH (pulled up)
- **Drive strength**: Both console and controller can actively drive the line LOW
- **Power**: 5V @ ~200mA max (with rumble active)

### Tri-State Operation

| Phase | Behavior |
|-------|----------|
| Console transmit | Console drives data line LOW/HIGH |
| Console receive | Console releases line, controller drives |
| Controller transmit | Controller drives data line LOW/HIGH |
| Controller receive | Controller releases line (pulled HIGH) |

**Critical**: Both sides must release the line (go tri-state) when not transmitting to avoid bus contention.

---

## Joybus Protocol Basics

### Bit Encoding

Each bit is transmitted as a **4us period** with 3 phases:

**Logical 0**:
```
    1us      2us      1us
   +-----+         +-----
   | LOW |   LOW   | HIGH  (pulled up after release)
---+     +---------+
```

**Logical 1**:
```
    1us      2us      1us
   +-----+---------+-----
   | LOW |  HIGH   | HIGH
---+     +---------+
```

**Encoding rule**:
- All bits start with **1us LOW pulse**
- Bit value determines **2us data period** (LOW = 0, HIGH = 1)
- Followed by **1us delay** before next bit

**Sampling point**: Receiver samples at **1us after falling edge** (middle of 2us data period)

### 9-Bit Byte Encoding

The Joybus protocol uses **9-bit bytes** to support multi-byte messages without inter-byte gaps:

| Bit(s) | Field | Description |
|--------|-------|-------------|
| 0-7 | Data byte | MSB first |
| 8 | Stop bit | 0 = continue, 1 = stop |

**Example** (3-byte command `0x40 0x03 0x00`):

| Byte | Value | Stop Bit | Meaning |
|------|-------|----------|---------|
| 0 | 0x40 | 0 | Continue to next byte |
| 1 | 0x03 | 0 | Continue to next byte |
| 2 | 0x00 | 1 | End of command |

### Command-Response Cycle

The console sends a command (1-3 bytes) to the controller, then waits for a 4us reply delay. The controller then sends its response (length varies by command) back to the console.

**Reply delay**: Controller must wait 4us (1 bit period) after receiving command before responding.

### Timing Constraints

- **Command timeout**: 50us (if no response received, console retries)
- **Inter-byte gap**: None (9-bit encoding allows continuous transmission)
- **Reset timeout**: 130us (if line stays LOW/HIGH too long, protocol resets)

---

## Controller Protocol

### Command Set

| Command | Hex | Bytes | Description | Response |
|---------|-----|-------|-------------|----------|
| PROBE | 0x00 | 1 | Probe device type | 3 bytes (status) |
| RESET | 0xFF | 1 | Reset device | 3 bytes (status) |
| ORIGIN | 0x41 | 1 | Calibration request | 10 bytes (origin report) |
| RECALIBRATE | 0x42 | 3 | Recalibration | 10 bytes (origin report) |
| POLL | 0x40 | 3 | Poll controller state | 8 bytes (controller report) |
| **KEYBOARD** | **0x54** | **3** | **Poll keyboard state** | **8 bytes (keyboard report)** |
| GAME_ID | 0x1D | 11 | Read game disc ID | (varies) |

### Device Identification

**PROBE / RESET Response** (3 bytes):

Bytes 0-1 contain the device type as a big-endian 16-bit value:

| Value | Device |
|-------|--------|
| 0x0009 | Standard controller |
| 0x0900 | WaveBird receiver (no controller paired) |
| 0x0920 | WaveBird receiver (controller paired) |
| 0x2008 | Keyboard |
| 0x0800 | Steering wheel |
| 0x0200 | Bongos |

Byte 2 contains status flags:

| Bit | Description |
|-----|-------------|
| 0 | Rumble motor supported |
| 1 | Standard controller |
| 2-7 | Reserved |

**Example** (standard controller with rumble): The response bytes are `0x09 0x00 0x03`, where bytes 0-1 (`0x0900`) identify the device type and byte 2 (`0x03`) indicates both rumble support and standard controller flags are set.

### Controller Poll Command

**Command**: `0x40 0x03 0x00` (8 bytes requested, rumble stop bit)

**Rumble control via stop bit**:
- `0x40 0x03 | 0` -> Rumble OFF (stop bit = 0)
- `0x40 0x03 | 1` -> Rumble ON (stop bit = 1)

**Byte 1** (`0x03`): Report mode
- `0x00` = Mode 0 (4-bit triggers, 4-bit analog A/B)
- `0x01` = Mode 1 (4-bit C-stick, full triggers)
- `0x02` = Mode 2 (4-bit C-stick, 4-bit triggers)
- `0x03` = Mode 3 (standard - full 8-bit everything)
- `0x04` = Mode 4 (full C-stick, full analog A/B)

**Byte 2** (`0x00`): Reserved (always 0x00)

### Controller Report Format (Mode 3)

**8 bytes** (default mode used by most games):

| Byte | Content | Description |
|------|---------|-------------|
| 0 | Button bits: A, B, X, Y, Start, Origin, ErrLatch, ErrStat | Face buttons and status flags (MSB to LSB) |
| 1 | Button bits: DL, DR, DD, DU, Z, R, L, High1 | D-pad, triggers, and protocol marker (MSB to LSB) |
| 2 | Left stick X | 0x00 = left, 0x80 = center, 0xFF = right |
| 3 | Left stick Y | 0x00 = down, 0x80 = center, 0xFF = up |
| 4 | C-stick X | Same range as left stick |
| 5 | C-stick Y | Same range as left stick |
| 6 | L analog | 0x00 = released, 0xFF = fully pressed |
| 7 | R analog | 0x00 = released, 0xFF = fully pressed |

**Bit fields**:
- All buttons: Active HIGH (1 = pressed)
- `Origin` bit: LOW after console sends ORIGIN command
- `High1` bit: Always set to 1 (protocol marker)

### Origin Calibration

**ORIGIN Command** (`0x41`):
Requests the controller's neutral position (calibration data).

**Response** (10 bytes): Bytes 0-7 contain the current controller state in the same format as a POLL response. Bytes 8-9 are reserved and always 0x00.

The console uses this to establish the controller's center position for analog sticks and triggers. Games typically request this on boot or when the controller is first connected.

---

## Keyboard Protocol (Reverse-Engineered)

### Discovery

The GameCube keyboard was released exclusively in Japan for **Phantasy Star Online Episode I & II**. The protocol was completely undocumented, requiring hardware analysis and iterative testing to decode.

**Key discovery**: The keyboard uses command `0x54` (not documented anywhere publicly) and returns an 8-byte report with **3 simultaneous keypresses** and a **rolling counter with XOR checksum**.

### Keyboard Poll Command

**Command**: `0x54 0x00 0x00` (8 bytes requested)

Unlike the controller, the keyboard does **not** support rumble, so the stop bit has no effect.

### Keyboard Report Format

**8 bytes**:

| Byte | Content | Description |
|------|---------|-------------|
| 0 | Counter (4 bits), Unknown (2 bits), ErrLatch, ErrStat | High nibble is a rolling counter; low 2 bits are error flags |
| 1 | Unknown | Reserved / undetermined |
| 2 | Unknown | Reserved / undetermined |
| 3 | Unknown | Reserved / undetermined |
| 4 | Keypress 1 | GameCube keycode of first simultaneous key |
| 5 | Keypress 2 | GameCube keycode of second simultaneous key |
| 6 | Keypress 3 | GameCube keycode of third simultaneous key |
| 7 | Checksum | XOR of keypress 1, keypress 2, keypress 3, and counter |

**Counter**: 4-bit value (0-15) that increments with each report. Used in checksum calculation.

**Checksum algorithm**: The checksum is computed by XOR-ing all three keypress bytes together with the 4-bit counter value. This prevents data corruption and validates that the report is intact.

### Keyboard Keycodes

The GameCube keyboard uses **proprietary keycodes** (0x00-0x61), different from USB HID:

#### Control Keys

| Keycode | Name | Description |
|---------|------|-------------|
| 0x00 | NONE | No key pressed |
| 0x06 | HOME | Home (Fn + Up) |
| 0x07 | END | End (Fn + Right) |
| 0x08 | PAGEUP | Page Up (Fn + Left) |
| 0x09 | PAGEDOWN | Page Down (Fn + Down) |
| 0x0A | SCROLLLOCK | Scroll Lock (Fn + Insert) |

#### Alphanumeric Keys

| Range | Keys |
|-------|------|
| 0x10-0x29 | A-Z |
| 0x2A-0x33 | 0-9 |

#### Function Keys

| Range | Keys |
|-------|------|
| 0x40-0x4B | F1-F12 |

#### Special Keys

| Keycode | Name | PSO Mapping (Normal / Shift) |
|---------|------|------------------------------|
| 0x34 | MINUS | - / = |
| 0x35 | CARET | ^ / ~ |
| 0x36 | YEN | \ / \| |
| 0x37 | AT | @ / ` |
| 0x38 | LEFTBRACKET | [ / { |
| 0x39 | SEMICOLON | ; / + |
| 0x3A | COLON | : / * |
| 0x3B | RIGHTBRACKET | ] / } |
| 0x3C | COMMA | , / < |
| 0x3D | PERIOD | . / > |
| 0x3E | SLASH | / / ? |
| 0x3F | BACKSLASH | \ / _ |

#### System Keys

| Keycode | Name |
|---------|------|
| 0x4C | ESC |
| 0x4D | INSERT |
| 0x4E | DELETE |
| 0x4F | GRAVE |
| 0x50 | BACKSPACE |
| 0x51 | TAB |
| 0x53 | CAPSLOCK |
| 0x54 | LEFTSHIFT |
| 0x55 | RIGHTSHIFT |
| 0x56 | LEFTCTRL |
| 0x57 | LEFTALT |
| 0x59 | SPACE |
| 0x61 | ENTER |

#### Arrow Keys

| Keycode | Name |
|---------|------|
| 0x5C | LEFT |
| 0x5D | DOWN |
| 0x5E | UP |
| 0x5F | RIGHT |

#### Japanese Layout Keys

| Keycode | Name | Description |
|---------|------|-------------|
| 0x58 | LEFTUNK1 | Muhenkan |
| 0x5A | RIGHTUNK1 | Henkan/Zenkouho |
| 0x5B | RIGHTUNK2 | Hiragana/Katakana |

These keys are specific to Japanese keyboards and may not have direct equivalents on Western keyboards.

### Keyboard Mode Switching

The GameCube console determines the device type from the PROBE response. When the console detects a keyboard (device type `0x2008`), it sends `0x54` keyboard poll commands instead of `0x40` controller poll commands.

To emulate a keyboard, a device must:
1. Respond to PROBE with device type `0x2008`
2. Handle `0x54` poll commands
3. Return 8-byte keyboard reports with valid checksums

To switch between controller and keyboard emulation at runtime, the device changes its PROBE response device type and adjusts its poll response format accordingly.

### Phantasy Star Online Key Mappings

The keyboard was designed for PSO Episode I & II. Here are the in-game key mappings:

| Function | Key | Keycode |
|----------|-----|---------|
| Move forward | W | 0x26 |
| Move backward | S | 0x22 |
| Strafe left | A | 0x10 |
| Strafe right | D | 0x13 |
| Jump | Space | 0x59 |
| Action | Enter | 0x61 |
| Menu | ESC | 0x4C |
| Chat | / | 0x3E |
| Inventory | I | 0x18 |
| Map | M | 0x1C |

---

## Timing Requirements

### Critical Timing Constraints

**Joybus bit period**: 4us (250 kbit/s)
- T1: 1us LOW pulse
- T2: 2us data period
- T3: 1us delay

**Console timing tolerance**: +/-0.5us per bit (measured)

### Reply Delay

**Controller must wait 4us** (1 bit period) after receiving a command before replying. This prevents bus contention between console and controller.

### Timeout Values

| Parameter | Value | Description |
|-----------|-------|-------------|
| Bit period | 5us | Per bit including margins |
| Receive timeout | 50us | No byte received = command timed out |
| Reset timeout | 130us | Line held too long = protocol error, reset |

---

## Implementation Notes

### Digital-Only Trigger Support

Modern controllers have different trigger types:
- **Analog triggers**: Xbox, PlayStation (send both digital bit + analog value)
- **Digital-only triggers**: Switch Pro, PS3 (send only digital bit, analog = 0)

When emulating a GameCube controller with a digital-only source, the digital button press should be converted to a full analog value (255) so that games expecting analog trigger data function correctly.

### Mouse to Analog Stick Mapping

USB mice typically report at 125-1000Hz, while the GameCube polls controllers at ~60Hz. When mapping mouse deltas to analog stick values, the higher-frequency mouse reports should be accumulated between GameCube poll cycles and clamped to the +/-127 analog range to produce smooth input.

---

## Quick Reference

### Command Summary

| Command | Hex | Description |
|---------|-----|-------------|
| PROBE | 0x00 | Probe device type -> 3 bytes (status) |
| RESET | 0xFF | Reset device -> 3 bytes (status) |
| ORIGIN | 0x41 | Calibration request -> 10 bytes |
| RECALIBRATE | 0x42 | Recalibration -> 10 bytes |
| POLL | 0x40 | Poll controller -> 8 bytes |
| **KEYBOARD** | **0x54** | **Poll keyboard -> 8 bytes** |

### Device Types

| Value | Device |
|-------|--------|
| 0x0009 | Standard controller |
| 0x0900 | WaveBird (no pairing) |
| 0x0920 | WaveBird (paired) |
| **0x2008** | **Keyboard** |
| 0x0800 | Steering wheel |
| 0x0200 | Bongos |

### Timing Summary

| Parameter | Value |
|-----------|-------|
| Bit period | 4us |
| T1 (LOW pulse) | 1us |
| T2 (data period) | 2us |
| T3 (delay) | 1us |
| Reply delay | 4us |
| Receive timeout | 50us |
| Reset timeout | 130us |

---

## Acknowledgments

- **JonnyHaystack** - [joybus-pio](https://github.com/JonnyHaystack/joybus-pio) library
- **Nintendo** - Original Joybus protocol design
- **Phantasy Star Online community** - Keyboard protocol hints

---

## References

- [N64brew Joybus Documentation](https://n64brew.dev/wiki/Joybus_Protocol)
- [Dolphin Emulator SI Documentation](https://github.com/dolphin-emu/dolphin)
- [GameCube Controller Protocol (Wiibrew)](https://wiibrew.org/wiki/GameCube_controller_protocol)
- [joybus-pio Library](https://github.com/JonnyHaystack/joybus-pio)

---

*The GameCube keyboard protocol documented here represents original reverse-engineering work. The 0x54 keyboard command and 8-byte report structure with XOR checksum were discovered through hardware analysis and testing with Phantasy Star Online Episode I & II.*
