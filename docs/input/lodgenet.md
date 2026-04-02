# LodgeNet Input Interface

Reads Nintendo LodgeNet hotel gaming controllers via proprietary serial protocols over an RJ11 connector. Supports N64, GameCube, and SNES controller variants with automatic detection and hot-swap between types.

## Protocol

Two protocol families, auto-detected and swapped dynamically:

### MCU Protocol (N64 / GameCube)

- **Bus**: 3-wire serial (Clock, Data, VCC) over RJ11
- **Method**: PIO state machine (`lodgenet_mcu_program`)
- **Polling**: ~60Hz (`MCU_POLL_INTERVAL_US = 16000us`)
- **Data**: Hello pulse sequence, then 80 bits clocked MSB-first (10 bytes)
- **Detection**: Byte 1 bit 7 = MCU present, bit 6 = GC (set) vs N64 (clear)
- **Stability**: Requires 15 consecutive good reads before outputting (prevents connection flash)

### SR Protocol (SNES)

- **Bus**: Dual-clock shift register (CLK1 + CLK2 + Data) over RJ11
- **Method**: PIO state machine (`lodgenet_sr_program`)
- **Polling**: ~131Hz (`SR_POLL_INTERVAL_US = 7620us`)
- **Data**: 16 data bits + 1 presence bit
- **Presence**: Data line LOW after shift = controller present

### Auto-Detection

The driver cycles between protocols when communication fails:
- MCU fails 5 times consecutively -> switch to SR protocol
- SR fails 5 times consecutively -> switch to MCU protocol
- PIO program is unloaded and reloaded on each switch (single SM shared)

**Location**: `src/native/host/lodgenet/`

## Supported Controllers

| Device | Protocol | Detection |
|--------|----------|-----------|
| LodgeNet N64 controller | MCU | Byte 1 bit 6 clear |
| LodgeNet GameCube controller | MCU | Byte 1 bit 6 set |
| LodgeNet SNES controller | SR | Presence bit in SR read |

Hot-swap between all three types without reboot.

## LodgeNet Connector

The LodgeNet system uses a 6-pin RJ11-style connector. Connector diagram via [Nielk1](https://x.com/Nielk1):

```
 +-+-+-+-+-+-+-+       GAME        TV                MTI
 | | | | | | | |   1   DATA(in)    CLK(out)          IR(in)
 | 1 2 3 4 5 6 |   2   CLK(out)    MTI(in)           GND
 |             |   3   12V         N/C               DATA(in)
 |             |   4   CLK2(out)   DATA(out)         N/C
 +---+---+---+-+   5   GND         GND               MTI(out)
     +---+         6   IR          IR(out)           CLK(in)
```

Only 4 lines from the GAME port are needed:

| RJ11 Pin | Signal | Description |
|----------|--------|-------------|
| 1 | DATA | Controller data output (active-low, pull-up) |
| 2 | CLK | Host clock output (idle HIGH) |
| 3 | 12V | Power input (use 5V from Pico VBUS instead) |
| 4 | CLK2 | Second clock (SNES SR protocol only) |
| 5 | GND | Ground |

## GPIO Pinout

Default pins (overridable per-app):

| GPIO | Function | Default |
|------|----------|---------|
| LODGENET_PIN_DATA | Data input (pull-up) | 7 |
| LODGENET_PIN_CLOCK | CLK1 output (idle HIGH) | 5 |
| LODGENET_PIN_CLOCK2 | CLK2 output (SR only) | 5 |
| LODGENET_PIN_VCC | VCC output (powers controller) | 4 |

Custom pin initialization via `lodgenet_host_init_pins()`.

## Button Mapping -- N64 LodgeNet

| LodgeNet N64 | JP_BUTTON_* | Notes |
|--------------|-------------|-------|
| A | B1 | |
| B | B3 | |
| C-Down | B2 | |
| C-Left | B4 | |
| C-Up | L3 | |
| C-Right | R3 | |
| Z | R1 | |
| L | L2 | |
| R | R2 | |
| Start | S2 | |
| D-pad | DU/DD/DL/DR | Decoded from encoded SOCD d-pad |
| Menu (U+D encoded) | A1 | Home button |
| Select (U+D+R encoded) | S1 | Back button |

### Encoded D-pad and Virtual Buttons

The MCU protocol encodes the d-pad and LodgeNet system buttons using SOCD (Simultaneous Opposing Cardinal Directions) combinations in the low nibble of byte 0. Physical d-pad directions that are impossible on a real d-pad encode virtual buttons:

| Encoded Value | Meaning |
|---------------|---------|
| 0x0F | Reset |
| 0x0C | Menu (U+D) |
| 0x03 | * |
| 0x0D | Select (U+D+R) |
| 0x0B | Order |
| 0x0E | # |

The driver tracks `last_dpad` to separate real d-pad state from encoded virtual button presses.

## Button Mapping -- GameCube LodgeNet

| LodgeNet GC | JP_BUTTON_* | Notes |
|-------------|-------------|-------|
| A | B2 | |
| B | B1 | |
| X | B4 | |
| Y | B3 | |
| Z | R1 | |
| L | L2 | |
| R | R2 | |
| Start | S2 | |
| D-pad | DU/DD/DL/DR | Decoded from encoded d-pad |
| Main Stick | ANALOG_LX/LY | Y inverted: `255 - raw` |
| C-Stick | ANALOG_RX/RY | Y inverted: `255 - raw` |
| L Trigger | ANALOG_L2 | 0-255 analog |
| R Trigger | ANALOG_R2 | 0-255 analog |
| Menu (U+D encoded) | A1 | Home button |
| Select (U+D+R encoded) | S1 | Back button |

Layout reported as `LAYOUT_GAMECUBE`.

## Button Mapping -- SNES LodgeNet

| LodgeNet SNES | JP_BUTTON_* | Notes |
|---------------|-------------|-------|
| B | B1 | |
| A | B2 | |
| Y | B3 | |
| X | B4 | |
| L | L1 | |
| R | R1 | |
| Select | S1 | |
| Start | S2 | |
| D-pad Up | DU | Filtered by SOCD detection |
| D-pad Down | DD | Filtered by SOCD detection |
| D-pad Left | DL | Filtered by SOCD detection |
| D-pad Right | DR | Filtered by SOCD detection |
| Menu | A1 | Home button |
| Order | A2 | Capture button |
| Minus (U+D SOCD) | A3 | |
| Plus (L+R SOCD) | A4 | |

Layout reported as `LAYOUT_NINTENDO_4FACE`. SOCD filtering prevents simultaneous opposing directions from being output as d-pad; instead they produce the Minus and Plus virtual buttons.

## Analog Axes

### N64 LodgeNet

N64 stick is read as signed 8-bit and scaled from +/-80 to full range (same scaling as [N64 host](n64.md)):

```
scaled = (raw * 127) / 80
output = scaled + 128
```

Y-axis inverted: `stick_ly = 255 - (scaled_y + 128)`.

No right stick or triggers -- ANALOG_RX/RY at center (128), ANALOG_L2/R2 at 0.

### GameCube LodgeNet

Full analog: main stick, C-stick (both Y-inverted), L/R triggers (0-255).

### SNES LodgeNet

Purely digital -- all analog axes at center (128).

## Connection Detection

- MCU: Valid read requires byte 1 bit 7 set (MCU present) and no forced-fail flag
- SR: Presence bit after the 16 data bits (LOW = present)
- Disconnect after 5 consecutive failed reads, then protocol switch
- On connect/disconnect, state is logged and cleared input submitted

## Feedback

No rumble or LED feedback -- LodgeNet controllers are passive devices.

## Configuration

- **Device address**: 0xF0 (fixed, single port)
- **Transport type**: `INPUT_TRANSPORT_NATIVE`
- **Input source**: `INPUT_SOURCE_NATIVE_LODGENET`
- **PIO**: PIO0, single SM shared between MCU and SR programs (swapped on protocol change)

## Apps Using This Input

- [lodgenet2usb](../apps/lodgenet2usb.md) -- LodgeNet controllers to USB HID
