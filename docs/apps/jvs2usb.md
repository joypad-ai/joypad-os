# jvs2usb

JVS I/O board to USB HID gamepad.

## Overview

Reads arcade I/O boards using the JVS (JAMMA Video Standard) protocol over RS-485 and outputs as a standard USB HID gamepad. Supports up to 2 players from a single JVS I/O board. Designed for use with Naomi Universal Cabinet (NUC) with JVS I/O boards.

## Input

[JVS Input Driver](../input/jvs.md) — RS-485 UART-based host driver (jvsio library). Auto-enumerates connected I/O boards, parses capability reports, and polls digital switches and coin inputs each frame. Protocol reference: [docs/protocols/jvs.md](../protocols/jvs.md).

## Output

[USB Device Output](../output/usb-device.md) — USB HID gamepad with multiple emulation modes.

## Core Configuration

| Setting | Value |
|---------|-------|
| Routing mode | MERGE (all inputs blended to one output) |
| Player slots | 2 (fixed) |
| Max USB output ports | 2 |
| Profile system | Yes (Select+Start = Home combo) |
| Board | RP2040-Zero |

## Key Features

- **JVS host enumeration** — Automatically identifies connected I/O boards, reads capability reports (digital inputs, coin slots, analog channels), and logs board identity, JVS revision, and command revision.
- **Hot-plug detection** — Monitors the JVS sense line; reports connection status via `JVSIO_Client_isSenseConnected()`.
- **Coin as Select** — Coin insertion pulses the Select button for 200 ms per coin.
- **Test button as Home** — The JVS TEST signal (player 1 coin_slot byte bit 7) maps to `JP_BUTTON_A1` (Guide/Home).
- **Select+Start = Home combo** — Pressing Select and Start simultaneously produces a Home/Guide button press via the profile combo system.
- **USB output mode cycling** — Double-click the board button to cycle modes (SInput → XInput → PS3 → PS4 → Switch → KB/Mouse). Triple-click to reset to SInput.
- **Player LED colors** — NeoPixel LED indicates player count (green = 1P, blue = 2P, etc.).
- **Variable baud rate** — Tested with 115,200 bps standar JVS I/O speed.

## Supported Boards

| Board | Build Command |
|-------|---------------|
| RP2040-Zero | `make jvs2usb_rp2040zero` |

## Build and Flash

```bash
make jvs2usb_rp2040zero
make flash-jvs2usb_rp2040zero
```

## PCB

https://github.com/herzmx/jvs2usb

The repository includes KiCad source files, Gerber files for fabrication, and the bill of materials (BOM).

## Hardware

### PCB Overview

jvs2usb uses a dedicated PCB that integrates:

- **ISL3178EIBZ-T**: Full-duplex RS-485 transceiver. Converts between the RP2040's 3.3V UART logic and the RS-485 differential bus. Powered at 3.3V.
- **LM2903**: Dual open-collector comparator. Sense line detector circuit.
- **R5 — 120 Ω**: RS-485 bus termination resistor between A and B lines. Prevents signal reflections on the JVS bus when this board is the host in the chain.

### RP2040-Zero Pin Assignment

| GPIO | Net name | ISL3178 pin | Direction | Description |
|------|----------|-------------|-----------|-------------|
| GP6 | jvs_re | RE (pin 2) | Output | Receive Enable — active LOW |
| GP7 | jvs_de | DE (pin 3) | Output | Driver Enable — active HIGH |
| GP8 | jvs_tx | DI (pin 4) | UART | UART1 TX → RS-485 driver input |
| GP9 | jvs_rx | RO (pin 1) | UART | RS-485 receiver output → UART1 RX |
| GP12 | `sense_in_high` | LM2903 U1B out | Input | HIGH when sense line is floating (no board); LOW when board pulls sense to ground |
| GP13 | `sense_in_low` | LM2903 U1A out | Input | LOW when sense line is at a low voltage threshold |


## Button Mapping

JVS switch data arrives as two bytes per player (`sw_state0`, `sw_state1`):

### sw_state0 (byte 0)

| Bit | JVS Signal | `JP_BUTTON_*` |
|-----|------------|-------------|
| 7 | Start | S2 |
| 6 | Service | S1 |
| 5 | D-pad Up | DU |
| 4 | D-pad Down | DD |
| 3 | D-pad Left | DL |
| 2 | D-pad Right | DR |
| 1 | Button 1 (Punch 1) | B3 |
| 0 | Button 2 (Punch 2) | B4 |

### sw_state1 (byte 1)

| Bit | JVS Signal | `JP_BUTTON_*` |
|-----|------------|-------------|
| 7 | Button 3 (Punch 3) | R1 |
| 6 | Button 4 (Kick 1) | B1 |
| 5 | Button 5 (Kick 2) | B2 |
| 4 | Button 6 (Kick 3) | R2 |
| 3 | Button 7 | L1 |
| 2 | Button 8 | L2 |
| 1 | Button 9 | L3 |
| 0 | Button 10 | R3 |

### Special Inputs

| Source | Condition | `JP_BUTTON_*` | Notes |
|--------|-----------|-------------|-------|
| Coin (player i) | Coin inserted | S1 (Select) | Held for 200 ms after insertion |
| TEST (coin byte bit 7, player 1 only) | Active | A1 | Home/Guide button |
| Select + Start combo | Both held simultaneously | A1 | Profile combo via `jvs2usb_combos[]` |

## Profiles

The default profile includes a single button combo:

| Combo | Output | Type |
|-------|--------|------|
| Select (S1) + Start (S2) held together | Home (A1) | Exclusive — only fires when no other buttons are pressed |

## JVS Input

The JVS input layer uses the **jvsio** library (modified fork of [toyoshim/jvsio](https://github.com/toyoshim/jvsio)).

- Protocol specification (framing, commands, addressing): [docs/protocols/jvs.md](../protocols/jvs.md)
- Driver implementation (jvsio API, callbacks, baud rate): [docs/input/jvs.md](../input/jvs.md)


## Compatible Hardware

JVS-compliant I/O board. Tested board types include:

- SEGA ENTERPRISESLTD.;I/O BD JVS;837-13551;Ver1.00;98/10
- SEGA ENTERPRISESLTD.;I/O BD JVS;837-13844-01;Ver1.00;99/07


## Troubleshooting

**I/O board not detected**
- Check PBC right solde components

**Buttons not registering:**
- Use the USB input monitor at [config.joypad.ai](https://config.joypad.ai) to view raw button state.
