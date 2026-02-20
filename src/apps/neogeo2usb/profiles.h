// profiles.h - NEOGEO2USB App Profiles
// SPDX-License-Identifier: Apache-2.0
//
// Profile definitions for NEOGEO2USB adapter.

#ifndef NEOGEO2USB_PROFILES_H
#define NEOGEO2USB_PROFILES_H

#include "core/services/profiles/profile.h"

// ============================================================================
// BUTTON COMBOS
// ============================================================================

// S1 + S2 (alone) = A1 (Home/Guide button)
// Uses exclusive combo: only fires when S1+S2 are the ONLY buttons pressed
// If any other buttons are held (e.g., S1+S2+L1+R2), combo doesn't fire
// and all buttons pass through normally
static const button_combo_entry_t neogeo2usb_combos[] = {
    MAP_COMBO_EXCLUSIVE(JP_BUTTON_S1 | JP_BUTTON_S2, JP_BUTTON_A1),
};

// ============================================================================
// DEFAULT PROFILE
// ============================================================================

static const profile_t neogeo2usb_profiles[] = {
    {
        .name = "default",
        .description = "Standard mapping with Select+Start=Home",
        .button_map = NULL,
        .button_map_count = 0,
        .combo_map = neogeo2usb_combos,
        .combo_map_count = sizeof(neogeo2usb_combos) / sizeof(neogeo2usb_combos[0]),
        PROFILE_TRIGGERS_DEFAULT,
        PROFILE_ANALOG_DEFAULT,
        .adaptive_triggers = false,
    },
};

// ============================================================================
// PROFILE SET
// ============================================================================

static const profile_set_t neogeo2usb_profile_set = {
    .profiles = neogeo2usb_profiles,
    .profile_count = sizeof(neogeo2usb_profiles) / sizeof(neogeo2usb_profiles[0]),
    .default_index = 0,
};

#endif // NEOGEO2USB_PROFILES_H
