# Router

The router is the central data plane of Joypad Core. It receives `input_event_t` events from input drivers, applies transforms and profiles, and stores the result for output drivers to read. All routing is inline on Core 0 -- there are no queues, threads, or copies on the hot path (when possible).

Source: `src/core/router/router.c` and `router.h`

## Routing Modes

The router supports four modes, configured at init time via `router_config_t.mode`:

### SIMPLE (1:1)

Each controller maps to its own player slot. Controller N goes to output slot N. Used by most console adapters (usb2pce, usb2dc, snes2usb).

```
Controller 0 --> Slot 0
Controller 1 --> Slot 1
Controller 2 --> Slot 2
```

### MERGE (N:1)

All controllers merge into a single output slot (slot 0). Used by usb2gc (all USB inputs feed one GC port) and bt2usb (all BT controllers feed one USB gamepad).

```
Controller 0 --\
Controller 1 ---+--> Slot 0
Controller 2 --/
```

How inputs are combined depends on the merge mode (see below).

### BROADCAST (1:N)

Every input goes to every active output target. Used by multi-output apps where the same controller drives multiple consoles simultaneously.

```
Controller 0 --> Slot 0 (GameCube)
Controller 0 --> Slot 0 (USB Device)
Controller 0 --> Slot 0 (BLE)
```

### CONFIGURABLE (N:M)

User-defined routing table. Each route entry maps an input source to an output target with optional filters (device address, instance, target player). Routes are matched at runtime via `router_find_routes()`.

## Merge Modes

When multiple inputs target the same slot (MERGE mode or multiple controllers in SIMPLE mode), the merge mode determines how they combine:

### BLEND

All inputs are OR'd together. For each output slot, the router tracks every contributing device in a `blend_device_state_t` array and re-blends on every update:

- **Buttons**: OR together (any controller pressing A = A pressed)
- **Sticks**: Use the value furthest from center (128)
- **Triggers**: Use the maximum value
- **Mouse deltas**: Accumulate, then clear per-device to prevent re-adding
- **Motion/Pressure/Touch**: Use the first device that reports data

This is the default for usb2gc -- two players can both contribute to a single GameCube controller.

### PRIORITY

Higher-priority input wins. Used by composite adapters (e.g., USB input has priority over SNES fallback). Lower-priority sources only update the output when no higher-priority source is active.

### ALL

Most recent input overwrites the entire output state. Simple last-writer-wins semantics.

## Transform Pipeline

Before storing to an output slot, the router can apply transformations to the input event. Transformations are enabled via `router_config_t.transform_flags` (a bitfield):

| Flag | Description |
|------|-------------|
| `TRANSFORM_MOUSE_TO_ANALOG` | Accumulate mouse deltas into analog stick positions. Configurable target axes (`mouse_target_x/y`) and drain rate. |
| `TRANSFORM_MERGE_INSTANCES` | Merge multi-instance devices (e.g., Joy-Con Grip left+right into single controller). |
| `TRANSFORM_SPINNER` | Accumulate X-axis deltas for spinner input (Nuon, etc.). |

Transformations modify the event in-place. When no transforms are enabled, the event passes through zero-copy.

### Mouse-to-Analog

Converts mouse delta_x/delta_y into analog stick positions. Per-player accumulators track position:

- `mouse_target_x` / `mouse_target_y`: Which analog axis to write (e.g., `ANALOG_LX`, `ANALOG_RX`, or `MOUSE_AXIS_DISABLED`)
- `mouse_drain_rate`: How fast the accumulated position drains back to center each frame. 0 = hold position (no drain).
- Values are clamped to [-127, 127] then mapped to [1, 255] (centered at 128)

## What Happens When `router_submit_input()` Is Called

Step-by-step walkthrough of a single input event:

1. **Null/route check** -- If the event is NULL or no routes are configured (`route_count == 0`), return immediately.

2. **CDC streaming** -- If `CONFIG_USB` is defined, the raw input is streamed to CDC for the web configuration tool.

3. **Find output target** -- The first active route in the routing table determines the primary output target.

4. **Dispatch by mode**:

   **SIMPLE mode:**
   - a. Look up the player index for this `(dev_addr, instance)` pair via `find_player_index()`.
   - b. If not found and the controller has buttons pressed or analog stick deflected beyond threshold (~40%), call `add_player()` to assign a new slot. The device name is looked up from USB HID registry, BT device table, or transport type.
   - c. If `transform_flags` is set, copy the event and call `apply_transformations()`. Otherwise use the event pointer directly (zero-copy).
   - d. Unless this output has an exclusive tap, write the final event to `router_outputs[output][player_index]` and set `updated = true`.
   - e. If a tap callback is registered for this output, call it with the final event.

   **MERGE mode:**
   - a. Register the player (same as SIMPLE step a/b) for LED and rumble tracking.
   - b. Apply transformations if enabled.
   - c. Dispatch to the configured merge sub-mode:
     - MERGE_ALL: Overwrite slot 0 with this event.
     - MERGE_BLEND: Update this device's entry in `blend_devices[]`, then re-blend all active devices into slot 0 (OR buttons, furthest-from-center sticks, max triggers).
     - MERGE_PRIORITY: Only update slot 0 if this source has higher priority than the current one.
   - d. Set `updated = true` and call tap callback if registered.

   **BROADCAST mode:**
   - Call `router_simple_mode()` for each active output target.

   **CONFIGURABLE mode:**
   - Find all matching routes via `router_find_routes()` (checks input source, device address, instance filters).
   - For each match, write the event to the specified output target and player slot.

## Output Retrieval

Output drivers on Core 1 read state via:

- `router_get_output(output, player_id)` -- Returns a pointer to `router_outputs[output][player_id].current_state`. Zero-copy, lock-free. Returns NULL if the slot has not been updated.
- `router_has_updates(output)` -- Fast scan: are any player slots updated for this output?
- `router_get_player_count(output)` -- How many player slots are occupied?

## Output Taps

For push-based outputs (UART, BLE) that do not poll `router_get_output()`, register a tap callback:

```c
// Standard tap: event stored to router_outputs AND callback called
router_set_tap(OUTPUT_TARGET_UART, my_uart_callback);

// Exclusive tap: callback called, router_outputs NOT written (avoids copy)
router_set_tap_exclusive(OUTPUT_TARGET_BT, my_ble_callback);
```

## Routing Table

The routing table holds up to `MAX_ROUTES` (32) entries. Each entry (`route_entry_t`) specifies:

| Field | Description |
|-------|-------------|
| `input` | Input source enum (USB_HOST, BLE_CENTRAL, NATIVE_SNES, etc.) |
| `output` | Output target enum (GAMECUBE, PCENGINE, USB_DEVICE, etc.) |
| `priority` | Route priority (0 = highest) |
| `input_dev_addr` | Filter by device address (0 = wildcard) |
| `input_instance` | Filter by instance (-1 = wildcard) |
| `output_player_id` | Target player slot (0xFF = auto-assign) |

Apps configure routes at init time:

```c
router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_GAMECUBE, 0);
```

## Device Disconnect

When a device disconnects, call `router_device_disconnected(dev_addr, instance)` before removing the player from the player manager. This clears the device's output state and removes it from blend tracking.

## Configuration Example

From `usb2gc/app.c`:

```c
router_config_t router_cfg = {
    .mode = ROUTING_MODE_MERGE,
    .merge_mode = MERGE_BLEND,
    .transform_flags = TRANSFORM_MOUSE_TO_ANALOG,
    .mouse_drain_rate = 8,
    .mouse_target_x = ANALOG_LX,
    .mouse_target_y = ANALOG_LY,
};
router_cfg.max_players_per_output[OUTPUT_TARGET_GAMECUBE] = 4;
router_init(&router_cfg);
router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_GAMECUBE, 0);
```

## See Also

- [Buttons](buttons.md) -- JP_BUTTON_* constants the router passes through
- [Profiles](profiles.md) -- Button remapping applied after routing
- [Players](players.md) -- Player slot assignment the router delegates to
- [Data Flow](../overview/data-flow.md) -- Full input-to-output pipeline
