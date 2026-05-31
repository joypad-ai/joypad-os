# PSX Input Interface

Reads native PlayStation 1 / PlayStation 2 controllers over the PSX SIO bus. Auto-detects the controller type each poll and decodes every standard PSX peripheral.

## Protocol

- **Bus**: PSX SIO -- CLK, ATT, CMD, DAT (open-drain DAT with pull-up)
- **Method**: RP2040 PIO + DMA, hardware-paced 500 kHz clock with active pull-up on DAT
- **Polling**: ~250 Hz, paced by the PIO state machine; CPU never bit-bangs
- **Location**: `src/native/host/psx/`

The PSX SIO is a clocked serial bus where the host (us) clocks command bytes out on CMD while the controller clocks reply bytes back on DAT. Each transaction begins by pulling ATT low, sends `0x01 0x42` plus motor bytes, then clocks 17 reply bytes. The first reply byte's high nibble identifies the controller type, the low nibble gives the data-halfword count.

The active pull-up technique briefly drives DAT high (weak 2 mA) at the start of each bit before releasing it back to input. This wins on a released '1' bit against cable capacitance, but loses to the controller's strong pull-down on a '0' bit, so the read stays correct -- and old analog pads (notably Sony SCPH-110) finally read clean at the 500 kHz clock instead of clipping at the 0x00 / 0xFF extremes.

## Supported Controllers

| Device | Type ID | Notes |
|--------|---------|-------|
| Digital pad | 0x41 | Standard SCPH-1080 |
| DualShock | 0x73 | Analog mode; auto-enabled via config command |
| DualShock 2 | 0x79 | 12-byte per-button pressure (passed to PS3 output mode) |
| neGcon | 0x23 | Namco NPC-101; twist + I/II/L analog buttons |
| Dual Analog flight | 0x53 | SCPH-1110 / Dual Analog "red" mode |
| GunCon | 0x63 | Namco light gun; screen X/Y |
| JogCon | 0xE3 | Namco paddle wheel; experimental recenter FF |
| PlayStation Mouse | 0x12 | SCPH-1090; relative X/Y + 2 buttons |
| Config mode | 0xF3 | Transient response during analog/pressure enable |

## Configuration

The host sends the standard DualShock unlock sequence (enter config -> set analog mode -> enable rumble -> enable pressure -> exit config). Pads that ignore some of these (digital pad, mouse, GunCon, neGcon) simply stay in their default mode; pads that accept them (DualShock, DS2, JogCon) transition into their richest reporting mode.

## Wiring

Per-board pin assignments are defined in `src/CMakeLists.txt` under the `psx2usb` target. CLK and ATT must remain consecutive GPIOs (the PIO side-set uses both); CMD and DAT can be on any free pins. The QT Py and KB2040 variants both use A0=CLK, A1=ATT, A2=CMD, A3=DAT (GP26-29).
