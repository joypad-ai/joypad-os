# PCEngine / TurboGrafx-16 Output Interface

Emulates a PCEngine multitap with up to 5 controllers. Supports 2-button, 3-button, and 6-button controller modes as well as mouse emulation. Uses three PIO state machines for timing-critical multiplexed output on Core 1.

## Protocol

- **Wire protocol**: Multiplexed parallel bus -- 4 data lines, 1 select line, 1 clock/OE line
- **PIO programs**: 3 state machines on PIO0:
  - `plex.pio` (sm1) -- Data multiplexing on D0-D3
  - `clock.pio` (sm2) -- Monitors CLR/OE edges for scan timing
  - `select.pio` (sm3) -- Monitors SEL line for nibble toggle
- **Core**: Runs on Core 1 (timing-critical)

The console scans controller ports sequentially. Each scan cycle toggles the SEL line to read two nibbles per controller (buttons are split across two 4-bit reads). The CLR/OE line resets the scan to port 0.

### PCEngine Controller Port (8-pin DIN)

| Pin | Signal | Description |
|-----|--------|-------------|
| 1 | VCC | 5V power |
| 2 | D0 | Data bit 0 (Up/I) |
| 3 | D1 | Data bit 1 (Right/II) |
| 4 | D2 | Data bit 2 (Down/Select) |
| 5 | D3 | Data bit 3 (Left/Run) |
| 6 | SEL | Select -- nibble toggle from console |
| 7 | CLR/OE | Clear/Output Enable -- scan reset from console |
| 8 | GND | Ground |

Pin 7 is labeled **OE** (Output Enable) in some references and **CLR** (Clear) in others. They are the same signal.

See [PCEngine Protocol Reference](../protocols/PCENGINE.md) for wire-level details.

### GPIO Pins

**KB2040 (default):**

| Signal | GPIO | Notes |
|--------|------|-------|
| D0 | GP26 | Data bit 0 (consecutive out group) |
| D1 | GP27 | Data bit 1 |
| D2 | GP28 | Data bit 2 |
| D3 | GP29 | Data bit 3 |
| SEL | GP18 | Select/nibble toggle (input from console) |
| CLR/OE | GP19 | Clear/Output Enable (input from console) |

**Pico:**

| Signal | GPIO |
|--------|------|
| D0-D3 | GP4-GP7 |
| SEL | GP18 |
| CLR/OE | GP19 |

Note: The source code uses legacy names `DATAIN_PIN` (GP18) for SEL and `CLKIN_PIN` (GP19) for CLR/OE.

## Player Support

- **Max players**: 5 (PCEngine multitap)
- **Assignment**: Players assigned in order of connection (first connected = Player 1)
- **Persistence**: Player slots persist until disconnect; no shifting on disconnect

## Button Mapping

### Standard Controller (6-button)

| JP_BUTTON_* | PCEngine Button | Notes |
|-------------|-----------------|-------|
| `JP_BUTTON_B1` | II | Primary action |
| `JP_BUTTON_B2` | I | Secondary action |
| `JP_BUTTON_B3` | IV (Turbo II) | Auto-fire version of II |
| `JP_BUTTON_B4` | III (Turbo I) | Auto-fire version of I |
| `JP_BUTTON_L1` | VI | 6-button mode only |
| `JP_BUTTON_R1` | V | 6-button mode only |
| `JP_BUTTON_S1` | Select | Select button |
| `JP_BUTTON_S2` | Run | Start/Run button |
| `JP_BUTTON_DU/DD/DL/DR` | D-pad | Direct mapping |

### Button Modes

| Mode | Constant | Buttons Active |
|------|----------|----------------|
| 2-button | `BUTTON_MODE_2` | I, II only |
| 6-button | `BUTTON_MODE_6` | I through VI |
| 3-button (Select) | `BUTTON_MODE_3_SEL` | I, II, Select |
| 3-button (Run) | `BUTTON_MODE_3_RUN` | I, II, Run |

### Mouse Mapping

| USB Mouse | PCEngine Mouse |
|-----------|----------------|
| Left Click | Left Button |
| Right Click | Right Button |
| Movement | Movement (1:1) |

Mouse is auto-detected when a USB mouse is connected -- no manual mode switching needed.

## Analog Mapping

PCEngine controllers are fully digital. Left stick input is converted to D-pad directions.

## Feedback

PCEngine provides no feedback channel (no rumble, no LEDs).

## Profiles

Profile support is available at the app level. The PCEngine output itself does not define console-specific profiles.

## Apps Using This Output

| App | Description |
|-----|-------------|
| `usb2pce` | USB/BT controllers to PCEngine/TurboGrafx-16 |
