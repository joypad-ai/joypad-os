// runtime_profile.h - Runtime profile mapping service
//
// Provides a combo-triggered interactive mode that builds a non-persistent
// profile_t by mapping controller buttons to fixed output buttons one by one.
//
// Usage:
//   1. Define output buttons, input mask, hold time and the app profile in
//      runtime_profile_config_t, then call runtime_profile_init() from app_init().
//   2. The output tap callback calls runtime_profile_get_active() to check
//      whether a runtime mapping is built; if NULL, falls back to
//      profile_get_active() normally.
//   3. runtime_profile_is_active() returns true while mapping; the task
//      loop uses this to suppress the profile-switch combo.
//
// Trigger:  SELECT (S1) held alone for config.hold_ms, then press any input_mask button.
//           That first button is assigned to entry 1. D-pad is not in input_mask so
//           SELECT+D-pad (profile switch) is unaffected and resets the hold timer.
// Map:      press any button in config.input_mask → maps to current entry.
//           First button pressed wins (lowest-bit isolation).
// Cancel:   SELECT + START (S1 + S2) while in mapping mode.

#ifndef RUNTIME_PROFILE_H
#define RUNTIME_PROFILE_H

#include <stdint.h>
#include <stdbool.h>
#include "core/services/profiles/profile.h"

// Maximum number of mappable entries.
#define RUNTIME_PROFILE_ENTRY_MAX 16

typedef struct {
    // Fixed output JP_BUTTON_* values — one per entry, in order.
    const uint32_t* output_buttons;

    // Number of buttons to map (must be <= RUNTIME_PROFILE_ENTRY_MAX).
    uint8_t output_button_count;

    // Bitmask of JP_BUTTON_* values eligible as inputs.
    // Typically excludes S1/S2 (used for trigger/cancel) and D-pad.
    uint32_t input_mask;

    // How long SELECT must be held alone before the first input_mask
    // button press activates mapping mode (ms).
    uint32_t hold_ms;

    // Optional display names for each output button (NULL = use JP_BUTTON_* name).
    // Array must have at least output_button_count entries if provided.
    const char* const* output_button_names;

    // App-defined profile to build into at runtime.
    // The service sets button_map on init and updates button_map_count
    // as entries are filled. Define alongside other profiles in profiles.h.
    profile_t* profile;

} runtime_profile_output_config_t;

typedef struct {
    // Per-output runtime profile configs, indexed by output_target_t.
    // NULL means no runtime profile for that output.
    const runtime_profile_output_config_t* output_configs[MAX_OUTPUT_TARGETS];
} runtime_profile_config_t;

// Initialize the service with app-defined config. Call from app_init().
// Passing NULL resets to idle and disables the service.
void runtime_profile_init(const runtime_profile_config_t* config);

// Clear the runtime mapping and return to the base profile.
// Call when the user switches profiles normally so the new base profile
// takes effect immediately.
void runtime_profile_clear(void);

// Zero autofire_period_ms on every entry in runtime_map, leaving all other
// state and button assignments intact.
void runtime_autofire_clear(void);

// Drive the state machine with the latest raw button state.
// Call from the output device task loop alongside profile_check_switch_combo().
// l2/r2: raw analog values (0-255) for threshold and digital normalization.
void runtime_profile_check_combo(uint32_t input_buttons, uint8_t l2, uint8_t r2);

// Returns true while the user is actively mapping entries.
bool runtime_profile_is_active(void);

// Returns the built runtime profile if one exists, NULL otherwise.
// Output tap callbacks use this to check for a runtime override:
//   const profile_t* p = runtime_profile_get_active(OUTPUT_TARGET_GPIO);
//   if (!p) p = profile_get_active(OUTPUT_TARGET_GPIO);
const profile_t* runtime_profile_get_active(output_target_t output);

// ============================================================================
// CALLBACKS
// ============================================================================

// Set player count callback (for feedback)
void runtime_profile_set_player_count_callback(uint8_t (*callback)(void));

#endif // RUNTIME_PROFILE_H
