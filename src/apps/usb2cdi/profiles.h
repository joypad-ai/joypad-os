// profiles.h - USB2CDI Profile Definitions
//
// CD-i has 2 action buttons + pointing device (D-pad/stick as cursor).

#ifndef USB2CDI_PROFILES_H
#define USB2CDI_PROFILES_H

#include "core/services/profiles/profile.h"
#include "native/device/cdi/cdi_buttons.h"

// ============================================================================
// PROFILE: Default
// ============================================================================
// B1 (Cross/A) → CD-i Button 1
// B2 (Circle/B) → CD-i Button 2
// Left stick / D-pad → Cursor movement

static const button_map_entry_t cdi_default_map[] = {
    MAP_BUTTON(JP_BUTTON_B1, CDI_BUTTON_1),
    MAP_BUTTON(JP_BUTTON_B2, CDI_BUTTON_2),
    MAP_BUTTON(JP_BUTTON_B3, CDI_BUTTON_1),  // Square/X also Button 1
    MAP_BUTTON(JP_BUTTON_B4, CDI_BUTTON_2),  // Triangle/Y also Button 2
    MAP_BUTTON(JP_BUTTON_L1, CDI_BUTTON_1),  // L1 also Button 1
    MAP_BUTTON(JP_BUTTON_R1, CDI_BUTTON_2),  // R1 also Button 2
    MAP_DISABLED(JP_BUTTON_L2),
    MAP_DISABLED(JP_BUTTON_R2),
    MAP_DISABLED(JP_BUTTON_S1),
    MAP_DISABLED(JP_BUTTON_S2),
    MAP_DISABLED(JP_BUTTON_L3),
    MAP_DISABLED(JP_BUTTON_R3),
};

static const profile_t cdi_profile_default = {
    .name = "default",
    .description = "Standard CD-i mapping",
    .button_map = cdi_default_map,
    .button_map_count = sizeof(cdi_default_map) / sizeof(cdi_default_map[0]),
    .l2_behavior = TRIGGER_PASSTHROUGH,
    .r2_behavior = TRIGGER_PASSTHROUGH,
    .l2_threshold = 128,
    .r2_threshold = 128,
    .l2_analog_value = 0,
    .r2_analog_value = 0,
    .left_stick_sensitivity = 1.0f,
    .right_stick_sensitivity = 1.0f,
    .left_stick_modifiers = NULL,
    .left_stick_modifier_count = 0,
    .right_stick_modifiers = NULL,
    .right_stick_modifier_count = 0,
    .adaptive_triggers = false,
    .socd_mode = SOCD_PASSTHROUGH,
};

// ============================================================================
// PROFILE SET
// ============================================================================

static const profile_t cdi_profiles[] = {
    cdi_profile_default,
};

static const profile_set_t cdi_profile_set = {
    .profiles = cdi_profiles,
    .profile_count = sizeof(cdi_profiles) / sizeof(cdi_profiles[0]),
    .default_index = 0,
};

#endif // USB2CDI_PROFILES_H
