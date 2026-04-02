# NES Input Interface

Reads native NES controllers via a dedicated PIO state machine. The PIO program generates Latch and Clock signals and captures the 8-bit shift register response from the controller.

## Protocol

- **Bus**: Parallel shift register (Clock, Latch, Data) -- same electrical interface as SNES but 8 bits
- **Method**: PIO state machine (`nes_host_program`) with timer-driven 60Hz triggering
- **Polling**: 60Hz via repeating timer callback with fractional correction for precise timing
- **Location**: `src/native/host/nes/`

The PIO program handles the full latch-clock-read cycle:
1. A repeating timer fires at 60Hz and sets a PIO IRQ flag
2. PIO waits for the IRQ, then generates the Latch pulse and 8 Clock pulses via sideset pins
3. Data bits are shifted in (active-low) and delivered to the RX FIFO
4. A PIO0 IRQ0 handler reads the FIFO in interrupt context for minimal latency

The timer uses fractional accumulation (`1000000 % 60 = 40`) to maintain exactly 60.000Hz on average.

## Supported Controllers

| Device | Notes |
|--------|-------|
| Standard NES controller | 8 buttons (A, B, Select, Start, D-pad) |

## Button Mapping

| NES Button | JP_BUTTON_* |
|------------|-------------|
| B | B1 |
| A | B2 |
| Select | S1 |
| Start | S2 |
| D-pad Up | DU |
| D-pad Down | DD |
| D-pad Left | DL |
| D-pad Right | DR |

## Analog Axes

NES controllers are purely digital. All analog axes remain at center (128).

## Connection Detection

The driver samples the data line state before each PIO trigger. An unconnected pin stays HIGH due to the internal pull-up, while a connected NES controller pulls data LOW at idle.

- **Connect**: Data line held LOW for 500ms (`NES_DEBOUNCE_US`)
- **Disconnect**: Data line held HIGH for 500ms
- State changes are timestamp-debounced since `nes_host_task()` runs much faster than 60Hz
- On disconnect, cleared input is submitted to prevent stuck buttons

## Feedback

No rumble or LED feedback.

## Configuration

Default GPIO pins (overridable per-app):

| Pin | Default GPIO | Function |
|-----|-------------|----------|
| NES_PIN_CLOCK | 5 | Clock output (PIO sideset bit 0) |
| NES_PIN_LATCH | 6 | Latch output (PIO sideset bit 1) |
| NES_PIN_DATA0 | 8 | Data input (pull-up enabled) |

PIO clock is set to 1MHz instruction rate for correct timing.

### NES Controller Port (7-pin)

| Pin | Signal | Description |
|-----|--------|-------------|
| 1 | GND | Ground |
| 2 | CLK | Clock |
| 3 | LATCH | Latch |
| 4 | DATA | Data out from controller |
| 5 | N/C | Not connected |
| 6 | N/C | Not connected |
| 7 | VCC | 5V power |

### KB2040 Wiring

| NES Pin | Signal | KB2040 GPIO |
|---------|--------|-------------|
| 1 | GND | GND |
| 2 | CLK | GP5 |
| 3 | LATCH | GP6 |
| 4 | DATA | GP8 |
| 7 | VCC (5V) | VBUS |

The DATA pin has an internal pull-up enabled in firmware. No external pull-up resistor is required.

- **Device address range**: 0xF0+ (port 0 = 0xF0)
- **Max ports**: 1
- **Transport type**: `INPUT_TRANSPORT_NATIVE`
- **Input source**: `INPUT_SOURCE_NATIVE_NES`
- **Layout**: `LAYOUT_UNKNOWN`

## Relationship to SNES Input

The NES and SNES shift register protocols are electrically compatible (NES uses 8 bits, SNES uses 16). The [SNES input interface](snes.md) can also read NES controllers via the SNESpad library. This dedicated NES driver uses PIO for more precise timing and ISR-based data capture.

## Apps Using This Input

- [nes2usb](../apps/nes2usb.md) -- NES controller to USB HID
- [snes2usb](../apps/snes2usb.md) -- also reads NES controllers (via SNESpad, not this PIO driver)
- [snes23do](../apps/snes23do.md) -- also reads NES controllers (via SNESpad)
