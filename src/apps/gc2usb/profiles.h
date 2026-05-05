// profiles.h - GC2USB App Profiles
//
// Profile definitions for GameCube to USB adapter.

#ifndef GC2USB_PROFILES_H
#define GC2USB_PROFILES_H

#include "core/services/profiles/profile.h"

// ============================================================================
// PROFILE 1: DEFAULT (Standard layout)
// ============================================================================
// A=B1, B=B2, X=B3, Y=B4
// L=L1, R=R1, Z=R2
// Main stick=left analog, C-stick=right analog
// L/R triggers=analog L2/R2

// No remapping needed - use core defaults


// ============================================================================
// PROFILE 2: XBOX LAYOUT
// ============================================================================
// Swap A/B (A=B2, B=B1)

static const button_map_entry_t gc2usb_xbox_map[] = {
    MAP_BUTTON(JP_BUTTON_B1, JP_BUTTON_B2),  // GC A -> USB B
    MAP_BUTTON(JP_BUTTON_B2, JP_BUTTON_B1),  // GC B -> USB A
};

// ============================================================================
// PROFILE 3: NINTENDO LAYOUT
// ============================================================================
// Standard Nintendo: A=right (B1), B=down (B2), X=top (B4), Y=left (B3)
// Swaps X and Y positions to match Nintendo convention

static const button_map_entry_t gc2usb_nintendo_map[] = {
    MAP_BUTTON(JP_BUTTON_B3, JP_BUTTON_B4),  // GC X -> USB Y position
    MAP_BUTTON(JP_BUTTON_B4, JP_BUTTON_B3),  // GC Y -> USB X position
};

// ============================================================================
// PROFILE DEFINITIONS
// ============================================================================

static const profile_t gc2usb_profiles[] = {
    // Profile 0: Default (Standard GC layout)
    {
        .name = "default",
        .description = "Standard: A/B/X/Y as-is",
        .button_map = NULL,
        .button_map_count = 0,
        .combo_map = NULL,
        .combo_map_count = 0,
        // Triggers: passthrough only — GC L/R digital fires from the
        // physical click at the bottom of the trigger pull (driver's
        // report->l/r), independent of analog travel. Threshold=0 skips
        // the analog→digital override entirely.
        .l2_behavior = TRIGGER_PASSTHROUGH,
        .r2_behavior = TRIGGER_PASSTHROUGH,
        .l2_threshold = 0,
        .r2_threshold = 0,
        .l2_analog_value = 0,
        .r2_analog_value = 0,
        PROFILE_ANALOG_DEFAULT,
        .adaptive_triggers = false,
    },
    // Profile 1: Xbox layout (A/B swapped)
    {
        .name = "xbox",
        .description = "Xbox: A/B swapped",
        .button_map = gc2usb_xbox_map,
        .button_map_count = sizeof(gc2usb_xbox_map) / sizeof(gc2usb_xbox_map[0]),
        .combo_map = NULL,
        .combo_map_count = 0,
        // Triggers: passthrough only — GC L/R digital fires from the
        // physical click at the bottom of the trigger pull (driver's
        // report->l/r), independent of analog travel. Threshold=0 skips
        // the analog→digital override entirely.
        .l2_behavior = TRIGGER_PASSTHROUGH,
        .r2_behavior = TRIGGER_PASSTHROUGH,
        .l2_threshold = 0,
        .r2_threshold = 0,
        .l2_analog_value = 0,
        .r2_analog_value = 0,
        PROFILE_ANALOG_DEFAULT,
        .adaptive_triggers = false,
    },
    // Profile 2: Nintendo layout (X/Y swapped)
    {
        .name = "nintendo",
        .description = "Nintendo: X/Y swapped",
        .button_map = gc2usb_nintendo_map,
        .button_map_count = sizeof(gc2usb_nintendo_map) / sizeof(gc2usb_nintendo_map[0]),
        .combo_map = NULL,
        .combo_map_count = 0,
        // Triggers: passthrough only — GC L/R digital fires from the
        // physical click at the bottom of the trigger pull (driver's
        // report->l/r), independent of analog travel. Threshold=0 skips
        // the analog→digital override entirely.
        .l2_behavior = TRIGGER_PASSTHROUGH,
        .r2_behavior = TRIGGER_PASSTHROUGH,
        .l2_threshold = 0,
        .r2_threshold = 0,
        .l2_analog_value = 0,
        .r2_analog_value = 0,
        PROFILE_ANALOG_DEFAULT,
        .adaptive_triggers = false,
    },
};

// ============================================================================
// PROFILE SET
// ============================================================================

static const profile_set_t gc2usb_profile_set = {
    .profiles = gc2usb_profiles,
    .profile_count = sizeof(gc2usb_profiles) / sizeof(gc2usb_profiles[0]),
    .default_index = 0,
};

#endif // GC2USB_PROFILES_H
