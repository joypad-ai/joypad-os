// profiles.h - USB2Loopy Profile Definitions
//
// Button mapping profiles for USB to Casio Loopy adapter.
// Uses console-specific button aliases for readability.
//
// Loopy button layout:
//   A (B1) - Primary action (bottom)
//   B (B2) - Secondary action (right)
//   C (B3) - Tertiary (top)
//   D (B4) - Quaternary (left)
//   L/R (L1/R1) - Shoulder triggers
//   Start (S2)
//   D-pad

#ifndef USB2LOOPY_PROFILES_H
#define USB2LOOPY_PROFILES_H

#include "core/services/profiles/profile.h"
#include "native/device/loopy/loopy_buttons.h"

// ============================================================================
// PROFILE: Default - Standard Loopy Layout
// ============================================================================
// Maps modern controllers to Loopy naturally

static const button_map_entry_t loopy_default_map[] = {
    // Face buttons - diamond layout
    MAP_BUTTON(JP_BUTTON_B1, LOOPY_BUTTON_A),    // Cross → A (bottom)
    MAP_BUTTON(JP_BUTTON_B2, LOOPY_BUTTON_B),    // Circle → B (right)
    MAP_BUTTON(JP_BUTTON_B3, LOOPY_BUTTON_C),    // Square → C (top)
    MAP_BUTTON(JP_BUTTON_B4, LOOPY_BUTTON_D),    // Triangle → D (left)

    // Shoulders
    MAP_BUTTON(JP_BUTTON_L1, LOOPY_BUTTON_L),
    MAP_BUTTON(JP_BUTTON_R1, LOOPY_BUTTON_R),

    // Triggers also map to L/R
    MAP_BUTTON(JP_BUTTON_L2, LOOPY_BUTTON_L),
    MAP_BUTTON(JP_BUTTON_R2, LOOPY_BUTTON_R),

    // System
    MAP_DISABLED(JP_BUTTON_S1),                   // Select → nothing
    MAP_BUTTON(JP_BUTTON_S2, LOOPY_BUTTON_START), // Start → Start
};

static const profile_t loopy_profile_default = {
    .name = "default",
    .description = "Standard Loopy layout",
    .button_map = loopy_default_map,
    .button_map_count = sizeof(loopy_default_map) / sizeof(loopy_default_map[0]),
    .l2_behavior = TRIGGER_PASSTHROUGH,
    .r2_behavior = TRIGGER_PASSTHROUGH,
    .l2_threshold = 128,
    .r2_threshold = 128,
    .l2_analog_value = 0,
    .r2_analog_value = 0,
    .left_stick_sensitivity = 1.0f,
    .right_stick_sensitivity = 1.0f,
    .adaptive_triggers = false,
};

// ============================================================================
// PROFILE SET
// ============================================================================

static const profile_t loopy_profiles[] = {
    loopy_profile_default,
};

static const profile_set_t loopy_profile_set = {
    .profiles = loopy_profiles,
    .profile_count = sizeof(loopy_profiles) / sizeof(loopy_profiles[0]),
    .default_index = 0,
};

#endif // USB2LOOPY_PROFILES_H
