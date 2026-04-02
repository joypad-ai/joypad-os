# Data Flow

This page explains how controller input travels from a physical device all the way to a console or USB output, step by step.

## The Pipeline

```
  Controller          Input Driver          Router            Output Driver         Console
  ──────────          ────────────          ──────            ─────────────         ───────

  [Button Press] --> [Normalize to    ] --> [Transform   ] --> [Read from    ] --> [Send to
                      input_event_t]        Profile           router_outputs]      console]
                                            Merge
                                            Store

  [Rumble Motor] <-- [Send SET_REPORT] <-- [Player Mgr ] <-- [get_rumble() ] <-- [Console
                                            feedback]                              command]
```

Data flows forward through the input path and backward through the feedback path. Both paths are lock-free on the critical read/write operations.

## Step 1: Input Normalization

Every input driver -- whether USB HID, Bluetooth, SNES GPIO, or WiFi -- converts raw controller data into a common `input_event_t` structure:

```c
typedef struct {
    uint8_t dev_addr;             // Device address (unique per controller)
    int8_t instance;              // Instance within device (e.g., hub port)
    input_device_type_t type;     // GAMEPAD, MOUSE, or KEYBOARD
    input_transport_t transport;  // USB, BT_CLASSIC, BLE, WIFI, NATIVE

    uint32_t buttons;             // JP_BUTTON_* bitmap (W3C Gamepad order)

    uint8_t analog[7];            // LX, LY, RX, RY, L2, R2, RZ
                                  // All normalized: 0-255, 128 = center

    int8_t delta_x, delta_y;     // Mouse deltas
} input_event_t;
```

Key normalization rules:

- **Buttons** use W3C Gamepad API order (JP_BUTTON_B1 through JP_BUTTON_A1). See the [Glossary](glossary.md) for the full list.
- **Analog axes** are normalized to 0-255 with 128 as center. All drivers must follow HID convention: 0 = up/left, 255 = down/right.
- **Y-axis inversion**: Nintendo-style controllers (N64, GameCube) use inverted Y (0 = down). Input drivers invert this during normalization so the rest of the system sees standard HID convention.

## Step 2: Router Submit

The input driver calls `router_submit_input(&event)` to hand off the event. The router processes it through a five-stage inline pipeline -- there is no queue or thread boundary here:

```
router_submit_input(&event)
    |
    v
1. TRANSFORM    Apply transform flags (mouse-to-analog, spinner
                accumulation, instance merging)
    |
    v
2. PROFILE      Apply the active button remapping profile
                (defined per-app in profiles.h)
    |
    v
3. MERGE        Combine with other inputs targeting the same
                output slot (based on routing mode)
    |
    v
4. STORE        Write to router_outputs[target][slot]
                (atomic single-writer from Core 0)
    |
    v
5. TAP          Call push callbacks for outputs that need
                immediate notification
```

This entire pipeline runs inline -- the function returns only after the event is stored and any callbacks have fired.

## Step 3: Routing Modes

The router mode determines how input devices map to output slots:

**SIMPLE** -- One controller per slot. Device N maps to player slot N. This is the default for console adapters. When controller 1 connects, it gets slot 1; controller 2 gets slot 2; and so on up to the output's player limit.

**MERGE** -- All controllers merge into a single slot. All button presses and analog values are blended together (OR for buttons, priority-based for analog). Used by `bt2usb` and copilot/accessibility setups.

**BROADCAST** -- Every input is sent to every output slot. Used for specialized multi-output configurations.

When multiple inputs target the same slot (in MERGE or CONFIGURABLE modes), a merge mode determines how they combine:

- **MERGE_BLEND** -- OR all button states together. If either controller presses A, the output sees A pressed.
- **MERGE_PRIORITY** -- The highest-priority input wins on conflicts.
- **MERGE_ALL** -- The most recently active input wins.

## Step 4: Profile Application

Before an event reaches the output, the router applies the active button remapping profile. Profiles are defined per-app in `profiles.h` and support:

- **1:1 remapping** -- Map B1 to B2, swap sticks, etc.
- **Button combos** -- Multiple inputs produce a single output.
- **Analog targets** -- A button press produces a specific analog axis value.
- **Analog sensitivity** -- Scale stick ranges.

Users cycle profiles by holding SELECT + D-pad Up (next) or Down (previous) for 2 seconds. The NeoPixel LED flashes to confirm the change. The selected profile persists to flash so it survives power cycles.

## Step 5: Output Read

Output drivers call `router_get_output(target, slot)` to read the latest state for their player slot. This returns a direct pointer to internal router storage -- zero-copy, no mutex, no allocation.

Console outputs running on Core 1 call this in their tight PIO loop. USB device outputs running on Core 0 call it in their periodic task. Either way, the read is lock-free because the router uses atomic single-writer semantics (only Core 0 writes; reads are always consistent).

## Step 6: Console Protocol

The output driver translates the common format into the console's native protocol:

- **GameCube**: Packs buttons and analog into a joybus response frame, sent via PIO at 130MHz.
- **PCEngine**: Maps buttons into multiplexed select/clock scan lines via PIO.
- **Dreamcast**: Builds a maple bus packet with button/analog/trigger data.
- **USB Device**: Fills a HID report descriptor matching the selected output mode (SInput, XInput, PS4, Switch, etc.).

## Feedback Path

Feedback flows backward through the system. When a console sends a rumble command:

1. The output driver receives the command (e.g., GameCube sends a rumble bit in its poll request).
2. `OutputInterface.get_rumble()` returns the motor state.
3. `players_task()` reads feedback for each player slot and routes it to the correct input device by `dev_addr` and `instance`.
4. The input driver sends the feedback to the physical controller (USB SET_REPORT, Bluetooth HID output report, N64 rumble pak command, etc.).

This is how a DualSense vibrates when a GameCube game triggers rumble -- the feedback crosses the entire stack.

## Dual-Core Execution

On RP2040, timing-critical work is split across two CPU cores:

**Core 0** runs the main loop:
```
while (1) {
    services: leds_task(), players_task(), storage_task()
    inputs:   for each input_interface -> task()
    outputs:  for each output_interface -> task()
    app:      app_task()
}
```

**Core 1** runs the output's `core1_task()` -- a tight PIO loop for console protocols. Only one output can claim Core 1. Core 1 reads from the router (lock-free) and interacts with PIO state machines.

Input polling on Core 0 runs *before* output tasks on Core 0, so outputs always see the freshest data from the current loop iteration.

## Latency Design

The architecture minimizes input-to-output latency through several deliberate choices:

1. **Input before output** -- Outputs always read data from the current loop iteration.
2. **Zero-copy router** -- `router_get_output()` returns a pointer, not a copy.
3. **No mutexes on critical path** -- Single-writer atomic stores; lock-free reads.
4. **No queuing** -- `router_submit_input()` processes the event inline. No event queue between layers.
5. **RAM placement** -- Timing-critical code is placed in SRAM with `__not_in_flash_func` to avoid flash cache misses.
6. **Core isolation** -- Console PIO loops on Core 1 are never interrupted by USB or Bluetooth processing.

## Next Steps

- [Architecture](architecture.md) -- The four-layer model
- [Glossary](glossary.md) -- Key terms and definitions
- [Joypad Core](../core/index.md) -- Router, profiles, players, and other services
