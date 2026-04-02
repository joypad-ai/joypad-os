# SNES Input Interface

Reads native SNES and NES controllers via the SNESpad library using GPIO polling. Supports SNES controllers, NES controllers, SNES mouse, and Xband keyboard. Also serves as the foundation for NES reading in apps that use both protocols -- see [NES](nes.md) for the dedicated NES PIO-based driver.

## Protocol

- **Bus**: Parallel shift register (Clock, Latch, Data)
- **Method**: SNESpad library, GPIO polling from Core 0
- **Polling**: Every main loop iteration (effectively as fast as the main loop runs)
- **Location**: `src/native/host/snes/`

The SNES controller protocol uses a simple shift register interface:
1. Host pulls Latch HIGH for 12us
2. Host pulses Clock 16 times
3. Controller shifts out one bit per clock pulse on the Data line
4. Bits are active-low (0 = pressed)

## Supported Controllers

| Device | Type ID | Notes |
|--------|---------|-------|
| SNES controller | 0 | Standard 12-button pad |
| NES controller | 1 | 8-button pad (compatible protocol) |
| SNES mouse | 2 | Relative X/Y motion |
| Xband keyboard | 3 | Full keyboard |

Device type is auto-detected by the SNESpad library based on the response pattern.

## Button Mapping

### SNES Controller

| SNES Button | JP_BUTTON_* |
|-------------|-------------|
| B | B1 |
| A | B2 |
| Y | B3 |
| X | B4 |
| L | L1 |
| R | R1 |
| Select | S1 |
| Start | S2 |
| D-pad Up | DU |
| D-pad Down | DD |
| D-pad Left | DL |
| D-pad Right | DR |

### NES Controller

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

SNES controllers are purely digital -- all analog axes remain at center (128). The SNES mouse produces delta_x and delta_y values for relative motion.

## Connection Detection

The SNESpad library detects device presence based on the data line response. An all-high response (no bits pulled low) indicates no controller is connected.

## Feedback

- **Rumble**: Supported via the LRG (Limited Run Games) SNES Rumble protocol on compatible controllers. `snes_host_set_rumble()` accepts left/right motor values (0-255, scaled internally to 0-15).

### LRG Rumble Protocol

The rumble protocol piggybacks on the existing controller clock signal. On each clock pulse (same pulses used to read button data), the host drives IOBit (pin 6) with a bit from a 16-bit frame. The controller shifts these bits into a register. When the high byte matches `0x72`, the low byte is interpreted as a rumble command.

```
Bit:  15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
       0  1  1  1  0  0  1  0 |R3 R2 R1 R0|L3 L2 L1 L0|
       -------- 0x72 --------|-- Right ---|--- Left ---|
```

- Data clocked out MSB first, one bit per clock pulse
- Frame detection: `(shift_register & 0xFF00) == 0x7200`
- Right motor intensity: high nibble (0-15)
- Left motor intensity: low nibble (0-15)

**Compatibility:**

- Standard SNES controllers: Unaffected (pin 6 not wired)
- NES controllers: Unaffected (no pin 6)
- SNES Mouse: Uses IOBit for speed cycling -- rumble disabled when mouse detected
- Xband Keyboard: Uses IOBit for caps lock LED -- rumble disabled when keyboard detected

References: Mesen2 `SnesRumbleController.cpp`, [LRG SNES Rumble repo](https://github.com/LimitedRunGames-Tech/snes-rumble)

## Configuration

Default GPIO pins (overridable per-app with `#define` before including the header):

| Pin | Default GPIO | Function |
|-----|-------------|----------|
| SNES_PIN_CLOCK | 2 | Clock output |
| SNES_PIN_LATCH | 3 | Latch output |
| SNES_PIN_DATA0 | 4 | Data input (controller) |
| SNES_PIN_DATA1 | 5 | Data1 input (multitap/keyboard) |
| SNES_PIN_IOBIT | 6 | I/O bit (mouse/keyboard) |

Custom pin initialization is available via `snes_host_init_pins()`.

- **Device address range**: 0xF0+ (native input range)
- **Max ports**: 4 (port 0 active, ports 1-3 reserved for future multitap)
- **Transport type**: `INPUT_TRANSPORT_NATIVE`
- **Input source**: `INPUT_SOURCE_NATIVE_SNES`

## Apps Using This Input

- [snes2usb](../apps/snes2usb.md) -- SNES/NES controller to USB HID
- [snes23do](../apps/snes23do.md) -- SNES/NES controller to 3DO
