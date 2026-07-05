// profiles.h - USB2NEOGEO Tournament Mode Profile Definitions
//
// Single profile (default 1L6B layout).
// Runtime remap is consecutive mode only — input_mask is 0 so the
// SELECT+button combos that trigger alt/autofire modes can never fire.

#ifndef USB2NEOGEO_TE_PROFILES_H
#define USB2NEOGEO_TE_PROFILES_H

#include "core/services/profiles/profile.h"
#include "core/services/profiles/runtime_profile.h"
#include "native/device/gpio/neogeo_buttons.h"

// ============================================================================
// PROFILE: Default - Standard Six Button Layout
// ============================================================================
//
//  ( )
//   |      (P1) (P2) (P3) ( )
//          (K1) (K2) (K3) ( )
//

static const button_map_entry_t neogeo_te_default_map[] = {
    MAP_BUTTON(JP_BUTTON_B3, NEOGEO_BUTTON_B1),    // Square   → P1/A
    MAP_BUTTON(JP_BUTTON_B4, NEOGEO_BUTTON_B2),    // Triangle → P2/B
    MAP_BUTTON(JP_BUTTON_R1, NEOGEO_BUTTON_B3),    // R1       → P3/C
    MAP_BUTTON(JP_BUTTON_B1, NEOGEO_BUTTON_B4),    // Cross    → K1/D
    MAP_BUTTON(JP_BUTTON_B2, NEOGEO_BUTTON_B5),    // Circle   → K2/SELECT
    MAP_BUTTON(JP_BUTTON_R2, NEOGEO_BUTTON_B6),    // R2       → K3

    MAP_DISABLED(JP_BUTTON_L1),
    MAP_DISABLED(JP_BUTTON_L2),
};

static const profile_t neogeo_te_profile_default = {
    .name = "default",
    .description = "Standard six button layout",
    .button_map = neogeo_te_default_map,
    .button_map_count = sizeof(neogeo_te_default_map) / sizeof(neogeo_te_default_map[0]),
    .l2_behavior = TRIGGER_PASSTHROUGH,
    .r2_behavior = TRIGGER_PASSTHROUGH,
    .l2_threshold = 128,
    .r2_threshold = 128,
    .l2_analog_value = 0,
    .r2_analog_value = 0,
    .left_stick_sensitivity = 1.0f,
    .right_stick_sensitivity = 1.0f,
    .adaptive_triggers = false,
    .socd_mode = SOCD_UP_PRIORITY,
};

// ============================================================================
// PROFILE SET (single profile — no cycling)
// ============================================================================

static const profile_t neogeo_te_profiles[] = {
    neogeo_te_profile_default,
};

static const profile_set_t neogeo_te_profile_set = {
    .profiles = neogeo_te_profiles,
    .profile_count = sizeof(neogeo_te_profiles) / sizeof(neogeo_te_profiles[0]),
    .default_index = 0,
};

// ============================================================================
// RUNTIME PROFILE (consecutive mode only)
// ============================================================================
// input_mask = 0 prevents SELECT+button combos from triggering alt/autofire
// modes. SELECT alone (consecutive mode) still works normally.

static const uint32_t neogeo_te_runtime_outputs[] = {
    NEOGEO_BUTTON_B1,
    NEOGEO_BUTTON_B2,
    NEOGEO_BUTTON_B3,
    NEOGEO_BUTTON_B4,
    NEOGEO_BUTTON_B5,
    NEOGEO_BUTTON_B6,
};

static const char* const neogeo_te_runtime_output_names[] = {
    "A (P1)",
    "B (P2)",
    "C (P3)",
    "D (K1)",
    "Select (K2)",
    "K3",
};

static profile_t neogeo_te_runtime_profile = {
    .name = "runtime",
    .description = "Runtime profile mapping",
    .button_map = NULL,
    .button_map_count = 0,
    .l2_behavior = TRIGGER_PASSTHROUGH,
    .r2_behavior = TRIGGER_PASSTHROUGH,
    .l2_threshold = 128,
    .r2_threshold = 128,
    .l2_analog_value = 0,
    .r2_analog_value = 0,
    .left_stick_sensitivity = 1.0f,
    .right_stick_sensitivity = 1.0f,
    .adaptive_triggers = false,
    .socd_mode = SOCD_UP_PRIORITY,
};

static const runtime_profile_output_config_t neogeo_te_runtime_output_config = {
    .output_buttons      = neogeo_te_runtime_outputs,
    .output_button_count = sizeof(neogeo_te_runtime_outputs) / sizeof(neogeo_te_runtime_outputs[0]),
    .input_mask          = 0,   // Disables SELECT+button combos (alt/autofire modes)
    .hold_ms             = 2000,
    .output_button_names = neogeo_te_runtime_output_names,
    .profile             = &neogeo_te_runtime_profile,
};

#endif // USB2NEOGEO_TE_PROFILES_H
