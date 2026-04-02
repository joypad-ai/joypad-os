# LodgeNet Protocol

Wire protocol reference for LodgeNet hotel gaming controllers. Two sub-protocols exist: MCU (for N64/GameCube controllers with a microcontroller) and SR (for SNES controllers with shift registers).

Reference: Nielk1's lodgenet-gc-adapter-rp2040 for connector pinout and protocol details.

## Pins

| Pin | Function |
|-----|----------|
| CLK1 | Clock (both protocols) |
| CLK2 | Second clock (SR protocol only) |
| DATA | Bidirectional data (active-low, active-low presence) |
| VCC | Controller power |

All pins idle HIGH with pull-ups enabled. PIO clock divider = 125 (1 us per cycle).

## MCU Protocol (N64/GameCube)

Used for controllers with a LodgeNet microcontroller. Host clocks data out of the controller MSB-first.

### Hello Sequence

Before each read, the host sends a two-pulse hello on CLK1:

| Phase | CLK1 | Duration |
|-------|------|----------|
| Pulse 1 LOW | LOW | 7 us |
| Pulse 1 HIGH | HIGH | 7 us |
| Pulse 2 LOW | LOW | 7 us |
| Pulse 2 HIGH | HIGH | 27 us |

### Bit Clocking

After the hello, the host clocks N bits (typically 80 bits = 10 bytes):

| Phase | CLK1 | Duration | Action |
|-------|------|----------|--------|
| Clock LOW | LOW | 6 us | Sample DATA at end (6th us) |
| Clock HIGH | HIGH | 30 us | Idle between bits |

### Byte Layout (10 bytes)

| Byte | Bits | Content |
|------|------|---------|
| 0 | 7: A (N64) / B (GC), 6: B (N64) / A (GC), 5: Z, 4: Start, 3-0: D-pad (encoded) |
| 1 | 7: has_mcu flag, 6: is_gc flag, 5: L, 4: R, 3: Y (GC) / C-Up (N64), 2: X (GC) / C-Down (N64), 1: C-Left (N64), 0: forced_fail (GC) / C-Right (N64) |
| 2 | Left stick X (uint8 or int8) |
| 3 | Left stick Y (uint8 or int8, inverted for HID) |
| 4-5 | Right stick X/Y (GC only, uint8) |
| 6-7 | L/R triggers (GC only, uint8) |
| 8-9 | Reserved |

### Detection Flags

- `bytes[1] & 0x80` -- has_mcu: controller present
- `bytes[1] & 0x40` -- is_gc: GameCube (vs N64)
- `bytes[1] & 0x01` -- forced_fail: GC controller reports error (invalidates read when is_gc is set)

### D-pad Encoding

The lower 4 bits of byte 0 encode d-pad and virtual LodgeNet buttons (active-low). When opposing directions are simultaneously pressed, the value encodes a virtual button:

| Value | Meaning |
|-------|---------|
| 0x0F | Reset |
| 0x0C | Menu |
| 0x03 | * |
| 0x0D | Select |
| 0x0B | Order |
| 0x0E | # |

### Timing

- Poll interval: 16 ms (~60 Hz)
- Read timeout: `num_bytes * 8 * 50 + 5000` us
- Auto-push at 8 bits (left shift, MSB-first)
- Connection requires 15 consecutive good reads; disconnection after 5 consecutive failures

## SR Protocol (SNES)

Used for SNES controllers connected via a dual-clock shift register.

### Bit Clocking

The host drives two clocks (CLK1 via side-set, CLK2 via set pin) to shift out 16 data bits plus 1 presence bit:

| Phase | CLK1 | CLK2 | Duration | Action |
|-------|------|------|----------|--------|
| Latch/shift | LOW | HIGH | 3 us | Parallel load / shift |
| Inter-clock | HIGH | HIGH | 20 us | Wait |
| Read setup | HIGH | LOW | 25 us | Wait for data to settle |
| Sample | HIGH | LOW | -- | Read DATA pin |

After 16 data bits, one extra clock pulse reads the presence bit (17th bit).

### Bit Layout (16 bits, active-low)

```
Bit:  15  14  13  12  11  10   9   8   7   6   5   4   3   2   1   0
      M   O   B   Y   S   *   Up  Dn  1   1   Lt  Rt  A   X   L   R
```

| Bit | Button |
|-----|--------|
| 15 | Menu |
| 14 | Order |
| 13 | B |
| 12 | Y |
| 11 | Select |
| 10 | Start (*) |
| 9 | D-Up |
| 8 | D-Down |
| 7-6 | Always 1 (unused) |
| 5 | D-Left |
| 4 | D-Right |
| 3 | A |
| 2 | X |
| 1 | L |
| 0 | R |

### Presence Detection

The 17th bit (after the extra clock pulse) indicates controller presence:
- LOW = controller connected
- HIGH = no controller

### Timing

- Poll interval: 7620 us (~131 Hz)
- Auto-push at 8 bits; 3 bytes received (16 data bits + 1 presence bit, padded)
- Read timeout: 5000 us
- Disconnection after 5 consecutive failures; falls back to MCU protocol

## Protocol Auto-Detection

The host starts in MCU mode. On 5 consecutive MCU read failures, it switches to SR mode (and vice versa). Only one PIO state machine is used; programs are swapped on protocol change.
