# GPIO Input Interface (Custom Controllers)

Reads buttons and analog inputs from custom-wired GPIO controllers. This is the input path for bespoke controller builds like the Fisher Price controller, Alpakka gamepad, and MacroPad, where physical buttons are wired directly to MCU pins.

## Protocol

- **Bus**: Direct GPIO pins (digital buttons) and ADC (analog sticks)
- **Method**: Pad input system (`src/apps/controller/pad/`) with configurable device definitions
- **Polling**: Every main loop iteration
- **Location**: `src/apps/controller/pad/`

The pad input system uses a configuration struct (`pad_config_t`) that describes each controller's physical layout: which GPIO pins connect to which buttons, whether analog sticks or encoders are present, LED configurations, speaker pins, display connections, and more.

## Supported Controller Types

| Controller | Config File | Notes |
|------------|-------------|-------|
| Fisher Price V1 | `pad/configs/fisherprice.h` | Toy controller conversion |
| Fisher Price V2 | `pad/configs/fisherprice.h` | Updated wiring |
| Alpakka | `pad/configs/alpakka.h` | Open-source gamepad with analog sticks |
| MacroPad | `pad/configs/macropad.h` | Button/encoder macro pad |

New controller types are added by creating a config header defining the `pad_config_t` struct and selecting it via a `CONTROLLER_TYPE_*` compile-time define.

## Button Mapping

GPIO controllers map directly to JP_BUTTON_* constants. The mapping is defined per-controller in the pad config. The [arcade input interface](neogeo.md) provides similar GPIO-based input for arcade-style builds.

## Features

- **UART Link**: Controllers can be linked via QWIIC/UART for bidirectional input sharing between two boards
- **I2C Peer**: STEMMA QT / QWIIC I2C connection for slave mode (serves local inputs to a master)
- **Speaker/Haptics**: Configurable speaker pin for rumble feedback via audio
- **Display**: SPI display support for mode indication and button visualization
- **NeoPixel LEDs**: Per-button LED illumination with press-reactive lighting
- **Konami Code**: Button sequence detection with audio/visual feedback
- **USB Output Modes**: Double-click board button to cycle modes; triple-click to reset

## Configuration

Controller type is selected at compile time:

```bash
make controller_fisherprice_v1_kb2040
make controller_alpakka_kb2040
make controller_macropad_rp2040zero
```

- **Input source**: `INPUT_SOURCE_GPIO`
- **Router mode**: `ROUTING_MODE_SIMPLE`, single player per output

## Apps Using This Input

- [controller](../apps/controllers.md) -- Custom GPIO controllers to USB HID
- [controller_btusb](../apps/controllers.md) -- Custom GPIO controllers with Bluetooth bridge
