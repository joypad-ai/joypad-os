# Nuon Polyface Controller Protocol

**First Open-Source Documentation of the Nuon Polyface Protocol**

Reverse-engineered and implemented by Robert Dale Smith (2022-2023)

This document represents months of research analyzing raw protocol data from Nuon DVD player hardware. The Polyface protocol was previously completely undocumented, making this likely the only comprehensive technical reference available.

---

## Table of Contents

- [Overview](#overview)
- [Physical Layer](#physical-layer)
- [Packet Structure](#packet-structure)
- [Protocol State Machine](#protocol-state-machine)
- [Command Reference](#command-reference)
- [Device Configuration](#device-configuration)
- [Button Encoding](#button-encoding)
- [Analog Channels](#analog-channels)
- [CRC Algorithm](#crc-algorithm)
- [Timing Requirements](#timing-requirements)
- [Implementation Notes](#implementation-notes)

---

## Overview

The **Polyface protocol** is a proprietary bidirectional serial controller protocol developed by Jude Katsch for VM Labs' Nuon multimedia processor. The protocol enables communication between the Nuon DVD player and various controller types (gamepads, mice, steering wheels, fishing reels, etc.).

### Key Characteristics

- **Bidirectional**: Single data line with tri-state capability
- **Clock-synchronized**: External clock provided by Nuon hardware
- **Packet-based**: 64-bit packets with CRC-16 error detection
- **Stateful**: Device discovery and configuration through enumeration sequence
- **Extensible**: Capability-based device configuration supports many controller types

### Protocol Designer

**Magic Number: `0x4A554445` ("JUDE" in ASCII)**

This magic number honors Jude Katsch, the inventor of the Polyface protocol. It appears during device enumeration.

---

## Physical Layer

### Pin Configuration

| Pin | Name | Direction | Description |
|-----|------|-----------|-------------|
| 1 | GND | - | Ground |
| 2 | DATA | Bidirectional | Tri-state data line (3.3V/5V tolerant) |
| 3 | CLOCK | Input | Clock signal from Nuon (~variable frequency) |
| 4 | VCC | - | Power (3.3V or 5V depending on player model) |

### Electrical Characteristics

- **Data Line**: Open-drain/tri-state
  - HIGH (idle/receive): Hi-Z (pulled high by Nuon)
  - LOW (transmit): Actively driven by controller
- **Clock**: Provided by Nuon hardware
  - Frequency: Variable, typically ~100-200kHz
  - Controller samples data on rising edge
  - Controller outputs data after falling edge

### Timing

```
CLOCK:  ┐   ┌───┐   ┌───┐   ┌───┐   ┌
        └───┘   └───┘   └───┘   └───┘

DATA:   ────┐       ┌───────────┐
            └───────┘           └──── (sample on rising edge)
```

**Critical Timing Requirements**:
- Data must be stable 1 clock cycle before rising edge
- Controller must release bus (tri-state) when not transmitting
- Collision avoidance requires ~29 clock delay after receive

---

## Packet Structure

All communication uses **64-bit packets** with the following structure:

### Bit Layout (MSB first)

```
Bits 63-56: [START][CTRL] + Data Byte 0
Bits 55-48: Data Byte 1
Bits 47-40: Data Byte 2
Bits 39-32: Data Byte 3
Bits 31-16: CRC-16 Checksum
Bits 15-0:  Padding (zeros)
```

### Detailed Packet Format

```
Bit:  63  62  61-54     53-46     45-38     37-30     29-14      13-0
     ┌───┬───┬─────────┬─────────┬─────────┬─────────┬──────────┬────────────┐
     │ S │ C │ A[7:0]  │ B[7:0]  │ C[7:0]  │ D[7:0]  │  CRC16   │    PAD     │
     └───┴───┴─────────┴─────────┴─────────┴─────────┴──────────┴────────────┘
      │   │      │         │         │         │                      │
      │   │      │         └─────────┴─────────┘                      │
      │   │      │               Data bytes                           │
      │   │      └── Command/Address byte                             │
      │   └── Control bit (1=READ, 0=WRITE)                           │
      └── Start bit (always 1)                      Padding (zeros) ──┘
```

### Field Descriptions

**START (Bit 63)**: Always `1` - marks beginning of packet

**CTRL (Bit 62)**: Packet type
- `1` = READ request (Nuon requests data from controller)
- `0` = WRITE command (Nuon sends data to controller)

**A[7:0] (Bits 61-54)**: Command/Address byte
- Identifies the type of packet (ALIVE, PROBE, ANALOG, etc.)

**B[7:0], C[7:0], D[7:0]**: Data bytes (usage varies by command)

**CRC16 (Bits 31-16)**: CRC-16 checksum
- Polynomial: `0x8005`
- Calculated over data bytes only
- MSB-first bit ordering

**PAD (Bits 15-0)**: Always zero (reserved for future use)

### Example Packets

**ALIVE Request** (Nuon → Controller):
```
Binary:  11 10000000 00000000 00000000 00000000 [CRC16] 0000000000000000
Hex:     C0 80 00 00 00 XX XX 00 00
         └─ START=1, CTRL=1 (READ), CMD=0x80
```

**ALIVE Response** (Controller → Nuon):
```
Binary:  11 00000001 00000000 00000000 00000000 [CRC16] 0000000000000000
Hex:     C0 01 00 00 00 XX XX 00 00
         └─ START=1, CTRL=1, Response=0x01
```

---

## Protocol State Machine

The Nuon Polyface protocol implements a stateful enumeration and configuration sequence. Each controller must progress through these states:

### State Diagram

```
     ┌─────────┐
     │ RESET   │ (Power-on or RESET command)
     └────┬────┘
          │
          ▼
     ┌─────────┐
     │ IDLE    │ (No controller connected)
     └────┬────┘
          │ (Controller connects)
          ▼
     ┌─────────┐
     │ ALIVE   │ ◄──┐ (Nuon polls for devices)
     └────┬────┘    │
          │         │ (Periodic ALIVE queries)
          ▼         │
     ┌─────────┐    │
     │ MAGIC   │────┘ (Controller identifies as Polyface device)
     └────┬────┘
          │
          ▼
     ┌─────────┐
     │ PROBE   │ (Nuon queries device capabilities)
     └────┬────┘
          │
          ▼
     ┌─────────┐
     │ BRAND   │ (Nuon assigns unique ID)
     └────┬────┘
          │
          ▼
     ┌─────────┐
     │ ACTIVE  │ ◄──┐ (Normal operation: polling buttons/analog)
     └─────────┘    │
          │         │
          └─────────┘
```

### State Descriptions

#### 1. RESET State
- **Entry**: Power-on or RESET command (0xB1)
- **Actions**: Clear all device state variables
  - `id = 0`
  - `alive = false`
  - `tagged = false`
  - `branded = false`
  - `channel = 0`
- **Exit**: Remains in IDLE until a controller connects

#### 2. IDLE State
- **Condition**: No controller connected
- **Actions**: Ignore all Nuon commands except RESET
- **Purpose**: Prevents spurious responses when no controller present

#### 3. ALIVE State
- **Command**: `0x80 00 00`
- **First Response**: `0x01` (device present but not enumerated)
- **Subsequent**: `(id & 0x7F) << 1` (echo assigned ID)
- **Purpose**: Periodic polling to detect connected devices
- **Transition**: After first ALIVE, Nuon sends MAGIC

#### 4. MAGIC State
- **Command**: `0x90 XX XX` (dataS and dataC ignored)
- **Response**: `0x4A554445` ("JUDE" in ASCII)
- **Purpose**: Authenticates device as genuine Polyface controller
- **Notes**: Only respond before BRAND (prevents re-enumeration)

#### 5. PROBE State
- **Command**: `0x94 XX XX`
- **Response**: 32-bit device descriptor:
  ```
  Bit 31:    DEFCFG (default config, always 1)
  Bits 30-24: VERSION (firmware version, e.g., 11)
  Bits 23-16: TYPE (device type, e.g., 3 = gamepad)
  Bits 15-8:  MFG (manufacturer ID, e.g., 0)
  Bit 7:     TAGGED (1 if device has been enumerated before)
  Bit 6:     BRANDED (1 if device has received ID)
  Bits 5-1:  ID (assigned device ID, 0-31)
  Bit 0:     PARITY (even parity over entire word)
  ```
- **Purpose**: Announces device capabilities and identity

#### 6. BRAND State
- **Command**: `0xB4 00 [ID]`
- **Actions**: Store assigned ID (1-15 typical)
- **Purpose**: Nuon assigns unique ID for this session
- **Notes**: No response packet required

#### 7. ACTIVE State
- **Commands**:
  - CONFIG (0x25): Query device configuration
  - ANALOG (0x35): Query analog channel
  - CHANNEL (0x34): Set analog channel
  - SWITCH (0x30/0x31): Read button state
  - QUADX (0x32): Read spinner/wheel delta
- **Purpose**: Normal operation - Nuon polls inputs continuously

### Enumeration Sequence Example

```
Nuon → Controller: RESET (0xB1 00 00)
Controller → Nuon: (no response, device resets state)

Nuon → Controller: ALIVE (0x80 00 00)
Controller → Nuon: 0x01 (device present)

Nuon → Controller: MAGIC (0x90 XX XX)
Controller → Nuon: 0x4A554445 ("JUDE")

Nuon → Controller: PROBE (0x94 XX XX)
Controller → Nuon: 0x8B030000 (gamepad, version 11, not branded)

Nuon → Controller: BRAND (0xB4 00 05)
Controller: (stores ID=5, sets branded=true)

Nuon → Controller: CONFIG (0x25 01 00)
Controller → Nuon: 0xC0C00000 (device capabilities)

[Now in ACTIVE state - normal polling begins]
```

---

## Command Reference

### 0x80 - ALIVE

**Purpose**: Periodic polling to detect connected devices

**Request Format**:
```
A=0x80, S=0x00, C=0x00
```

**Response**:
- **First time (not alive)**: `0x01`
- **Subsequent (alive)**: `(id & 0x7F) << 1`

**Logic**:
- If not yet alive: respond with `0x01`, set alive flag
- If already alive: respond with `(id & 0x7F) << 1`

**Notes**:
- Nuon sends ALIVE continuously (~60Hz)
- Device must respond even before enumeration
- Used to detect hot-plug and disconnect

---

### 0x90 - MAGIC

**Purpose**: Authenticate device as Polyface controller

**Request Format**:
```
A=0x90, S=XX, C=XX (S and C ignored)
```

**Response**:
```
0x4A554445 ("JUDE" in ASCII)
```

**Logic**:
- Only respond before receiving BRAND command (i.e., not yet branded)
- Response: `0x4A554445`

**Notes**:
- Only respond BEFORE receiving BRAND command
- Prevents re-enumeration of already branded devices
- Honors protocol designer Jude St. John

---

### 0x94 - PROBE

**Purpose**: Query device type and capabilities

**Request Format**:
```
A=0x94, S=XX, C=XX
```

**Response**: 32-bit descriptor with even parity
```
┌──────┬─────────┬──────┬─────┬────────┬─────────┬────┬───────┐
│DEFCFG│ VERSION │ TYPE │ MFG │ TAGGED │ BRANDED │ ID │PARITY │
├──────┼─────────┼──────┼─────┼────────┼─────────┼────┼───────┤
│  1   │ 7 bits  │8 bits│8bits│  1 bit │  1 bit  │5bit│ 1 bit │
└──────┴─────────┴──────┴─────┴────────┴─────────┴────┴───────┘
 Bit 31  30-24    23-16  15-8    7         6      5-1     0
```

**Field Values**:
- **DEFCFG**: Always `1` (default configuration available)
- **VERSION**: Firmware version (e.g., `11` = v0.11)
- **TYPE**: Device type
  - `3` = Standard gamepad
  - `8` = Mouse/trackball
  - See "Device Types" section for full list
- **MFG**: Manufacturer ID (`0` = generic/homebrew)
- **TAGGED**: `1` if device was previously enumerated (across power cycles)
- **BRANDED**: `1` if device has received ID in this session
- **ID**: Assigned device ID (0-31, populated after BRAND)
- **PARITY**: Even parity bit over all 32 bits

**Response Construction**:

The 32-bit response word is assembled by placing each field at its designated bit position: DEFCFG in bit 31, VERSION (7 bits, masked to 0x7F) in bits 30-24, TYPE (8 bits) in bits 23-16, MFG (8 bits) in bits 15-8, TAGGED (1 bit) in bit 7, BRANDED (1 bit) in bit 6, ID (5 bits, masked to 0x1F) in bits 5-1, and the even parity bit in bit 0. All fields are OR'd together to form the complete response.

**Even Parity Calculation**:

Even parity is computed over all 32 bits. The parity bit (bit 0) is set such that the total number of 1-bits in the word is even.

---

### 0xB4 - BRAND

**Purpose**: Assign unique device ID

**Request Format**:
```
A=0xB4, S=0x00, C=[ID]
```

**Response**: None (write command)

**Logic**:
- Store `dataC` as the assigned device ID
- Set branded flag to true

**Notes**:
- ID range: 1-15 typical (0 reserved, 16-31 extended)
- After BRAND, device enters ACTIVE state
- ID persists until RESET or power cycle

---

### 0x25 - CONFIG

**Purpose**: Query device configuration capabilities

**Request Format**:
```
A=0x25, S=0x01, C=0x00
```

**Response**: 8-bit configuration word + CRC
```
Bits 7-0: Configuration flags
```

**Configuration Bits** (from Nuon SDK `joystick.h`):
- Bit 7: ANALOG2 support
- Bit 6: ANALOG1 support
- Bit 5: QUADSPINNER support
- Bit 4: THROTTLE support
- Bit 3: BRAKE support
- Bit 2: RUDDER/TWIST support
- Bit 1: WHEEL/PADDLE support
- Bit 0: MOUSE/TRACKBALL support

**Common Configurations**:

| Binary | Hex | Description |
|--------|-----|-------------|
| 11000000 | 0xC0 | ANALOG1 + ANALOG2 (standard dual-stick gamepad) |
| 10000000 | 0x80 | ANALOG1 only (single-stick gamepad) |
| 11010000 | 0xD0 | MOUSE/TRACKBALL |
| 10011101 | 0x9D | Full gamepad (QUADSPINNER + ANALOG1 + ANALOG2) |

---

### 0x31 - SWITCH (Extended Config)

**Purpose**: Query extended device switches/configuration

**Request Format**:
```
A=0x31, S=0x01, C=0x00
```

**Response**: 8-bit extended configuration + CRC

**Notes**: Purpose not fully reverse-engineered; appears to mirror CONFIG

---

### 0x30 - SWITCH (Button State)

**Purpose**: Read digital button state

**Request Format**:
```
A=0x30, S=0x02, C=0x00
```

**Response**: 16-bit button word + CRC
```
Bits 15-0: Button state (active LOW)
```

**Button Encoding** (see "Button Encoding" section):

| Bit | Button |
|-----|--------|
| 15 | C_DOWN |
| 14 | A |
| 13 | START |
| 12 | NUON |
| 11 | DOWN |
| 10 | LEFT |
| 9 | UP |
| 8 | RIGHT |
| 7 | (unused) |
| 6 | (unused) |
| 5 | L |
| 4 | R |
| 3 | B |
| 2 | C_LEFT |
| 1 | C_UP |
| 0 | C_RIGHT |

**Notes**:
- Buttons are **active LOW** (0 = pressed, 1 = released)
- Idle state: `0x0080` (all buttons released except reserved bits)

---

### 0x34 - CHANNEL

**Purpose**: Set analog-to-digital channel for next ANALOG read

**Request Format**:
```
A=0x34, S=0x01, C=[CHANNEL]
```

**Response**: None (write command)

**Channel Values**:

| Value | Name | Description |
|-------|------|-------------|
| 0x00 | ATOD_CHANNEL_NONE | Device mode packet |
| 0x01 | ATOD_CHANNEL_MODE | Reserved |
| 0x02 | ATOD_CHANNEL_X1 | Analog stick 1 X-axis |
| 0x03 | ATOD_CHANNEL_Y1 | Analog stick 1 Y-axis |
| 0x04 | ATOD_CHANNEL_X2 | Analog stick 2 X-axis (C-stick) |
| 0x05 | ATOD_CHANNEL_Y2 | Analog stick 2 Y-axis (C-stick) |

**Logic**: Store `dataC` as the selected channel for the next ANALOG read.

**Usage Pattern**:
```
Nuon → CHANNEL(0x02)  // Select X1
Nuon → ANALOG         // Read X1 value
Nuon → CHANNEL(0x03)  // Select Y1
Nuon → ANALOG         // Read Y1 value
...
```

---

### 0x35 - ANALOG

**Purpose**: Read analog value from previously selected channel

**Request Format**:
```
A=0x35, S=0x01, C=0x00
```

**Response**: Varies by channel
- **Channel 0x00**: Device mode packet (capabilities)
- **Channel 0x02-0x05**: 8-bit analog value + CRC

**Analog Value Format**:
```
0x00 = Full left/down
0x80 = Center (neutral)
0xFF = Full right/up
```

**Logic**: Response depends on the currently selected channel:
- Channel 0x00 (NONE): Return device mode/capabilities packet
- Channel 0x02 (X1): Return left stick X-axis value
- Channel 0x03 (Y1): Return left stick Y-axis value
- Channel 0x04 (X2): Return right stick X-axis value
- Channel 0x05 (Y2): Return right stick Y-axis value

**Device Mode Packet** (Channel 0x00):
```
24-bit capability descriptor (from Nuon SDK joystick.h)
Examples:
  0x9D = CTRLR_ANALOG1 | CTRLR_ANALOG2 | CTRLR_STDBUTTONS | CTRLR_DPAD | ...
  0xC0 = CTRLR_ANALOG1 | CTRLR_ANALOG2
  0x80 = CTRLR_ANALOG1 only
```

---

### 0x32 - QUADX (Spinner/Wheel)

**Purpose**: Read quadrature spinner delta (e.g., Tempest 3000)

**Request Format**:
```
A=0x32, S=0x02, C=0x00
```

**Response**: 8-bit signed delta + CRC
```
-128 to +127 (movement since last query)
```

**Notes**:
- Used for spinner/mouse input in games such as Tempest 3000
- Value represents accumulated movement delta since last query, then resets
- Positive = clockwise, Negative = counterclockwise

---

### 0x27 - REQUEST (Address)

**Purpose**: Address-related request (exact purpose unclear)

**Request Format**:
```
A=0x27, S=0x01, C=0x00
```

**Response**: Varies by current channel
- If channel is MODE (0x01): respond with `0xF4` + CRC
- Otherwise: respond with `0xF6` + CRC

**Notes**: Exact purpose not fully understood from reverse-engineering

---

### 0x84 - REQUEST_B

**Purpose**: Secondary request sequence (possibly initialization)

**Request Format**:
```
A=0x84, S=0x04, C=0x40
```

**Response**: Bit pattern based on a request counter. The controller cycles through a 12-bit pattern, looping bits 7-11 after the initial sequence:

**Bit Pattern**:
```
Requests:  0   1   2   3   4   5   6   7   8   9  10  11
Response:  0   0   1   1   0   0   1   0   0   1   0   1
           │                           └─ loops back
           └─ Pattern: 0b101001001100 (bits 0-11)
```

**Notes**: Purpose unclear - appears to be part of initialization handshake

---

### 0x88 - ERROR

**Purpose**: Error condition or invalid request

**Request Format**:
```
A=0x88, S=0x04, C=0x40
```

**Response**: `0x00` (error/no data)

---

### 0x99 - STATE

**Purpose**: Read/write device state (purpose not fully understood)

**Request Format**:
```
A=0x99, S=0x01, C=[DATA]
Type bit (bit 25) determines READ (1) or WRITE (0)
```

**Response** (if READ):
- If accumulated state equals `0x4151`: respond with `0xD1028E00`
- Otherwise: respond with `0xC0002800`

**Write Operation**: Accumulates a 16-bit state value by shifting the current state left by 8 bits and OR-ing in the received data byte (dataC).

**Notes**: Exact purpose unclear - may relate to advanced features or calibration

---

### 0xB1 - RESET

**Purpose**: Reset controller to initial state

**Request Format**:
```
A=0xB1, S=0x00, C=0x00
```

**Response**: None

**Actions**: Clear all device state:
- `id` = 0
- `alive` = false
- `tagged` = false
- `branded` = false
- `state` = 0
- `channel` = 0

**Notes**: Resets the device to its initial power-on state, requiring full re-enumeration

---

## Device Configuration

The Nuon Polyface protocol supports a wide variety of controller types through a capability-based configuration system. Device capabilities are announced during enumeration via the **PROBE**, **CONFIG**, and **ANALOG(channel=0)** commands.

### Device Capability Flags

From the Nuon SDK `joystick.h`, devices can announce support for:

| Flag Name | Value | Description |
|-----------|-------|-------------|
| CTRLR_STDBUTTONS | 0x000001 | A, B, START buttons |
| CTRLR_DPAD | 0x000002 | D-Pad (up/down/left/right) |
| CTRLR_SHOULDER | 0x000004 | L/R shoulder buttons |
| CTRLR_EXTBUTTONS | 0x000008 | Extended buttons (C-buttons, NUON, etc.) |
| CTRLR_ANALOG1 | 0x000010 | Primary analog stick (X1/Y1) |
| CTRLR_ANALOG2 | 0x000020 | Secondary analog stick (X2/Y2) |
| CTRLR_WHEEL | 0x000040 | Steering wheel/paddle |
| CTRLR_THROTTLE | 0x000100 | Throttle axis |
| CTRLR_BRAKE | 0x000200 | Brake axis |
| CTRLR_TWIST | 0x000400 | Rudder/twist axis |
| CTRLR_MOUSE | 0x000800 | Mouse/trackball movement |
| CTRLR_QUADSPINNER1 | 0x001000 | Quadrature spinner #1 |
| CTRLR_THUMBWHEEL1 | 0x004000 | Thumbwheel #1 |
| CTRLR_THUMBWHEEL2 | 0x008000 | Thumbwheel #2 |
| CTRLR_FISHINGREEL | 0x010000 | Fishing reel input |

### Example Device Configurations

#### Standard Gamepad (Dual Analog)

| Field | Value |
|-------|-------|
| Properties | 0x0000103F |
| Device Mode | 0x9D (binary: 10011101) |
| Config | 0xC0 (binary: 11000000) |
| Switch | 0xC0 (binary: 11000000) |

Capabilities: QUADSPINNER1 (for mouse mode), ANALOG1 (left stick), ANALOG2 (right stick / C-stick), STDBUTTONS (A, B, START), DPAD (up/down/left/right), SHOULDER (L, R), EXTBUTTONS (C-buttons, NUON)

#### Mouse/Trackball

| Field | Value |
|-------|-------|
| Properties | 0x0000083F |
| Device Mode | 0x9D (binary: 10011101) |
| Config | 0xD0 (binary: 11010000) |
| Switch | 0x80 (binary: 10000000) |

Capabilities: MOUSE (XY movement via QUADX), ANALOG1, ANALOG2, STDBUTTONS, DPAD, SHOULDER, EXTBUTTONS

#### Single Analog Gamepad

| Field | Value |
|-------|-------|
| Properties | 0x0000001F |
| Device Mode | 0xB9 (binary: 10111001) |
| Config | 0x80 (binary: 10000000) |
| Switch | 0x80 (binary: 10000000) |

Capabilities: ANALOG1 only, STDBUTTONS, DPAD, SHOULDER, EXTBUTTONS

#### Racing Wheel

| Field | Value |
|-------|-------|
| Properties | 0x0000034F |
| Device Mode | 0xB9 (binary: 10111001) |
| Config | 0x80 (binary: 10000000) |
| Switch | 0x00 (binary: 00000000) |

Capabilities: BRAKE, THROTTLE, WHEEL/PADDLE, STDBUTTONS, DPAD, SHOULDER, EXTBUTTONS

#### Flight Stick

| Field | Value |
|-------|-------|
| Properties | 0x0000051F |
| Device Mode | 0x80 (binary: 10000000) |
| Config | 0x80 (binary: 10000000) |
| Switch | 0x80 (binary: 10000000) |

Capabilities: RUDDER/TWIST, THROTTLE, ANALOG1, STDBUTTONS, DPAD, SHOULDER, EXTBUTTONS

### Device Type Codes

**TYPE** field in PROBE response:

| Type Code | Device |
|-----------|--------|
| 0 | Unknown/Generic |
| 1 | Keyboard |
| 2 | Mouse |
| 3 | Gamepad/Joystick (most common) |
| 4 | Steering Wheel |
| 5 | Flight Stick |
| 6 | Fishing Rod |
| 7 | Light Gun |
| 8 | Trackball |

### Configuration Encoding

Device capabilities are encoded across three 8-bit configuration bytes sent during enumeration:

**device_mode** (Response to ANALOG with channel=0):
- Bits 7-0: Primary capability flags
- Most significant capabilities
- Example: `0x9D` = QUADSPINNER + ANALOG1 + ANALOG2

**device_config** (Response to CONFIG command):
- Bits 7-0: Secondary capability flags
- Analog/axis configuration
- Example: `0xC0` = ANALOG1 + ANALOG2

**device_switch** (Response to SWITCH[16:9] command):
- Bits 7-0: Tertiary capability flags
- Extended features
- Example: `0xC0` = (mirrors device_config in standard gamepad)

---

## Button Encoding

### Button Bit Layout (16-bit, Active LOW)

```
Bit │ Button    │ Binary    │ Hex    │ Description
────┼───────────┼───────────┼────────┼─────────────────────────
15  │ C_DOWN    │ 0x8000    │ 0x8000 │ C-Down (R-stick down)
14  │ A         │ 0x4000    │ 0x4000 │ A button (primary action)
13  │ START     │ 0x2000    │ 0x2000 │ START button
12  │ NUON      │ 0x1000    │ 0x1000 │ NUON button (Z / Home)
11  │ DOWN      │ 0x0800    │ 0x0800 │ D-Pad Down
10  │ LEFT      │ 0x0400    │ 0x0400 │ D-Pad Left
 9  │ UP        │ 0x0200    │ 0x0200 │ D-Pad Up
 8  │ RIGHT     │ 0x0100    │ 0x0100 │ D-Pad Right
 7  │ (reserved)│ 0x0080    │ 0x0080 │ Always 1 (unused)
 6  │ (reserved)│ 0x0040    │ 0x0040 │ Always 1 (unused)
 5  │ L         │ 0x0020    │ 0x0020 │ L shoulder button
 4  │ R         │ 0x0010    │ 0x0010 │ R shoulder button
 3  │ B         │ 0x0008    │ 0x0008 │ B button (secondary)
 2  │ C_LEFT    │ 0x0004    │ 0x0004 │ C-Left (R-stick left)
 1  │ C_UP      │ 0x0002    │ 0x0002 │ C-Up (R-stick up)
 0  │ C_RIGHT   │ 0x0001    │ 0x0001 │ C-Right (R-stick right)
```

### Idle State

**No buttons pressed**: `0x0080` (bits 7 and 6 reserved as high)

### Active LOW Logic

**IMPORTANT**: Buttons are active LOW (inverse logic):
- `0` = Button PRESSED
- `1` = Button RELEASED

Examples:
- **A button pressed** (all others released): The idle state `0x0080` with bit 14 cleared gives `0xBFFF`.
- **A + START + L pressed**: The idle state with bits 14, 13, and 5 cleared gives `0x9FDF`.

### C-Button Mapping

The **C-buttons** (C-Up, C-Down, C-Left, C-Right) correspond to the **right analog stick** on modern controllers:

- **C-Up**: Right stick up
- **C-Down**: Right stick down
- **C-Left**: Right stick left
- **C-Right**: Right stick right

This mapping originated with the N64 controller and was adopted by Nuon.

### Button State Packet

Buttons are sent via the **SWITCH (0x30)** command as a 16-bit value with CRC.

CRC is calculated over 2 data bytes, resulting in a 32-bit packet:
```
[Byte0][Byte1][CRC_High][CRC_Low]
```

---

## Analog Channels

Analog values are read through a **channel-based** system. The Nuon must first set the channel (via CHANNEL command), then read the value (via ANALOG command).

### Channel IDs

| Channel | Name | Description |
|---------|------|-------------|
| 0x00 | ATOD_CHANNEL_NONE | Device mode (capabilities packet) |
| 0x01 | ATOD_CHANNEL_MODE | Reserved (purpose unknown) |
| 0x02 | ATOD_CHANNEL_X1 | Left stick X-axis |
| 0x03 | ATOD_CHANNEL_Y1 | Left stick Y-axis |
| 0x04 | ATOD_CHANNEL_X2 | Right stick X-axis (C-stick) |
| 0x05 | ATOD_CHANNEL_Y2 | Right stick Y-axis (C-stick) |

### Analog Value Range

All analog values are **8-bit unsigned**:

```
Value │ Meaning
──────┼────────────────────
0x00  │ Full left/down
0x80  │ Center (neutral)
0xFF  │ Full right/up
```

### Y-Axis Convention

The Nuon expects Y-axis values where **0 = down** and **255 = up**. This is the inverse of the HID standard (0 = up, 255 = down). Input sources using HID convention must invert Y-axis values before sending to the Nuon.

### Reading Analog Values (Nuon's Perspective)

Typical polling sequence:

```
1. Nuon → CHANNEL(0x02)     // Select left stick X
2. Nuon → ANALOG            // Read X1 value (e.g., 0x80)

3. Nuon → CHANNEL(0x03)     // Select left stick Y
4. Nuon → ANALOG            // Read Y1 value (e.g., 0x7F)

5. Nuon → CHANNEL(0x04)     // Select right stick X
6. Nuon → ANALOG            // Read X2 value (e.g., 0x80)

7. Nuon → CHANNEL(0x05)     // Select right stick Y
8. Nuon → ANALOG            // Read Y2 value (e.g., 0x80)
```

### Device Mode Query

Channel `0x00` returns a **device mode packet** instead of an analog value:

```
Nuon → CHANNEL(0x00)
Nuon → ANALOG
Controller → 0x9DC0C000 (24-bit capability descriptor + CRC)
```

This packet describes the controller's capabilities (see "Device Configuration").

### CRC Encoding

Each analog value is sent as a 1-byte data packet with CRC appended:

```
Format: [DataByte][CRC_High][CRC_Low][Padding]
Example: 0x80 → 0x8086C300
```

---

## CRC Algorithm

The Polyface protocol uses **CRC-16** for error detection with a custom polynomial.

### CRC-16 Polynomial

The polynomial is `0x8005`, representing x^16 + x^15 + x^2 + 1. The initial CRC accumulator value is zero. Bits are processed MSB-first.

### CRC Calculation

CRC is calculated **over data bytes only** (not including start/control bits or CRC itself).

#### Lookup Table Generation

The 256-entry lookup table is generated as follows: for each index 0 through 255, start with the index value shifted left by 8 bits (into the high byte of a 16-bit word). Then process 8 iterations (one per bit). In each iteration, if the most significant bit (bit 15) is set, shift left by 1 and XOR with the polynomial `0x8005`; otherwise, just shift left by 1. Mask the result to 16 bits after all 8 iterations. The final value is stored as the table entry for that index.

#### CRC Computation

To compute the CRC for a single byte: XOR the high byte of the current CRC accumulator with the data byte to produce an 8-bit index. Look up that index in the table to get a 16-bit intermediate value. Then XOR that intermediate value with the current CRC accumulator shifted left by 8 bits. Mask the result to 16 bits. This process is applied sequentially to each data byte in the packet, starting with a CRC accumulator of zero.

#### Data Packet Creation

To create a data packet, the data bytes are placed into the most significant positions of a 32-bit word (packed left-aligned), and the CRC-16 is computed incrementally over each data byte from most significant to least significant. The resulting 16-bit CRC is then placed immediately after the last data byte in the 32-bit word. Any remaining bits are zero-padded.

For example, a 1-byte value occupies byte 3 (bits 31-24), with the CRC in bytes 2-1 (bits 23-8) and zero padding in byte 0. A 2-byte value occupies bytes 3-2, with the CRC in bytes 1-0.

### CRC Examples

**1-byte data** (e.g., analog value `0x80`):
```
Data:   0x80
CRC:    0x86C3
Packet: 0x8086C300
        └─┬─┘└─┬──┘└─ padding
          data  CRC
```

**2-byte data** (e.g., buttons `0x0080`):
```
Data:   0x0080
CRC:    0x4631
Packet: 0x00804631
        └──┬──┘└─┬──┘
          data   CRC
```

### CRC Verification

To verify received data:
1. Extract data bytes
2. Recalculate CRC
3. Compare with received CRC bytes
4. If match → valid packet
5. If mismatch → request retransmit or ignore

---

## Timing Requirements

### Clock Synchronization

All communication is synchronized to the **CLOCK** signal provided by the Nuon:

- **Clock Frequency**: ~100-200 kHz (varies by Nuon player model)
- **Sample Point**: Data sampled on **rising edge** of CLOCK
- **Output Point**: Data output after **falling edge** of CLOCK

### Collision Avoidance Delay

After receiving a packet from the Nuon, the controller must delay approximately **29 clock cycles** before transmitting to avoid bus collision. This delay ensures the Nuon has released the data line (tri-state) before the controller drives it.

### Packet Transmission Timing

A complete 64-bit packet transmission takes:

```
2 bits (START + CTRL) + 32 bits (data) + 16 bits (CRC) + 16 bits (padding) = 66 bits

At 100 kHz clock:
66 bits × 10 μs/bit = 660 μs per packet

At 200 kHz clock:
66 bits × 5 μs/bit = 330 μs per packet
```

### Polling Rate

Typical Nuon polling rate: **~60 Hz** (16.67 ms per frame)

Each frame, the Nuon may query:
- SWITCH (buttons)
- CHANNEL + ANALOG × 4 (both sticks, X and Y)
- QUADX (if mouse/spinner enabled)

Total: ~6-8 packets per frame → ~100-130 μs @ 200 kHz

### Receive and Transmit Sequence

A controller implementation needs to handle both receive and transmit on the shared data line:

**Receive**:
- Wait for START bit (bit 63 = 1)
- Read 33 bits (1 control + 32 data) synchronized to rising clock edges
- Total: 33 clock cycles per received packet

**Transmit**:
- Delay ~29 clock cycles after receiving (collision avoidance)
- Transmit START + CTRL (2 bits)
- Transmit DATA (32 bits)
- Transmit zeros (16 bits padding)
- Release data line to Hi-Z (tri-state)
- Total: ~80 clock cycles per transmitted packet

---

## Implementation Notes

### Tri-State Data Line

The **DATA** pin must support tri-state operation. When not transmitting, the controller must release the bus (Hi-Z) so the Nuon can drive it. When transmitting, the controller actively drives the line.

### Big-Endian Byte Ordering

Packets are transmitted **MSB-first** (big-endian). On little-endian architectures, byte order must be reversed before transmission.

### Spinner/Mouse Support

The protocol supports spinner and mouse input via the **QUADX (0x32)** command. The QUADX value represents a **signed 8-bit delta** since last query:
- Positive = clockwise rotation
- Negative = counterclockwise rotation
- Range: -128 to +127

This is used by games such as Tempest 3000 for spinner/wheel input.

### Incompletely Understood Areas

1. **STATE command** (0x99) - Purpose not fully reverse-engineered
2. **REQUEST commands** (0x27, 0x84) - Exact behavior unclear
3. **TAGGED flag** - Persistence mechanism unknown (may require EEPROM)
4. **Advanced device types** (fishing reel, light gun) - Not tested

---

## Acknowledgments

This protocol documentation was made possible through extensive reverse-engineering efforts:

- **Hardware Analysis**: Logic analyzer captures of Nuon → Controller communication
- **SDK Research**: Examination of leaked Nuon SDK headers (`joystick.h`)
- **Trial and Error**: Months of iterative testing with real Nuon hardware
- **Community Support**: Nuon homebrew community feedback and testing

**Special Thanks**:
- **Jude St. John** - Polyface protocol designer (MAGIC = "JUDE")
- **VM Labs / Nuon Community** - For preserving development materials

---

## Appendix: Quick Reference Tables

### Command Quick Reference

| Cmd | Name | Type | Purpose | Response |
|-----|------|------|---------|----------|
| 0x80 | ALIVE | R | Device detection | 0x01 or ID |
| 0x84 | REQUEST_B | R | Unknown sequence | Bit pattern |
| 0x88 | ERROR | R | Error state | 0x00 |
| 0x90 | MAGIC | R | Authentication | 0x4A554445 |
| 0x94 | PROBE | R | Query capabilities | Device descriptor |
| 0x99 | STATE | R/W | Read/write state | State-dependent |
| 0x25 | CONFIG | R | Query config | 8-bit config |
| 0x27 | REQUEST | R | Address request | 0xF4 or 0xF6 |
| 0x30 | SWITCH | R | Read buttons | 16-bit buttons |
| 0x31 | SWITCH | R | Extended config | 8-bit config |
| 0x32 | QUADX | R | Read spinner | 8-bit delta |
| 0x34 | CHANNEL | W | Set analog channel | None |
| 0x35 | ANALOG | R | Read analog value | 8-bit or mode |
| 0xB1 | RESET | W | Reset device | None |
| 0xB4 | BRAND | W | Assign ID | None |

**R** = Read (Nuon requests data)
**W** = Write (Nuon sends data)

### Button Bit Reference

| Bit | Button | Hex | Game Usage |
|-----|--------|-----|------------|
| 15 | C_DOWN | 0x8000 | Camera down, R-stick down |
| 14 | A | 0x4000 | Primary action (jump, fire) |
| 13 | START | 0x2000 | Pause/menu |
| 12 | NUON | 0x1000 | Home/system menu |
| 11 | DOWN | 0x0800 | Move down |
| 10 | LEFT | 0x0400 | Move left |
| 9 | UP | 0x0200 | Move up |
| 8 | RIGHT | 0x0100 | Move right |
| 5 | L | 0x0020 | Left shoulder |
| 4 | R | 0x0010 | Right shoulder |
| 3 | B | 0x0008 | Secondary action |
| 2 | C_LEFT | 0x0004 | Camera left, R-stick left |
| 1 | C_UP | 0x0002 | Camera up, R-stick up |
| 0 | C_RIGHT | 0x0001 | Camera right, R-stick right |

### Analog Channel Reference

| Ch | Name | Axis | Range | Center |
|----|------|------|-------|--------|
| 0x00 | NONE | Mode packet | N/A | N/A |
| 0x02 | X1 | Left stick X | 0-255 | 128 |
| 0x03 | Y1 | Left stick Y | 0-255 | 128 |
| 0x04 | X2 | Right stick X | 0-255 | 128 |
| 0x05 | Y2 | Right stick Y | 0-255 | 128 |

**Note**: Y-axes are inverted (0=down, 255=up)

---

## References

- **Nuon SDK**: `joystick.h` (leaked development headers)

---

**Document Version**: 1.0
**Last Updated**: 2025-11-19
**Author**: Robert Dale Smith
**License**: Apache 2.0

*This is the first comprehensive open-source documentation of the Nuon Polyface controller protocol. All information was derived through reverse-engineering of hardware and analysis of leaked SDK materials.*
