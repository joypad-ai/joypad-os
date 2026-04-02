# GameCube Input Interface

Reads native GameCube controllers via the joybus-pio library. Supports the full analog complement: two sticks and two analog triggers. The single-wire joybus protocol is implemented in PIO for precise timing.

## Protocol

- **Bus**: Joybus single-wire bidirectional (open-drain with pull-up)
- **Method**: PIO state machine via `joybus-pio` library (`src/lib/joybus-pio`)
- **Polling**: 125Hz (GameCube native rate, configurable via `GC_POLLING_RATE`)
- **Location**: `src/native/host/gc/`

The GameCube joybus protocol is similar to [N64](n64.md) but with a larger response:
1. Host sends poll command with rumble state embedded in the command
2. Controller responds with 64 bits of button, stick, and trigger data
3. Same bit encoding as N64 (timed pulses on a single wire)

## Supported Controllers

| Device | Type ID | Notes |
|--------|---------|-------|
| Standard GC controller | 0x0009 | 2 sticks, 2 triggers, 12 buttons |
| GC keyboard | 0x2008 | Keyboard controller |

## Button Mapping

| GC Button | JP_BUTTON_* | Notes |
|-----------|-------------|-------|
| A | B2 | Right face button |
| B | B1 | Bottom face button |
| X | B4 | Top face button |
| Y | B3 | Left face button |
| Z | R1 | Shoulder button |
| L (digital click) | L2 | Full press past analog range |
| R (digital click) | R2 | Full press past analog range |
| Start | S2 | |
| D-pad Up | DU | |
| D-pad Down | DD | |
| D-pad Left | DL | |
| D-pad Right | DR | |

The controller layout is reported as `LAYOUT_GAMECUBE`.

## Analog Axes

GameCube controllers provide full analog data on 6 axes:

| Axis | Source | Range | Notes |
|------|--------|-------|-------|
| ANALOG_LX | Main stick X | 0-255 | 128 = center |
| ANALOG_LY | Main stick Y | 0-255 | Y inverted: `255 - raw` |
| ANALOG_RX | C-stick X | 0-255 | 128 = center |
| ANALOG_RY | C-stick Y | 0-255 | Y inverted: `255 - raw` |
| ANALOG_L2 | L trigger analog | 0-255 | 0 = released |
| ANALOG_R2 | R trigger analog | 0-255 | 0 = released |

### Y-Axis Inversion

GameCube sticks use Nintendo convention (0=down, 255=up). The driver inverts both Y axes: `stick_y = 255 - report.stick_y` and `cstick_y = 255 - report.cstick_y` to match HID convention (0=up, 255=down).

### Analog Triggers

The L and R triggers provide both analog (0-255 progressive) and digital (click at full depression) values. The analog values map to ANALOG_L2/ANALOG_R2, while the digital clicks map to JP_BUTTON_L2/JP_BUTTON_R2.

## Connection Detection

- **Connect**: `GamecubeController_IsInitialized()` returns true after successful status command
- **Disconnect debounce**: 30 consecutive failed polls before reporting disconnect
- On disconnect, cleared input (centered sticks, zero triggers, no buttons) is submitted
- Connection state changes are logged

## Feedback

- **Rumble**: Binary on/off, embedded directly in the poll command. The rumble bit is included in every poll packet, so no separate rumble command is needed.
- Rumble state is tracked per-port and updated from the feedback system

## Configuration

| Setting | Default | Override |
|---------|---------|----------|
| GC_PIN_DATA | GPIO 2 | `#define GC_PIN_DATA <pin>` |
| GC_POLLING_RATE | 125 Hz | `#define GC_POLLING_RATE <hz>` |
| GC_MAX_PORTS | 1 | (future adapter/multitap) |

PIO assignment: PIO0, auto-assigned SM and offset.

- **Device address range**: 0xD0+ (port 0 = 0xD0)
- **Transport type**: `INPUT_TRANSPORT_NATIVE`
- **Input source**: `INPUT_SOURCE_NATIVE_GC`

## Apps Using This Input

- [gc2usb](../apps/gc2usb.md) -- GameCube controller to USB HID
