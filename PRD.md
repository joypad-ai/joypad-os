# PRD: Configurable Analog Stick Deadzone

## Context
This is the joypad-os firmware (RP2040/ESP32, C, CMake). The codebase lives at `~/joypad/worktrees/deadzone/`. This is a universal controller adapter firmware — USB/BT controllers connect as input, and output goes to retro/modern consoles.

**Problem:** Many controllers develop stick drift over time. There's no user-configurable deadzone for USB/BT input controllers. A deadzone exists for GPIO-wired pad inputs (`src/pad/pad_input.c`) but NOT for the main input pipeline that USB host and Bluetooth controllers use.

**Goal:** Add configurable per-stick deadzone at the router level so it applies to ALL input sources (USB, BT, native). Make it configurable via the CDC config protocol (web config at config.joypad.ai) and persist it in flash.

## Architecture Overview

### Input pipeline flow:
1. Input drivers (USB host, BT, native, pad) generate `input_event_t` structs
2. Events flow into the **router** (`src/core/router/router.c`) 
3. Router applies **transformations** (Phase 5) — mouse-to-analog, merge instances, etc.
4. Router routes transformed events to **output interfaces**

### Key files:
- `src/core/router/router.c` / `router.h` — Routing + transformations
- `src/core/input_event.h` — Input event struct (has `uint8_t analog[16]` array, centered at 128)
- `src/core/services/storage/flash.h` — Flash settings struct (256 bytes, has `custom_profile_t` with `reserved[22]`)
- `src/core/services/storage/flash.c` — Flash read/write
- `src/usb/usbd/cdc/cdc_commands.c` — CDC command handlers (web config protocol)
- `src/pad/pad_input.c` — Has existing `apply_deadzone()` function for reference

### Analog axis indices (from input_event.h):
```c
#define ANALOG_LX 0  // Left stick X
#define ANALOG_LY 1  // Left stick Y  
#define ANALOG_RX 2  // Right stick X
#define ANALOG_RY 3  // Right stick Y
```
Values are `uint8_t`, centered at 128 (0=full left/up, 255=full right/down).

### Existing deadzone reference (`src/pad/pad_input.c`):
```c
static uint8_t apply_deadzone(uint8_t value, uint8_t deadzone) {
    int centered = (int)value - 128;
    if (centered > -deadzone && centered < deadzone) {
        return 128;  // In deadzone, return center
    }
    return value;
}
```

## Implementation Plan

### 1. Add deadzone fields to `custom_profile_t` (flash.h)
Use 2 of the 22 reserved bytes:
```c
typedef struct {
    // ... existing fields ...
    uint8_t left_stick_sens;
    uint8_t right_stick_sens;
    uint8_t flags;
    uint8_t socd_mode;
    uint8_t left_deadzone;     // NEW: 0-127, deadzone radius for left stick (default 0 = off)
    uint8_t right_deadzone;    // NEW: 0-127, deadzone radius for right stick (default 0 = off)
    uint8_t reserved[20];      // Was 22, now 20
} custom_profile_t;
```

**Important:** The default value for these fields in existing flash data will be 0xFF (unprogrammed flash). The code MUST treat 0xFF as "no deadzone" (equivalent to 0) to maintain backward compatibility. Add a helper:
```c
static inline uint8_t get_effective_deadzone(uint8_t stored_value) {
    return (stored_value == 0xFF) ? 0 : stored_value;
}
```

### 2. Add deadzone transform to router (router.c)
Add a new transformation that runs in `apply_transformations()`:

```c
// In router.h, add new transform flag:
TRANSFORM_DEADZONE = 0x08,

// In apply_transformations():
// Always apply deadzone if profile has non-zero values
// (This should run regardless of transform_flags since it's profile-based)
```

Actually, better approach: Apply deadzone **unconditionally** in the router output path, reading from the active profile's deadzone settings. Don't use transform_flags — deadzone is a profile setting, not an app-level transform.

Add to router.c, in the function that finalizes output events (look for where `cdc_commands_send_input_event` or output state is written):

```c
static uint8_t apply_deadzone(uint8_t value, uint8_t deadzone) {
    if (deadzone == 0) return value;
    int centered = (int)value - 128;
    if (centered > -(int)deadzone && centered < (int)deadzone) {
        return 128;
    }
    return value;
}
```

Apply it to ANALOG_LX, ANALOG_LY (left deadzone) and ANALOG_RX, ANALOG_RY (right deadzone) before the event is sent to output interfaces.

### 3. Add CDC commands (cdc_commands.c)
Add two new commands to the command table:

**DEADZONE.GET** — Returns current deadzone settings
```json
{"left": 10, "right": 10}
```

**DEADZONE.SET** — Sets deadzone values
Input: `{"left": 15, "right": 15}`
Response: `{"ok": true, "left": 15, "right": 15}`

These should read/write the active profile's deadzone fields and save to flash.

Add entries to the `commands[]` table:
```c
{"DEADZONE.GET", cmd_deadzone_get},
{"DEADZONE.SET", cmd_deadzone_set},
```

### 4. Default deadzone
- For NEW profiles (not loaded from flash): set default deadzone to 0 (off)
- Flash backward compatibility: 0xFF treated as 0 (off)
- Range: 0-127 (0 = off, 10 = ~8% deadzone, 20 = ~16%, 127 = max)

## Testing
- Build must compile: `cd ~/joypad/worktrees/deadzone && mkdir -p build && cd build && cmake .. -DAPP=usb2usb -DBOARD=kb2040 && make -j$(nproc)`
- If cmake/make setup is complex, just verify the C code compiles without syntax errors by checking includes and types

## DO NOT
- Change the flash_t struct size (must remain 256 bytes)
- Break backward compatibility with existing flash data (0xFF = no deadzone)
- Modify any output interface code
- Change the input_event_t struct
- Remove or modify the existing pad_input.c deadzone (that's for GPIO-wired pads, separate concern)
- Add any new dependencies

## Commit
After implementation:
- `cd ~/joypad/worktrees/deadzone && git add -A && git commit -m "Add configurable analog stick deadzone via CDC config"`
- Push: `gh auth switch --user joypad-bot && git push origin feature/deadzone`
- Create PR: title "Feature: Configurable analog stick deadzone (fixes #117)"
