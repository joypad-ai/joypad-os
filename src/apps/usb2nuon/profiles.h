// profiles.h - USB2Nuon Profile Definitions
//
// Button mapping profiles for USB to Nuon adapter.
// Uses console-specific button aliases for readability.
//
// Nuon button layout:
//   A (B1) - Main action button
//   B (B3) - Secondary action
//   C-Up/Down/Left/Right - C-pad (L2, B2, B4, R2)
//   L/R (L1/R1) - Shoulder buttons
//   Start (S2)
//   Nuon/Z (S1) - System button
//   D-pad, Analog sticks

#ifndef USB2NUON_PROFILES_H
#define USB2NUON_PROFILES_H

#include "core/services/profile/profile.h"
#include "native/device/nuon/nuon_buttons.h"

// ============================================================================
// PROFILE: Default - Standard Nuon Layout
// ============================================================================
// Maps modern controllers to Nuon naturally (PlayStation style)

static const button_map_entry_t nuon_default_map[] = {
    // Face buttons - PlayStation style to Nuon
    MAP_BUTTON(USBR_BUTTON_B1, NUON_BTN_A),        // Cross → A
    MAP_BUTTON(USBR_BUTTON_B2, NUON_BTN_C_DOWN),   // Circle → C-Down
    MAP_BUTTON(USBR_BUTTON_B3, NUON_BTN_B),        // Square → B
    MAP_BUTTON(USBR_BUTTON_B4, NUON_BTN_C_LEFT),   // Triangle → C-Left

    // Shoulders
    MAP_BUTTON(USBR_BUTTON_L1, NUON_BTN_L),
    MAP_BUTTON(USBR_BUTTON_R1, NUON_BTN_R),

    // Triggers to C-pad
    MAP_BUTTON(USBR_BUTTON_L2, NUON_BTN_C_UP),
    MAP_BUTTON(USBR_BUTTON_R2, NUON_BTN_C_RIGHT),

    // System
    MAP_BUTTON(USBR_BUTTON_S1, NUON_BTN_NUON),     // Select → Nuon/Z
    MAP_BUTTON(USBR_BUTTON_S2, NUON_BTN_START),    // Start → Start
};

static const profile_t nuon_profile_default = {
    .name = "default",
    .description = "Standard PlayStation-style layout",
    .button_map = nuon_default_map,
    .button_map_count = sizeof(nuon_default_map) / sizeof(nuon_default_map[0]),
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
// PROFILE: N64 Style - For N64-like controllers
// ============================================================================
// C-buttons on face buttons like N64

static const button_map_entry_t nuon_n64_map[] = {
    // A/B on lower buttons
    MAP_BUTTON(USBR_BUTTON_B1, NUON_BTN_A),        // Cross → A
    MAP_BUTTON(USBR_BUTTON_B3, NUON_BTN_B),        // Square → B

    // C-buttons on upper face buttons
    MAP_BUTTON(USBR_BUTTON_B2, NUON_BTN_C_DOWN),   // Circle → C-Down
    MAP_BUTTON(USBR_BUTTON_B4, NUON_BTN_C_LEFT),   // Triangle → C-Left

    // Shoulders as C-Up/C-Right (like Z button usage)
    MAP_BUTTON(USBR_BUTTON_L1, NUON_BTN_C_UP),
    MAP_BUTTON(USBR_BUTTON_R1, NUON_BTN_C_RIGHT),

    // Triggers as L/R
    MAP_BUTTON(USBR_BUTTON_L2, NUON_BTN_L),
    MAP_BUTTON(USBR_BUTTON_R2, NUON_BTN_R),

    // System
    MAP_BUTTON(USBR_BUTTON_S1, NUON_BTN_NUON),
    MAP_BUTTON(USBR_BUTTON_S2, NUON_BTN_START),
};

static const profile_t nuon_profile_n64 = {
    .name = "n64",
    .description = "N64-style C-button layout",
    .button_map = nuon_n64_map,
    .button_map_count = sizeof(nuon_n64_map) / sizeof(nuon_n64_map[0]),
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

static const profile_t nuon_profiles[] = {
    nuon_profile_default,
    nuon_profile_n64,
};

static const profile_set_t nuon_profile_set = {
    .profiles = nuon_profiles,
    .profile_count = sizeof(nuon_profiles) / sizeof(nuon_profiles[0]),
    .default_index = 0,
};

#endif // USB2NUON_PROFILES_H
