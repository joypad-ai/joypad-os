# 3DO Output Interface

Emulates 3DO controllers connected via the PBUS (Peripheral Bus) daisy chain protocol. Supports up to 8 players, multiple device types (joypad, joystick, mouse, arcade pad), and extension passthrough for native 3DO controllers.

## Protocol

- **Wire protocol**: PBUS -- clock-synchronized serial with daisy chain architecture
- **PIO programs**: 2 state machines:
  - `sampling.pio` (sm_sampling) -- Samples clock and control signals from console
  - `output.pio` (sm_output) -- Outputs serial data to console
- **Core**: Runs on Core 1 (timing-critical)
- **Level shifters**: Required -- RP2040 GPIO is 3.3V and NOT 5V tolerant. All signal lines (CLK, DATA_OUT, DATA_IN, CS_CTRL) need shifting.

The 3DO console clocks data out of the controller chain serially. Each controller shifts its report bits, then relays the clock to the next device. Different device types have different report sizes.

See [3DO PBUS Protocol](../protocols/3DO_PBUS.md) for wire-level details.

### GPIO Pins (RP2040-Zero)

| Signal | GPIO | Description |
|--------|------|-------------|
| CLK | GP2 | Clock input from 3DO console |
| DATA_OUT | GP3 | Data output to 3DO console |
| DATA_IN | GP4 | Data input from next controller (daisy chain) |
| CS_CTRL | GP5 | Chip Select / Control signal |

### Level Shifter Wiring

Based on [FCare's USBTo3DO](https://github.com/FCare/USBTo3DO) design. Uses a 4-channel BSS138 bidirectional level shifter (BD-LCC):

| RP2040-Zero | BD-LCC (Low) | BD-LCC (High) | DB9-Female | DB9-Male |
|-------------|--------------|---------------|------------|----------|
| 5V | - | HV | - | - |
| GND | G | G | - | - |
| GPIO 2 | L1 | H1 | Pin 1 | Pin 1 |
| GPIO 3 | L2 | H2 | Pin 2 | Pin 2 |
| GPIO 4 | LV | HV | - | - |
| GPIO 5 | G | G | Pin 6 | Pin 6 |
| GPIO 6 | L3 | H3 | Pin 3 | Pin 3 |
| GPIO 7 | L4 | H4 | Pin 4 | Pin 4 |
| - | - | - | Pin 5 (5V) | Pin 5 |
| - | - | - | Pin 9 (GND) | Pin 9 |

- **DB9-Female**: Connects to 3DO console
- **DB9-Male**: Passthrough for daisy-chaining native controllers

All signal lines require bidirectional 3.3V-to-5V level shifting. Recommended: 4-channel BSS138 bidirectional level shifter (per FCare's [USBTo3DO](https://github.com/FCare/USBTo3DO) design).

### 3DO Controller Port Pinout

| Pin | Signal | Description |
|-----|--------|-------------|
| 1 | Clock | CLK from console |
| 2 | Data Out | Data to console |
| 3 | Data In | Data from next controller in chain |
| 4 | Audio Left | Unused by adapter |
| 5 | Audio Right | Unused by adapter |
| 6 | VCC | 5V power |
| 7 | GND | Ground |
| 8 | Control Select | Chip select / control |

### Device Types

| Type | Enum | Report Size | ID |
|------|------|-------------|-----|
| Joypad | `CONTROLLER_JOYPAD` | 2 bytes (16 bits) | `0b100` (3-bit) |
| Joystick | `CONTROLLER_JOYSTICK` | 9 bytes (72 bits) | `0x01 0x7B 0x08` |
| Mouse | `CONTROLLER_MOUSE` | 4 bytes (32 bits) | `0x49` |
| Silly Pad | `CONTROLLER_SILLY` | 2 bytes (16 bits) | `0xC0` |

## Player Support

- **Max players**: 8
- **Assignment**: Automatic on connection
- **Disconnect behavior**: Players shift (SHIFT mode) -- remaining players move up to fill gaps
- **Daisy chain**: USB controllers appear first in chain, native 3DO controllers pass through via extension port

### Extension Passthrough

The adapter supports two extension port modes:

| Mode | Constant | Description |
|------|----------|-------------|
| Passthrough | `TDO_EXT_PASSTHROUGH` | Relay extension data unchanged (default) |
| Managed | `TDO_EXT_MANAGED` | Parse extension controllers through player system |

### Output Modes

| Mode | Constant | Description |
|------|----------|-------------|
| Normal | `TDO_MODE_NORMAL` | Standard joypad/joystick output |
| Silly | `TDO_MODE_SILLY` | Arcade JAMMA silly pad (Orbatak, etc.) |

Toggled via hotkey.

## Button Mapping

### Joypad (Standard Controller)

| JP_BUTTON_* | 3DO Button | Notes |
|-------------|------------|-------|
| `JP_BUTTON_B1` | B | Middle button |
| `JP_BUTTON_B2` | C | Bottom button |
| `JP_BUTTON_B3` | A | Top button |
| `JP_BUTTON_B4` | (disabled) | Not mapped in default profile |
| `JP_BUTTON_L1` | L | Left shoulder |
| `JP_BUTTON_L2` | L | Left shoulder (OR with L1) |
| `JP_BUTTON_R1` | R | Right shoulder |
| `JP_BUTTON_R2` | R | Right shoulder (OR with R1) |
| `JP_BUTTON_S1` | X | Stop button |
| `JP_BUTTON_S2` | P | Play/Pause |
| `JP_BUTTON_DU/DD/DL/DR` | D-pad | Direct mapping |

Left stick is also mapped to D-pad directions.

### Mouse

| USB Mouse | 3DO Mouse |
|-----------|-----------|
| Left Click | Left Button |
| Right Click | Right Button |
| Middle Click | Middle Button |
| Movement | Relative motion (X/Y deltas) |

### Joystick

4-axis analog support with digital buttons. Maps analog axes 1-4 to X, Y, Z (twist), and throttle. Includes a FIRE trigger button.

## Analog Mapping

The 3DO standard joypad is fully digital. When using joystick mode, 4 analog axes are transmitted as 8-bit values (0-255).

## Feedback

3DO provides no feedback channel (no rumble, no LEDs).

## Profiles

Profiles are defined at the app level (e.g., `src/apps/usb23do/profiles.h`):

- **Default (SNES-style)**: A/B/C mapped for general play
- **Fighting**: Optimized for Way of the Warrior, SFII
- **Shooter**: Optimized for Doom, PO'ed

Profile switching uses the standard Select + D-pad mechanism. Profiles persist to flash.

## Apps Using This Output

| App | Description |
|-----|-------------|
| `usb23do` | USB/BT controllers to 3DO |
| `snes23do` | SNES controllers to 3DO |
