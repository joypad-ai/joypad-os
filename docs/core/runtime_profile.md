# Runtime Profile

The runtime profile service lets users remap buttons and assign auto-fire frequencies at runtime. Remappings are built on the fly using button combos on the controller itself and overlay on top of any active profile.

Source: `src/core/services/profiles/runtime_profile.c` and `runtime_profile.h`

## How It Works

The service watches all button events via `runtime_profile_check_combo()`, called from the app's input processing loop. It maintains an internal state machine with four states:

| State | Description |
|-------|-------------|
| `RUNTIME_IDLE` | Normal operation; listening for trigger combos |
| `RUNTIME_MAPPING` | Consecutive mapping mode (one input button per output) |
| `RUNTIME_MAPPING_ALT` | Press-count mapping mode (press N times → output N) |
| `RUNTIME_AUTOFIRE` | Auto-fire assignment mode (press N times → frequency) |

While the NeoPixel or controller LED is indicating (blinking), button input is ignored to avoid accidental triggers.

## Consecutive Mapping (RUNTIME_MAPPING)

Maps each input button to a fixed output, one at a time.

**To enter:**
1. Hold **SELECT** alone (no mask buttons, no D-pad) for `hold_ms`
2. Press the first mask button to assign to output slot 1

**While in mapping mode:**
- Press a mask button → assigns it to the next output
- Already-mapped buttons are silently rejected (duplicate input protection)
- Press **START** → cancel and clear the mapping

**To finish:** press mask buttons until all output are filled. Unmapped mask buttons are automatically disabled (they produce no output).

**Feedback:** 1 NeoPixel blink per intermediate entry confirmed; 2 blinks when mapping is complete.

## Press-Count Mapping (RUNTIME_MAPPING_ALT)

Maps input buttons to output by pressing: press a button N times to assign it to output N.

**To enter:** Hold **SELECT** + any 2 mask buttons simultaneously for `hold_ms`. After `hold_ms` the LED blinks twice and buttons stop registering — the device is in mapping mode. Release all buttons.

**While in mapping mode:**
- Press a mask button N times → assigns it to output N (1-indexed)
- 800ms silence after the last press commits the sequence (LED blinks once as confirmation)
- Pressing a different button commits the previous sequence and starts a new one
- Not pressing a button leaves it unmapped (it will not produce output)
- Press **SELECT** alone → save and exit (LED blinks twice)
- Press **START** alone → cancel and clear

> **Note:** Every time this mode is entered, the previous layout is fully erased and must be set a new.

**Multiple inputs to the same slot:** pressing different buttons the same number of times maps both to the same output. This allows, for example, assigning two input buttons to the same game action — one with auto-fire and one without:

> _Button 1 pressed once (→ output 1, no turbo) = charged shot_
> _Button 3 pressed once (→ output 1, with auto-fire) = rapid shot_

**Feedback:** 1 NeoPixel blink each time a press sequence is committed; 2 blinks on save.

## Auto-Fire (RUNTIME_AUTOFIRE)

Assigns a repeating auto-fire frequency to an existing mapped button.

**To enter:** Hold **SELECT** + exactly 1 mask button for `hold_ms`. After `hold_ms` the LED blinks twice and buttons stop registering — the device is in auto-fire mode. Release all buttons.

**While in auto-fire mode:**
- Press the target button N times → assigns auto-fire at the corresponding frequency:

| Press | Frequency |
|------|-----------|
| 1 | 30 Hz |
| 2 | 20 Hz |
| 3 | 15 Hz |
| 4 | 12 Hz |
| 5 | 10 Hz |
| 6 | 7.5 Hz |
| 7+ | Disabled (clears any existing auto-fire) |

- 800ms silence after the last press commits the frequency (LED blinks once as confirmation)
- Not pressing a button leaves its auto-fire setting unchanged
- Press **SELECT** alone → save and exit (LED blinks twice)
- Press **START** alone → discard all changes and exit

Auto-fire overlays the current mapping without replacing it. If no runtime mapping exists yet, it seeds from the active profile so frequencies are applied on top of the existing remapping.

**Feedback:** 1 NeoPixel blink each time a press sequence is committed; 2 blinks on save.

## Duplicate Input Protection

In `RUNTIME_MAPPING`, a `uint32_t runtime_mapped_mask` bitmask accumulates every input button that has already been assigned. When a button press is detected, a single bitwise AND rejects it instantly if already mapped:

```c
if (input_btn & runtime_mapped_mask) {
    // skip — already assigned to another output slot
}
```

The mask is set in `map_entry()` and cleared in `runtime_profile_init()`, `runtime_profile_clear()`, and at the start of each new mapping session.

## Clearing the Runtime Mapping

From `RUNTIME_IDLE`: hold **SELECT** for `hold_ms`, then press **START** (no mask buttons). The mapping is erased and the device returns to the active profile.

Programmatically: `runtime_profile_clear()` resets all state. `runtime_autofire_clear()` removes only auto-fire assignments without touching the button remapping.


## Integrating in an App

Apps configure the service by providing a `runtime_profile_config_t` at init:

```c
static profile_t runtime_prof = { .name = "runtime" };

static const runtime_profile_output_config_t rt_out = {
    .profile             = &runtime_prof,
    .input_mask          = JP_BUTTON_B1 | JP_BUTTON_B2 | JP_BUTTON_B3 | JP_BUTTON_B4,
    .output_buttons      = { JP_BUTTON_B1, JP_BUTTON_B2, JP_BUTTON_B3, JP_BUTTON_B4 },
    .output_button_names = NULL,
    .output_button_count = 4,
    .hold_ms             = 3000,
};

static const runtime_profile_config_t rt_cfg = {
    .output_configs = {
        [OUTPUT_TARGET_...] = & rt_cfg,
    },
};

// In app_init():
runtime_profile_init(&rt_cfg);

// In ..._device_init();
runtime_profile_set_player_count_callback(get_player_count);

// In the input processing loop:
runtime_profile_check_combo(event->buttons,
                            event->analog[ANALOG_L2],
                            event->analog[ANALOG_R2]);

// In output aplly
const profile_t* profile = runtime_profile_get_active(OUTPUT_TARGET_...);
if (!profile) profile = profile_get_active(OUTPUT_TARGET_...);

```

`runtime_profile_is_active()` returns true while the device is in any mapping or auto-fire mode, which apps can use to suppress unrelated combos.

## See Also

- [Profiles](profiles.md) -- Compiled profile system that runtime mapping overlays
- [Buttons](buttons.md) -- JP_BUTTON_* constants used as input and output masks