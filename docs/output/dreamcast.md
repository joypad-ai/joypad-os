# Dreamcast Output Interface

Emulates a Dreamcast controller connected via the Maple Bus protocol. Supports up to 4 players, analog triggers, and Puru Puru (rumble) feedback. Reports as a standard controller with vibration pack capability.

## Protocol

- **Wire protocol**: Maple Bus -- differential signaling on two data lines (SDCKA/SDCKB)
- **PIO program**: `maple.pio`
- **Core**: Core 1 handles real-time Maple Bus responses; Core 0 runs periodic maintenance via `dreamcast_task()`
- **Response delay**: Minimum 50us before responding to console commands

The Dreamcast console sends command frames (device info, get condition, set condition). The adapter responds with appropriate device info or controller state. The adapter identifies as a standard controller (`MAPLE_FT_CONTROLLER`) with vibration pack support (`MAPLE_FT_VIBRATION`).

### Maple Bus Commands Handled

| Command | Code | Description |
|---------|------|-------------|
| Device Info | `0x01` | Returns controller identity and function types |
| Extended Device Info | `0x02` | Returns extended device capabilities |
| Reset | `0x03` | Resets device state |
| Kill | `0x04` | Device shutdown |
| Get Condition | `0x09` | Poll controller state (buttons, sticks, triggers) |
| Set Condition | `0x0E` | Set peripheral settings (rumble motor commands) |

### GPIO Pins

### Dreamcast Controller Connector Pinout

Looking at the controller plug (male, from controller cable):

```
     ___
   /  5  \
  | 4   3 |
  | 2   1 |
   \_____/
```

| Pin | Signal | Description |
|-----|--------|-------------|
| 1 | SDCKA | Data line A |
| 2 | GND (Sense) | Ground / device detect |
| 3 | +5V | Power |
| 4 | GND | Ground |
| 5 | SDCKB | Data line B |

**KB2040 (default):**

| Signal | GPIO | Dreamcast Pin |
|--------|------|---------------|
| SDCKA (Data Line A) | GP2 | Pin 1 |
| SDCKB (Data Line B) | GP3 | Pin 5 |

**RP2040-Zero (USB4Maple-compatible):**

| Signal | GPIO | Dreamcast Pin |
|--------|------|---------------|
| SDCKA | GP14 | Pin 1 |
| SDCKB | GP15 | Pin 5 |

## Player Support

- **Max players**: 4
- **Addressing**: Each port addressed via Maple Bus port bits (0-3) in the frame header
- **Player colors**: Orange (P1), Blue (P2), Red (P3), Green (P4)

## Button Mapping

Dreamcast buttons are active-low in hardware (0 = pressed), handled internally by the driver.

| JP_BUTTON_* | DC Button | Notes |
|-------------|-----------|-------|
| `JP_BUTTON_B1` | A | Bottom face button |
| `JP_BUTTON_B2` | B | Right face button |
| `JP_BUTTON_B3` | X | Left face button |
| `JP_BUTTON_B4` | Y | Top face button |
| `JP_BUTTON_L1` | L Trigger (digital) | Left shoulder |
| `JP_BUTTON_R1` | R Trigger (digital) | Right shoulder |
| `JP_BUTTON_L2` | D button | Distinct from L, useful for N64-to-DC |
| `JP_BUTTON_R2` | R Trigger (analog) | Analog trigger |
| `JP_BUTTON_L3` | Z | Extra face button |
| `JP_BUTTON_R3` | C | Extra face button |
| `JP_BUTTON_S1` | D (2nd Start) | Arcade stick second start |
| `JP_BUTTON_S2` | Start | Start button |
| `JP_BUTTON_A1` | Start | Guide/Home also maps to Start |
| `JP_BUTTON_DU/DD/DL/DR` | D-pad | Direct mapping |

## Analog Mapping

| Input | DC Output | Range |
|-------|-----------|-------|
| Left stick X | `joy_x` | 0-255, 128 = center |
| Left stick Y | `joy_y` | 0-255, 128 = center |
| Right stick X | `joy2_x` | 0-255, 128 = center (extended controllers) |
| Right stick Y | `joy2_y` | 0-255, 128 = center (extended controllers) |
| L2 analog | `lt` | 0 = released, 255 = full |
| R2 analog | `rt` | 0 = released, 255 = full |

## Feedback

### Rumble (Puru Puru Pack)

The Dreamcast sends vibration commands via `MAPLE_CMD_SET_CONDITION` on the vibration function type. The adapter extracts:

- **Power** (0-7): Vibration intensity
- **Frequency**: Motor oscillation rate
- **Increment**: Power ramp direction

The driver converts the power level to a 0-255 rumble value routed to the input controller's vibration motor via the player manager. Detailed motor parameters are available through `dreamcast_get_purupuru_state()`.

## Profiles

Profile support is available at the app level. The Dreamcast output does not define console-specific profiles.

## Apps Using This Output

| App | Description |
|-----|-------------|
| `usb2dc` | USB/BT controllers to Dreamcast |
| `n642dc` | N64 controllers to Dreamcast |
