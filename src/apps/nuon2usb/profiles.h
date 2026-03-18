// profiles.h - NUON2USB App Profiles
//
// Profile definitions for Nuon to USB adapter.

#ifndef NUON2USB_PROFILES_H
#define NUON2USB_PROFILES_H

#include "core/services/profiles/profile.h"

// ============================================================================
// PROFILE 1: DEFAULT (Standard Nuon layout)
// ============================================================================
// Nuon A -> B1 (Cross/A)
// Nuon B -> B3 (Square/X)
// Nuon C-Down -> B2 (Circle/B)
// Nuon C-Left -> B4 (Triangle/Y)
// Nuon C-Up -> L2
// Nuon C-Right -> R2
// Nuon L -> L1
// Nuon R -> R1
// Nuon Start -> Start
// Nuon Nuon/Z -> Select

// No remapping needed - use core defaults


// ============================================================================
// PROFILE 2: CLASSIC LAYOUT
// ============================================================================
// Swaps some face buttons for classic gaming compatibility

static const button_map_entry_t nuon2usb_classic_map[] = {
    MAP_BUTTON(JP_BUTTON_B1, JP_BUTTON_B2),  // A -> B (Cross -> Circle)
    MAP_BUTTON(JP_BUTTON_B2, JP_BUTTON_B1),  // B -> A (Circle -> Cross)
};

// ============================================================================
// PROFILE DEFINITIONS
// ============================================================================

static const profile_t nuon2usb_profiles[] = {
    // Profile 0: Default (Standard Nuon layout)
    {
        .name = "default",
        .description = "Standard: A/B/C-pad as-is",
        .button_map = NULL,
        .button_map_count = 0,
        .combo_map = NULL,
        .combo_map_count = 0,
        PROFILE_TRIGGERS_DEFAULT,
        PROFILE_ANALOG_DEFAULT,
        .adaptive_triggers = false,
    },
    // Profile 1: Classic layout (A/B swapped)
    {
        .name = "classic",
        .description = "Classic: A/B swapped",
        .button_map = nuon2usb_classic_map,
        .button_map_count = sizeof(nuon2usb_classic_map) / sizeof(nuon2usb_classic_map[0]),
        .combo_map = NULL,
        .combo_map_count = 0,
        PROFILE_TRIGGERS_DEFAULT,
        PROFILE_ANALOG_DEFAULT,
        .adaptive_triggers = false,
    },
};

// ============================================================================
// PROFILE SET
// ============================================================================

static const profile_set_t nuon2usb_profile_set = {
    .profiles = nuon2usb_profiles,
    .profile_count = sizeof(nuon2usb_profiles) / sizeof(nuon2usb_profiles[0]),
    .default_index = 0,
};

#endif // NUON2USB_PROFILES_H
