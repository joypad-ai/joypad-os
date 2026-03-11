# nuon2usb — Nuon Controller to USB HID

Read a real Nuon controller (Polyface peripheral) and output as USB HID gamepad. The reverse of `usb2nuon`.

## Status

Work in progress on `nuon2usb-wip` branch. Controller communicates but button data read is not yet functional.

## Architecture

- **Core 1**: Generates continuous clock on GPIO3, sends commands and reads responses on GPIO2 (bidirectional). Must run as a dedicated tight loop — the controller requires continuous clocking to stay alive.
- **Core 0**: USB HID device output, serial console, player management. Reads controller state from Core 1 via shared volatile variables.
- **PIO**: `polyface_read` on PIO0 required for reading responses. Software `gpio_get()` cannot reliably capture controller output — only PIO captures it correctly. Software bit-bang is used for clock generation and command sending.

## Key Findings

### Protocol
- Polyface V11 spec defines the protocol. Commands are 32-bit words sent MSB first with even parity at bit 0.
- Wire format: `start(1) + 32 data bits = 33 bits`. No extra framing bit.
- Command fields: `type0` (READ/WRITE) at bit 25, address at bits 24:17, size at bits 15:9, control/data at bits 7:1.
- Controller samples data on clock edges. Data set during one phase, sampled on the opposite phase.
- Bus has pull-down (idle LOW). Controller holds data LOW when connected.

### Clock
- Spec says 1MHz `PP_CLK`. Software `busy_wait_us_32(1)` gives ~500kHz which works.
- Controller needs **continuous** clocking — stops responding if clock gaps are too long.
- NOP-based delays (~8MHz) are too fast for the controller ASIC.

### Enumeration
Commands verified correct by polyface packet sniffer (separate KB2040 man-in-the-middle tool):
- **RESET** (0xB1): WRITE, S=0x00, C=0x00
- **ALIVE** (0x80): READ, S=0x04, C=0x40
- **PROBE** (0x94): READ, S=0x04, C=0x00
- **MAGIC** (0x90): READ, S=0x04, C=0x00
- **BRAND** (0xB4): WRITE, S=0x00, C=`<id>`
- **SWITCH** (0x30): READ, S=0x02, C=0x00 — device checks `dataS==0x02`
- **CHANNEL** (0x34): WRITE, S=0x01, C=`<channel>`
- **ANALOG** (0x35): READ, S=0x01, C=0x00

Possibly missing steps:
- **FOCUS** (0xB0): May be needed before PROBE/MAGIC/BRAND
- **STATE** (0x99) write with ENABLE (bit 7) + ROOT (bit 6) = 0xC0: Spec says "ENABLE must be set after configuration, before using the device"

### What Works
- Sniffer confirms commands are correctly formatted and controller responds
- PIO `polyface_read` captures response data (software GPIO cannot)
- Enumeration sequence (ALIVE/MAGIC/PROBE/BRAND) completes
- Controller reports as connected

### Open Issues
1. **PIO read alignment**: The PIO SM runs continuously and captures bus traffic. Distinguishing the actual command response from stale/echo data is unreliable. The device side doesn't have this problem because it never sends while reading.
2. **Button data static**: SWITCH reads return a constant value regardless of button presses. Either we're reading echo/stale data, or the controller isn't in the right state to report buttons (missing ENABLE/FOCUS).
3. **Data format**: Response values (e.g., `0x80061E66`) don't clearly map to expected Nuon button format. May need bit realignment based on whether the response includes a control bit.

## Hardware Setup

- Pico W with GPIO2 (data) and GPIO3 (clock) wired directly to Nuon controller port
- Same daughter board / wiring as `bt2nuon` device mode
- KB2040 connected via UART for serial console (send 'B' to enter bootloader)
- Flash via `picotool load <uf2> -f && picotool reboot`

## Reference

- Polyface V11 spec: `/Users/robert/git/NUON_RESEARCH/PolyfaceV11.pdf`
- Device-side implementation: `src/native/device/nuon/nuon_device.c`
- Packet sniffer: Separate app on KB2040, man-in-the-middle on polyface bus
