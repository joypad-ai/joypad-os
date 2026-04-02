# Profile System

The profile system provides button remapping, analog tuning, and trigger configuration. Apps define profiles as static arrays; users cycle between them at runtime. Each player can have an independent active profile.

Source: `src/core/services/profiles/profile.h` and `profile.c`

## How Profiles Work

A profile is a `profile_t` struct containing:

- **Button map** -- Sparse array of `button_map_entry_t` entries. Only buttons that differ from the default 1:1 passthrough need entries. If a button is not in the map, it passes through unchanged.
- **Combo map** -- `button_combo_entry_t` entries for multi-button combinations. Combos are checked before individual mappings.
- **Trigger configuration** -- Per-trigger behavior (passthrough, digital only, full press, light press, instant, disabled).
- **Analog settings** -- Stick sensitivity scaling and button-triggered sensitivity modifiers.
- **SOCD handling** -- Simultaneous Opposite Cardinal Direction cleaning (passthrough, neutral, up-priority, last-win).

## Button Mapping Types

### Simple Remap

Map one input button to a different output button:

```c
MAP_BUTTON(JP_BUTTON_B1, JP_BUTTON_B2)  // A outputs as B
```

### Multi-Button Output

Map one input to multiple output buttons:

```c
MAP_BUTTON_MULTI(JP_BUTTON_R2, JP_BUTTON_L2, JP_BUTTON_R2)  // RT outputs as LT+RT
```

### Button to Analog

Map a button press to an analog axis value:

```c
MAP_BUTTON_ANALOG(JP_BUTTON_L1, JP_BUTTON_L2, ANALOG_TARGET_L2_FULL, 0)  // LB outputs L2 digital + L2 analog at 255
MAP_ANALOG_ONLY(JP_BUTTON_DU, ANALOG_TARGET_RY_MIN)  // D-Up outputs right stick up
```

### Button Disabled

Suppress a button entirely:

```c
MAP_DISABLED(JP_BUTTON_A1)  // Guide button does nothing
```

### Button Combos

Map multiple simultaneous buttons to an output:

```c
MAP_COMBO(JP_BUTTON_L1 | JP_BUTTON_R1, JP_BUTTON_A1)  // L1+R1 = Guide
MAP_COMBO_EXCLUSIVE(JP_BUTTON_B1 | JP_BUTTON_B2, JP_BUTTON_B3)  // A+B = X, but only if EXACTLY A+B pressed
```

### Stick Modifiers

Reduce stick sensitivity when a button is held:

```c
STICK_MODIFIER(JP_BUTTON_L3, 0.5f)  // Hold L3 = 50% stick sensitivity
```

## Trigger Behavior

Each trigger (L2/R2) can be configured independently:

| Mode | Description |
|------|-------------|
| `TRIGGER_PASSTHROUGH` | Analog value passed through, digital activates at threshold |
| `TRIGGER_DIGITAL_ONLY` | Digital button only, no analog output |
| `TRIGGER_FULL_PRESS` | Digital + analog forced to 255 |
| `TRIGGER_LIGHT_PRESS` | Analog capped at custom value, no digital |
| `TRIGGER_INSTANT` | Digital triggers at threshold = 1 (hair trigger) |
| `TRIGGER_DISABLED` | No output |

## SOCD Cleaning

For fighting game controllers (Hitbox, arcade sticks) where opposite directions can be pressed simultaneously:

| Mode | Up+Down | Left+Right |
|------|---------|------------|
| `SOCD_PASSTHROUGH` | Both output | Both output |
| `SOCD_NEUTRAL` | Cancel (neutral) | Cancel (neutral) |
| `SOCD_UP_PRIORITY` | Up wins | Cancel (neutral) |
| `SOCD_LAST_WIN` | Last input wins | Last input wins |

## Profile Cycling UX

Users switch profiles at runtime:

1. Hold **SELECT + D-pad Up** for 2 seconds -- cycle to the next profile
2. Hold **SELECT + D-pad Down** for 2 seconds -- cycle to the previous profile
3. The NeoPixel LED flashes to confirm (number of OFF blinks = profile index + 1)
4. Controller rumble provides haptic confirmation (if the controller supports it)
5. While the combo is held, SELECT + D-pad buttons are suppressed from the output

Per-player profile switching is supported: each player can independently cycle profiles using the same combo on their own controller.

Additionally, SELECT + D-pad Left/Right can switch USB output modes (when an output mode callback is registered).

## Profile Persistence

The active profile index is saved to flash and survives power cycles:

- `profile_save_to_flash()` writes the index via the [storage system](storage.md)
- `profile_load_from_flash()` restores it at boot
- Write debouncing (5-second delay) prevents excessive flash wear from rapid cycling

## Applying Profiles

Output drivers call `profile_apply()` to transform input state through the active profile:

```c
profile_output_t output;
const profile_t* profile = profile_get_active(OUTPUT_TARGET_GAMECUBE);
profile_apply(profile, event->buttons,
              event->analog[ANALOG_LX], event->analog[ANALOG_LY],
              event->analog[ANALOG_RX], event->analog[ANALOG_RY],
              event->analog[ANALOG_L2], event->analog[ANALOG_R2],
              event->analog[ANALOG_RZ], &output);
```

The `profile_output_t` contains remapped buttons, analog values with override flags, and passthrough motion/pressure data.

For simple button-only mapping, `profile_apply_button_map()` returns remapped buttons without analog processing.

## Defining Profiles in Apps

Apps define profiles in a `profiles.h` file:

```c
static const button_map_entry_t my_button_map[] = {
    MAP_BUTTON(JP_BUTTON_B1, JP_BUTTON_B2),
    MAP_BUTTON(JP_BUTTON_B2, JP_BUTTON_B1),
};

static const profile_t my_profiles[] = {
    PROFILE_DEFAULT,  // Index 0: passthrough
    {
        .name = "swapped",
        .description = "A/B buttons swapped",
        .button_map = my_button_map,
        .button_map_count = 2,
        PROFILE_TRIGGERS_DEFAULT,
        PROFILE_ANALOG_DEFAULT,
    },
};

static const profile_set_t my_profile_set = {
    .profiles = my_profiles,
    .profile_count = 2,
    .default_index = 0,
};
```

The profile set is registered at init via `profile_init()`.

## Custom Profiles (Web Config)

In addition to compiled profiles, users can create up to 4 custom profiles via the web configuration tool. Custom profiles are stored in flash as `custom_profile_t` structs with per-button remap values, stick sensitivity, and SOCD mode. See [Storage](storage.md) for the flash format.

## See Also

- [Buttons](buttons.md) -- JP_BUTTON_* constants used in mappings
- [Router](router.md) -- How input flows to profiles
- [Storage](storage.md) -- How profile selections persist
- [LEDs](leds.md) -- Visual feedback on profile changes
