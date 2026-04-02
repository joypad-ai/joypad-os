# Casio Loopy Output Interface

Emulates a Casio Loopy controller. Supports up to 4 players. This output interface is **experimental** -- the protocol is partially implemented with limited hardware testing.

## Protocol

- **Wire protocol**: Custom serial protocol using row/bit matrix scanning
- **PIO program**: `loopy.pio`
- **Core**: Runs on Core 1 (timing-critical)

The Loopy uses a matrix scanning approach with 6 row lines (active-low from console) and 8 bit lines (active-low output from controller). The PIO monitors row lines and presents the appropriate bit pattern for each row.

### GPIO Pins

**KB2040 (default):**

| Signal | GPIO | Notes |
|--------|------|-------|
| ROW0 | GP26 | Row scan input (consecutive group) |
| ROW1 | GP27 | |
| ROW2 | GP28 | |
| ROW3 | GP29 | GP20 on Pico W (GP29 used by CYW43) |
| ROW4 | GP18 | |
| ROW5 | GP19 | |
| BIT0-BIT7 | GP2-GP9 | Data output (consecutive 8-bit group) |

## Player Support

- **Max players**: 4

## Button Mapping

| JP_BUTTON_* | Loopy Button |
|-------------|--------------|
| `JP_BUTTON_B1` | B |
| `JP_BUTTON_B2` | A |
| `JP_BUTTON_B3` | C |
| `JP_BUTTON_B4` | D |
| `JP_BUTTON_S1` | Select |
| `JP_BUTTON_S2` | Start |
| `JP_BUTTON_DU/DD/DL/DR` | D-pad |

## Analog Mapping

Loopy controllers are fully digital. No analog axes.

## Feedback

Casio Loopy provides no feedback channel.

## Profiles

No console-specific profiles are defined. Buttons pass through unchanged.

## Development Status

- Basic protocol implemented via PIO
- Limited testing with actual Casio Loopy hardware
- Some timing issues may exist
- Community contributions welcome

The Casio Loopy (1995) was a Japan-only console with approximately 10 game titles. Documentation of the controller protocol is sparse.

## Apps Using This Output

| App | Description |
|-----|-------------|
| `usb2loopy` | USB/BT controllers to Casio Loopy |
