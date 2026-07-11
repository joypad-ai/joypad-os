# SNES Rumble Support (LRG Protocol)

## Overview

Support for the Limited Run Games SNES Rumble protocol in snes2usb mode.
When a USB host (PC/emulator) sends rumble commands, joypad-os forwards them
to the connected SNES rumble controller via the IOBit pin (pin 6).

## Protocol (reverse-engineered from Mesen2 source)

The SNES rumble protocol piggybacks on the existing controller clock signal:

1. On **each clock pulse** (same pulses used to read button data), the
   controller samples **IOBit (pin 6)** and shifts it into a 16-bit register
2. When the high byte matches the magic value `0x72`, the low byte is
   interpreted as a rumble command
3. The rumble byte contains **two 4-bit motor intensities**:
   - High nibble (bits 7-4) = right motor (0-15)
   - Low nibble (bits 3-0) = left motor (0-15)

### Frame Format

```
Bit:  15 14 13 12 11 10  9  8  7  6  5  4  3  2  1  0
       0  1  1  1  0  0  1  0 |R3 R2 R1 R0|L3 L2 L1 L0|
       -------- 0x72 --------|-- Right ---|--- Left ---|
```

### Timing

- Data is clocked out MSB first on IOBit, synchronized with each $4016 read
- The controller shifts in one bit per clock pulse
- After 16 clocks (normal button read cycle), a complete frame is available
- Frame detection: `(shift_register & 0xFF00) == 0x7200`

## Implementation Plan

### 1. SNESpad Library (`src/lib/SNESpad/src/snespad.c`)

Add rumble state and IOBit output during clock cycles:

```c
// In snespad_t struct (snespad_c.h):
uint16_t rumble_frame;      // 16-bit frame to shift out (0x72XX)
uint8_t  rumble_bit_pos;    // Current bit position (15..0)
bool     rumble_active;     // Whether we have a frame to send
uint8_t  rumble_left;       // Current left motor intensity (0-15)
uint8_t  rumble_right;      // Current right motor intensity (0-15)
```

Modify `snespad_clock_bit()` to set IOBit before each clock:

```c
static uint32_t snespad_clock_bit(snespad_t* pad, uint8_t data_pin)
{
    uint32_t ret;

    // Set IOBit for rumble data BEFORE clock pulse
    if (pad->rumble_active) {
        uint8_t bit = (pad->rumble_frame >> pad->rumble_bit_pos) & 1;
        gpio_write(pad->iobit_pin, bit);
        if (pad->rumble_bit_pos == 0) {
            pad->rumble_bit_pos = 15;  // Wrap around for continuous sending
        } else {
            pad->rumble_bit_pos--;
        }
    }

    gpio_write(pad->clock_pin, 0);
    delay_us(12);

    ret = gpio_read(data_pin);

    gpio_write(pad->clock_pin, 1);
    delay_us(12);

    return ret;
}
```

Add public API:

```c
// Set rumble intensity (0-15 each motor, scaled from 0-255 input)
void snespad_set_rumble(snespad_t* pad, uint8_t left, uint8_t right);
```

```c
void snespad_set_rumble(snespad_t* pad, uint8_t left, uint8_t right)
{
    // Scale 0-255 → 0-15
    uint8_t l = left >> 4;
    uint8_t r = right >> 4;

    // Only update if changed
    if (l == pad->rumble_left && r == pad->rumble_right) return;

    pad->rumble_left = l;
    pad->rumble_right = r;

    // Build frame: 0x72 header + rumble byte
    pad->rumble_frame = 0x7200 | (r << 4) | l;
    pad->rumble_bit_pos = 15;
    pad->rumble_active = (l > 0 || r > 0);

    // If motors off, send one final frame with zeros then stop
    if (!pad->rumble_active) {
        pad->rumble_frame = 0x7200;  // 0x72 + 0x00
        pad->rumble_active = true;   // Send one more frame to clear
        // Will be deactivated after frame completes (or on next set_rumble)
    }
}
```

### 2. SNES Host Driver (`src/native/host/snes/snes_host.c`)

Add function to forward rumble to SNESpad:

```c
void snes_host_set_rumble(uint8_t port, uint8_t left, uint8_t right)
{
    if (!initialized || port >= SNES_MAX_PORTS) return;
    snespad_set_rumble(&snes_pads[port], left, right);
}
```

### 3. snes2usb App (`src/apps/snes2usb/app.c`)

Add rumble forwarding in `app_task()` (same pattern as gc2usb):

```c
#include "core/services/players/feedback.h"

void app_task(void)
{
    // Forward rumble from USB host to SNES controller
    if (usbd_output_interface.get_feedback) {
        output_feedback_t fb;
        if (usbd_output_interface.get_feedback(&fb) && fb.dirty) {
            feedback_set_rumble(0, fb.rumble_left, fb.rumble_right);
        }
    }

    // Apply feedback to SNES controller
    feedback_state_t* state = feedback_get_state(0);
    if (state && state->rumble_dirty) {
        snes_host_set_rumble(0, state->rumble.left, state->rumble.right);
        feedback_clear_dirty(0);
    }
}
```

## Data Flow

```
USB Host (PC/Emulator)
  → USB HID output report (rumble)
  → usbd_output_interface.get_feedback()
  → feedback_set_rumble(player, left, right)
  → feedback_state_t.rumble_dirty = true
  → snes_host_set_rumble(port, left, right)
  → snespad_set_rumble(pad, left, right)
  → IOBit toggled on each clock pulse during snespad_poll()
  → SNES Rumble Controller reads IOBit, drives motors
```

## Compatibility

- **Standard SNES controllers**: Unaffected. They don't wire pin 6, so
  IOBit toggling is ignored completely.
- **NES controllers**: Unaffected. No pin 6 connection.
- **SNES Mouse**: Uses IOBit for speed cycling. Rumble should be disabled
  when mouse is detected (`pad->type == SNESPAD_MOUSE`).
- **Xband Keyboard**: Uses IOBit for caps lock LED. Rumble should be
  disabled when keyboard is detected (`pad->type == SNESPAD_KEYBOARD`).

## References

- Mesen2 source: `Core/SNES/Input/SnesRumbleController.cpp`
- BlueRetro SNES rumble: `darthcloud/BlueRetro` discussion #1274
- SNES controller pinout: Pin 6 = IOBit, via register $4201
- LRG repo: `github.com/LimitedRunGames-Tech/snes-rumble`
