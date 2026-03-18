# NUON2USB Implementation Task

## Goal
Implement `nuon2usb` — read a real Nuon controller (Polyface peripheral) and output as USB HID gamepad. This is the reverse of `bt2nuon`/`usb2nuon` which pretend to be a controller talking to a Nuon console.

## Key Insight
The existing `nuon_device.c` (device-side) code is a goldmine — it shows exactly how the console talks to a controller. We need to BE the console and issue those same commands, then read the controller's responses.

## Hardware Setup
- **Pi Pico** (regular, not Pico W) with GPIO2 (data) and GPIO3 (clock) wired to a real Nuon controller
- The controller is holding L and R buttons down physically (for testing)
- **KB2040** on UART pins for serial debug logs — send 'B' character to reboot Pico to bootloader
- Flash via: `picotool load <uf2> -f && picotool reboot`
- Build target: `make nuon2usb_kb2040` (already in Makefile, uses `boards/build_ada_kb2040.sh`)

## Architecture

### What Needs to Change vs Prior Failed Attempt

The prior agent tried to rewrite the PIO programs. DON'T DO THAT. The existing PIO programs work fine. What we need is:

1. **We drive the clock** — GPIO3 must be an OUTPUT generating continuous ~500kHz clock
2. **We send commands** via `polyface_send` PIO (same program, we push command words)
3. **We read responses** via `polyface_read` PIO (same program, it captures on clock edges)
4. **The tricky part**: synchronization between send and read — the device side doesn't have this problem because it never sends while reading

### Core Architecture

```
Core 1 (tight loop, time-critical):
  - Generate continuous clock on GPIO3 (~500kHz, busy_wait_us_32(1) between toggles)
  - Send Polyface commands (RESET, ALIVE, PROBE, MAGIC, BRAND, STATE, CHANNEL, SWITCH, ANALOG)
  - Read responses from PIO read SM
  - Update shared volatile state variables

Core 0 (main loop):
  - USB HID device output (via usbd interface)
  - Read shared volatile state from Core 1
  - Submit input events to router
  - Serial console (check for 'B' = bootloader)
```

### Clock Generation
- Console drives `PP_CLK` (GPIO3) — this is us now
- `busy_wait_us_32(1)` between toggles gives ~500kHz — known to work
- Controller needs CONTINUOUS clocking — cannot have gaps
- NOP-based delays (~8MHz) are TOO FAST for the controller ASIC

### Command Format (from the device-side code analysis)
Commands are 33-bit packets on the wire: `start(1) + 32 data bits`
The 32 data bits contain:
- bit 25: `type0` — 1=READ, 0=WRITE
- bits 24:17: `dataA` (address/command)
- bits 15:9: `dataS` (size field)
- bits 7:1: `dataC` (control/data field)
- bit 0: even parity

### Enumeration Sequence (from device-side state machine)
Study `nuon_device.c` core1_task() — the device handles these commands from the console:

1. **RESET** (0xB1, S=0x00, C=0x00) — WRITE, resets device state
2. **ALIVE** (0x80, S=0x04, C=0x40) — READ, device responds with ID or 0b01 if first time
3. **MAGIC** (0x90) — READ, device responds with 0x4A554445 ("JUDE")  
4. **PROBE** (0x94, S=0x04, C=0x00) — READ, device responds with DEFCFG/VERSION/TYPE/MFG/TAGGED/BRANDED/ID
5. **BRAND** (0xB4, S=0x00, C=<id>) — WRITE, assigns ID to device
6. **STATE** (0x99, S=0x01) — READ or WRITE. Write ENABLE+ROOT (0xC0) to activate device
7. **CHANNEL** (0x34, S=0x01, C=<channel>) — WRITE, selects analog channel
8. **SWITCH** (0x30, S=0x02, C=0x00) — READ, returns button data (2 bytes + CRC)
9. **ANALOG** (0x35, S=0x01, C=0x00) — READ, returns analog value for current channel

### Response Format
Responses are CRC-protected. For 1-byte values: `[data_byte][crc_hi][crc_lo][0x00]`
For 2-byte values: `[data_hi][data_lo][crc_hi][crc_lo]`
Use the existing `crc_data_packet()` and `crc_calc()` functions for verification.

### Button Data (from nuon_device.h)
```c
#define NUON_BUTTON_UP      0x0200
#define NUON_BUTTON_DOWN    0x0800
#define NUON_BUTTON_LEFT    0x0400
#define NUON_BUTTON_RIGHT   0x0100
#define NUON_BUTTON_A       0x4000
#define NUON_BUTTON_B       0x0008
#define NUON_BUTTON_L       0x0020
#define NUON_BUTTON_R       0x0010
#define NUON_BUTTON_C_UP    0x0002
#define NUON_BUTTON_C_DOWN  0x8000
#define NUON_BUTTON_C_LEFT  0x0004
#define NUON_BUTTON_C_RIGHT 0x0001
#define NUON_BUTTON_START   0x2000
#define NUON_BUTTON_NUON    0x1000
```

## Files to Create/Modify

### New: `src/native/host/nuon/nuon_host.h`
InputInterface declaration, config defines (GPIO pins, etc.)

### New: `src/native/host/nuon/nuon_host.c`
The main driver:
- `nuon_host_init()` — set up PIOs, init clock pin as output
- `nuon_host_core1_task()` — tight loop: clock generation + command/response protocol
- `nuon_host_task()` — called from Core 0, reads volatile state and submits to router
- Button mapping from Nuon format to JP format (reverse of `map_nuon_buttons()`)
- `nuon_input_interface` — InputInterface struct

### Modify: `src/apps/nuon2usb/app.h`
Update to proper app manifest — use Pi Pico board, require USB device output, require native NUON input.

### New: `src/apps/nuon2usb/app.c`  
App entry point following gc2usb pattern:
- Declare nuon_input_interface + usbd_output_interface
- Configure router: INPUT_SOURCE_NATIVE_NUON → OUTPUT_TARGET_USB_DEVICE
- Handle 'B' bootloader command on UART

### New: `src/apps/nuon2usb/app_config.h`
LED configuration.

### New: `src/apps/nuon2usb/profiles.h`
Default profile (probably passthrough).

### Modify: `src/core/router/router.h`
Add `INPUT_SOURCE_NATIVE_NUON` to enum.

### Modify: `src/CMakeLists.txt`
Update nuon2usb target to include new host driver sources.

## Critical Implementation Details

### PIO Read Synchronization (THE HARD PROBLEM)
The readme identifies this as the main issue. The PIO read SM runs continuously and captures ALL bus traffic. When we send a command, the read SM might capture our OWN command bits as "response data."

**Solution approach**: 
1. Flush the PIO read FIFO before sending a command
2. After sending command, wait for the turnaround delay (the device waits 30 clock edges before responding — see `polyface_respond()`)
3. Then read the response from PIO read FIFO
4. Alternatively: disable the read SM while sending, re-enable after send completes + turnaround

### Clock + Data Timing
Since WE drive the clock, we control when bits are sampled:
- Set data bit on one clock phase, controller samples on opposite phase
- The `polyface_send` PIO waits on GPIO3 edges — but now WE control GPIO3
- May need a different approach: software bit-bang for commands while generating clock, PIO for reads

**Recommended approach based on readme findings**:
- Software bit-bang for clock generation AND command sending (busy_wait loop)
- PIO `polyface_read` for capturing responses (software gpio_get can't reliably capture)
- This avoids the PIO send/read conflict entirely

### STATE Command
The readme notes this might be missing. After enumeration (ALIVE/MAGIC/PROBE/BRAND), the device side handles:
```c
if (dataA == 0x99 && dataS == 0x01) { // STATE
    // WRITE: state = ((state) << 8) | (dataC & 0xff);
    // READ: responds with state value
}
```
The BIOS writes STATE with ENABLE (bit 7) + ROOT (bit 6) = 0xC0. We should do the same.

### Full Polling Loop (what the console does)
Based on the device-side command handler, the console polling sequence is:
1. CHANNEL (0x34) with channel=0x02 (X1), then ANALOG (0x35) read
2. CHANNEL with channel=0x03 (Y1), then ANALOG read
3. CHANNEL with channel=0x04 (X2), then ANALOG read  
4. CHANNEL with channel=0x05 (Y2), then ANALOG read
5. SWITCH (0x30) read — gets buttons
6. Repeat

## Build & Test

```bash
cd ~/joypad/worktrees/nuon2usb
make nuon2usb_kb2040
# UF2 will be at src/build/joypad_nuon2usb.uf2
# Flash: picotool load src/build/joypad_nuon2usb.uf2 -f && picotool reboot
```

Watch serial output on KB2040's CDC port for debug logs.

The test controller has L and R buttons held down physically — if SWITCH reads show L+R bits set, we know it's working!

Expected L+R bits: `NUON_BUTTON_L (0x0020) | NUON_BUTTON_R (0x0010)` = 0x0030 in the button word.

## Reference Files
- Device-side protocol handler: `src/native/device/nuon/nuon_device.c` (STUDY THIS CAREFULLY)
- Device header with constants: `src/native/device/nuon/nuon_device.h`
- Button definitions: `src/native/device/nuon/nuon_buttons.h`
- PIO read program: `src/native/device/nuon/polyface_read.pio`
- PIO send program: `src/native/device/nuon/polyface_send.pio`
- Serial device (another device-side impl): `src/native/device/nuon/nuon_serial_device.c`
- GC host pattern (what to follow): `src/native/host/gc/gc_host.c` + `gc_host.h`
- GC2USB app pattern: `src/apps/gc2usb/app.c` + `app.h`
- USB device output interface: `src/usb/usbd/usbd.h`
- Router header: `src/core/router/router.h`
- Input event: `src/core/input_event.h`
- Build config: `src/CMakeLists.txt` (search for `joypad_nuon2usb`)
- Makefile: root `Makefile` (target: `nuon2usb_kb2040`)

## IMPORTANT: Git Identity
When committing, use:
```
git -c user.name="joypad-bot" -c user.email="bot@joypad.ai" commit ...
```
