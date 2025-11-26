// profiles.h - USB2XB1 Profile Definitions
//
// Button mapping profiles for USB to Xbox One adapter.
// Uses console-specific button aliases for readability.
//
// Xbox One button layout:
//   A (B1) - Green button (bottom)
//   B (B2) - Red button (right)
//   X (B3) - Blue button (left)
//   Y (B4) - Yellow button (top)
//   LB/RB (L1/R1) - Bumpers
//   LT/RT (L2/R2) - Triggers
//   L3/R3 - Stick clicks
//   View/Menu (S1/S2) - System buttons
//   Guide (A1) - Xbox button

#ifndef USB2XB1_PROFILES_H
#define USB2XB1_PROFILES_H

#include "core/services/profile/profile.h"
#include "native/device/xboxone/xboxone_buttons.h"

// ============================================================================
// PROFILE: Default - Standard Xbox One Layout
// ============================================================================
// Straight passthrough - Xbox One layout matches USB controllers

static const profile_t xb1_profile_default = {
    .name = "default",
    .description = "Standard Xbox One layout (passthrough)",
    .button_map = NULL,  // No remapping needed - straight passthrough
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
};

// ============================================================================
// PROFILE: PlayStation Swap - A/B and X/Y swapped
// ============================================================================
// For PlayStation users who prefer their layout

static const button_map_entry_t xb1_playstation_map[] = {
    // Swap A/B (Cross/Circle)
    MAP_BUTTON(USBR_BUTTON_B1, XB1_BUTTON_B),      // Cross → B
    MAP_BUTTON(USBR_BUTTON_B2, XB1_BUTTON_A),      // Circle → A

    // Swap X/Y (Square/Triangle)
    MAP_BUTTON(USBR_BUTTON_B3, XB1_BUTTON_Y),      // Square → Y
    MAP_BUTTON(USBR_BUTTON_B4, XB1_BUTTON_X),      // Triangle → X
};

static const profile_t xb1_profile_playstation = {
    .name = "playstation",
    .description = "PlayStation layout (A/B X/Y swapped)",
    .button_map = xb1_playstation_map,
    .button_map_count = sizeof(xb1_playstation_map) / sizeof(xb1_playstation_map[0]),
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

static const profile_t xb1_profiles[] = {
    xb1_profile_default,
    xb1_profile_playstation,
};

static const profile_set_t xb1_profile_set = {
    .profiles = xb1_profiles,
    .profile_count = sizeof(xb1_profiles) / sizeof(xb1_profiles[0]),
    .default_index = 0,
};

#endif // USB2XB1_PROFILES_H
