# Nuon Output Interface

Emulates a Nuon DVD player controller via the bidirectional Polyface serial protocol. Supports up to 4 players (expandable to 8), spinner emulation for Tempest 3000, and In-Game Reset (IGR) via hardware GPIO pins.

## Protocol

- **Wire protocol**: Polyface -- bidirectional serial with CRC error checking
- **PIO programs**: 2 state machines:
  - `polyface_send.pio` (sm1) -- Send controller data to console
  - `polyface_read.pio` (sm2) -- Read commands from console
- **Core**: Runs on Core 1 (timing-critical)
- **Polling rate**: Console polls at approximately 60Hz

The console sends read/write packets. The adapter responds to probe requests with device identification (type 3, `MAGIC = 0x4A554445` "JUDE") and returns button/analog state on controller polls. All packets include CRC-16 error checking (`CRC16 = 0x8005`).

See [Nuon Polyface Protocol](../protocols/NUON_POLYFACE.md) for wire-level details.

### GPIO Pins

| Signal | GPIO | Notes |
|--------|------|-------|
| DATAIO | GP2 | Bidirectional data line |
| CLKIN | GP3 | Clock input (consecutive with DATAIO) |
| POWER | GP4 | IGR power control |
| STOP | GP11 | IGR stop control |

## Player Support

- **Max players**: 4 (defined in `nuon_device.h`)
- **Protocol capacity**: Up to 8 players supported by the Polyface protocol

## Button Mapping

| JP_BUTTON_* | Nuon Button | Notes |
|-------------|-------------|-------|
| `JP_BUTTON_B1` | A | Primary action |
| `JP_BUTTON_B2` | C-Down | Secondary action |
| `JP_BUTTON_B3` | B | Alternate action |
| `JP_BUTTON_B4` | C-Left | C-button |
| `JP_BUTTON_L1` | L | Left shoulder |
| `JP_BUTTON_R1` | R | Right shoulder |
| `JP_BUTTON_L2` | C-Up | C-button |
| `JP_BUTTON_R2` | C-Right | C-button |
| `JP_BUTTON_S1` | Nuon | System button |
| `JP_BUTTON_S2` | Start | Start button |
| `JP_BUTTON_DU/DD/DL/DR` | D-pad | Direct mapping |

Left stick is also mapped to D-pad directions.

### Nuon Button Constants

The Nuon protocol uses non-sequential bit positions:

| Button | Bitmask | Bit Position |
|--------|---------|--------------|
| A | `0x4000` | 14 |
| B | `0x0008` | 3 |
| Start | `0x2000` | 13 |
| Nuon | `0x1000` | 12 |
| L | `0x0020` | 5 |
| R | `0x0010` | 4 |
| Up | `0x0200` | 9 |
| Down | `0x0800` | 11 |
| Left | `0x0400` | 10 |
| Right | `0x0100` | 8 |

## Analog Mapping

The Nuon protocol supports analog axes via A-to-D channel requests:

| Channel | Constant | Description |
|---------|----------|-------------|
| None | `ATOD_CHANNEL_NONE` | No analog data |
| Mode | `ATOD_CHANNEL_MODE` | Mode information |
| X1 | `ATOD_CHANNEL_X1` | Left stick X |
| Y1 | `ATOD_CHANNEL_Y1` | Left stick Y |
| X2 | `ATOD_CHANNEL_X2` | Right stick X / spinner |
| Y2 | `ATOD_CHANNEL_Y2` | Right stick Y |

## Feedback

### In-Game Reset (IGR)

Hardware-based reset via GPIO pins, triggered by button combo:

**Combo**: Hold **L1 + R1 + Start + Select**

- **Tap** (release before 2 seconds): Triggers **Stop** (STOP_PIN) -- returns to DVD menu
- **Hold** (2+ seconds): Triggers **Power** (POWER_PIN) -- powers off the player

Uses the hotkeys service for reliable combo detection with debouncing.

### Spinner Emulation (Tempest 3000)

USB mouse X-axis movement is converted to spinner rotation:
- Linear response (no acceleration)
- 1:1 mouse movement to rotation by default
- Adjustable via `NUON_SPINNER_SCALE` in firmware
- Auto-detects when a mouse is connected

## Profiles

Profiles are defined at the app level. The Nuon output exposes profile accessors through the `OutputInterface` struct.

## Apps Using This Output

| App | Description |
|-----|-------------|
| `usb2nuon` | USB/BT controllers to Nuon |
| `bt2nuon` | Bluetooth controllers to Nuon (Pico W) |
| `n642nuon` | N64 controllers to Nuon |
