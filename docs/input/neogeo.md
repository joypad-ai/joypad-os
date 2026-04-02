# Neo Geo / Arcade Input Interface

Reads arcade controllers and sticks via direct GPIO polling with internal pull-ups. Supports 4-8 button arcade layouts with configurable pin mapping, d-pad mode switching, and SOCD-style hotkeys. Despite the name in the index, this is a general-purpose arcade input driver used for Neo Geo sticks and custom arcade builds alike.

## Protocol

- **Bus**: Direct GPIO, active-low buttons with internal pull-ups
- **Method**: `gpio_get_all()` single read, masked and inverted
- **Polling**: Every main loop iteration (no throttle)
- **Location**: `src/native/host/arcade/`

Each button is wired to a GPIO pin. The pin is configured as input with an internal pull-up resistor. When a button is pressed, it pulls the pin LOW. The driver reads all GPIO pins in a single `gpio_get_all()` call, inverts the result (active-low to active-high), and masks to the configured pins.

## Supported Controllers

| Device | Connector | Notes |
|--------|-----------|-------|
| Neo Geo arcade stick | DB15 | 4 buttons (A-D) + Select + Start |
| Neo Geo 6-button | DB15 | 6 buttons + Select + Start |
| Custom arcade panels | Direct wire | Up to 8 action buttons + 4 system + 4 extra |

## Button Mapping

The driver uses an arcade-style layout with P (punch) and K (kick) rows:

| Arcade Input | JP_BUTTON_* | Notes |
|--------------|-------------|-------|
| P1 | B3 | Top-left (Square/Y) |
| P2 | B4 | Top-middle (Triangle/X) |
| P3 | R1 | Top-right |
| P4 | L1 | Far top-right |
| K1 | B1 | Bottom-left (Cross/A) |
| K2 | B2 | Bottom-middle (Circle/B) |
| K3 | R2 | Bottom-right |
| K4 | L2 | Far bottom-right |
| Coin/Select | S1 | System button |
| Start | S2 | System button |
| A1 | A1 | Guide/Home |
| A2 | A2 | Capture |
| L3 | L3 | Extra button |
| R3 | R3 | Extra button |
| L4 | L4 | Paddle |
| R4 | R4 | Paddle |
| D-pad | DU/DD/DL/DR | Joystick directions |

## D-pad Mode Hotkeys

Hold the button combo for 2 seconds to switch d-pad mode:

| Combo | Mode | Effect |
|-------|------|--------|
| S1 + S2 + D-Down | D-pad mode | D-pad outputs JP_BUTTON_DU/DD/DL/DR (default) |
| S1 + S2 + D-Left | Left stick mode | D-pad outputs ANALOG_LX/LY (0 or 255) |
| S1 + S2 + D-Right | Right stick mode | D-pad outputs ANALOG_RX/RY (0 or 255) |

When in stick mode, d-pad bits are cleared from the button output and converted to analog axis values (0/255 for cardinal directions).

## Analog Axes

Arcade controllers are purely digital. Analog axes are at center (128) unless d-pad mode is set to left-stick or right-stick mode, in which case d-pad directions produce full-deflection analog values.

Digital L2/R2 buttons also set ANALOG_L2/ANALOG_R2 to 255 when pressed.

## Connection Detection

The driver always reports as connected (`arcade_host_is_connected()` returns true). Since the GPIO lines have pull-ups, an unconnected pin reads as unpressed -- there is no explicit connect/disconnect detection.

## Feedback

No rumble or LED feedback from the controller. The adapter's onboard NeoPixel LED is used for status indication.

## Configuration

Pin assignment is fully configurable via `arcade_config_t`. Each button can be assigned to any GPIO, or set to `GPIO_DISABLED` (0xFF) to disable. The `PORT_CONFIG_INIT` macro initializes all pins to disabled.

Example from the neogeo2usb app (DB15 pinout):

```c
arcade_config_t config = PORT_CONFIG_INIT;
config.pin_du = 2;   // DB15 pin for Up
config.pin_dd = 3;   // Down
config.pin_dl = 4;   // Left
config.pin_dr = 5;   // Right
config.pin_p1 = 6;   // Button A
config.pin_p2 = 7;   // Button B
config.pin_k1 = 8;   // Button C
config.pin_k2 = 9;   // Button D
config.pin_s1 = 10;  // Coin/Select
config.pin_s2 = 11;  // Start
```

- **Device address**: 0xF0 (fixed)
- **Max ports**: 1
- **Device type**: `INPUT_TYPE_ARCADE_STICK`
- **Transport type**: `INPUT_TRANSPORT_NATIVE`
- **Input source**: `INPUT_SOURCE_NATIVE_ARCADE`

## Apps Using This Input

- [neogeo2usb](../apps/neogeo2usb.md) -- Neo Geo arcade stick to USB HID
