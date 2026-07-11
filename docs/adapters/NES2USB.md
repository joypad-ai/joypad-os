# NES to USB Adapter

NES controller to USB HID gamepad adapter. Connect an original NES controller and get [USB Output Interface](./USB2USB.md).

## Features

### 🎮 NES Controller Support

- Reads original NES controllers natively via PIO
- 60 Hz polling rate with precise fractional timing correction
- Automatic controller connect/disconnect detection (500ms debounce)
- Stuck-button prevention on disconnect (sends cleared input state)

### 🔌 USB Output Modes

Multiple USB output modes via board button:

- **Double-click** board button to cycle modes
- **Triple-click** to reset to SInput (default)
- Mode persists across power cycles

See [USB Output Interface](./USB2USB.md) for full mode details (SInput, XInput, PS3, PS4, Switch, Keyboard/Mouse).

### 🌐 Web Configuration

Connect the adapter to a computer and open **[config.joypad.ai](https://config.joypad.ai)** in a WebSerial-capable browser (Chrome, Edge) for mode switching, input monitoring, and firmware updates.

## Button Mapping

| NES Button | Joypad Output | W3C Mapping |
|------------|---------------|-------------|
| B | B1 | Cross / A |
| A | B2 | Circle / B |
| Select | S1 | Back / Select |
| Start | S2 | Start |
| D-Pad Up | D-Up | D-Up |
| D-Pad Down | D-Down | D-Down |
| D-Pad Left | D-Left | D-Left |
| D-Pad Right | D-Right | D-Right |

> **Note:** The NES controller is digital-only. Analog stick axes are centered (128) at all times.

## Hardware Requirements

- **Board**: Adafruit KB2040 (default), Pico W
- **Protocol**: NES shift register via PIO at 1 MHz instruction clock
- **Connector**: NES controller port (7-pin)

## Pin Configuration

### NES Controller Port (7-pin)

```
Pin 1: GND
Pin 2: Clock (CLK)
Pin 3: Latch (LATCH)
Pin 4: Data Out (DATA)
Pin 5: N/C
Pin 6: N/C
Pin 7: VCC (5V)
```

### Wiring — KB2040 (default)

| NES Pin | Signal | KB2040 GPIO |
|---------|--------|-------------|
| 1 | GND | GND |
| 2 | CLK | GP5 |
| 3 | LATCH | GP6 |
| 4 | DATA | GP8 |
| 7 | VCC (5V) | VBUS |

> **Note:** The DATA pin has an internal pull-up enabled in firmware. No external pull-up resistor is required.

### Pin Customization

Pins can be overridden per board by defining these macros before including the NES host driver:

| Macro | Default | Description |
|-------|---------|-------------|
| `NES_PIN_CLOCK` | GPIO 5 | Clock output to controller |
| `NES_PIN_LATCH` | GPIO 6 | Latch output to controller |
| `NES_PIN_DATA0` | GPIO 8 | Data input from controller |

## Technical Details

### NES Protocol (PIO)

The NES controller uses a parallel-to-serial shift register. The adapter drives CLK and LATCH signals and reads 8 bits from the DATA line:

1. **Latch** — Assert LATCH high for 12 cycles (12 us) to capture button state
2. **Read loop** — For each of 8 buttons:
   - Sample DATA while CLK is low (6 cycles)
   - Pulse CLK high (5 cycles)
   - Advance to next bit
3. Buttons are **active-low** (inverted to active-high in firmware)

### Connection Detection

- An idle NES controller pulls the DATA line **LOW**
- An empty port reads **HIGH** (internal pull-up)
- State changes are debounced for 500ms to avoid false triggers
- On disconnect, a cleared input event is sent to prevent stuck buttons

### Dual-Core Architecture

- **Core 0**: Timer callback triggers PIO, main loop processes input events
- **Core 1**: USB device stack (timing-critical)

### Polling Timing

- Base period: 16,666 us (60 Hz)
- Fractional correction: +1 us on 40 out of every 60 callbacks
- Maintains accurate 60.000 Hz average over time

## Build & Flash

```bash
# Build for KB2040
make nes2usb_kb2040

# Build for Pico W
make nes2usb_pico_w

# Flash (macOS - looks for /Volumes/RPI-RP2)
make flash-nes2usb_kb2040
make flash-nes2usb_pico_w
```

Output: `releases/joypad_<commit>_nes2usb_<board>.uf2`

## Troubleshooting

**Controller not detected:**
- Check NES port wiring, especially GND and VCC
- Verify CLK, LATCH, and DATA pin assignments match your board
- Ensure the NES controller is fully seated in the connector

**No response from buttons:**
- Confirm the adapter is powered (NeoPixel LED should be lit)
- Try a different NES controller to rule out a faulty controller
- Check that the DATA line has continuity from the controller port to GP8

**Wrong buttons or garbled input:**
- Verify CLK and LATCH are not swapped
- Check for cold solder joints on signal lines
- Use [config.joypad.ai](https://config.joypad.ai) input monitor to view raw input state

**USB not recognized by host:**
- Double-click the board button to cycle USB output mode
- Triple-click to reset to SInput (default HID mode)
- Try a different USB cable or port

## Product Links

- [GitHub Releases](https://github.com/joypad-ai/joypad-os/releases) - Latest firmware
