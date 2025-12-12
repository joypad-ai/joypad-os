// profiles.h - SNES2USB App Profiles
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Profile definitions for SNES2USB adapter.

#ifndef SNES2USB_PROFILES_H
#define SNES2USB_PROFILES_H

#include "core/services/profiles/profile.h"

// ============================================================================
// BUTTON COMBOS
// ============================================================================

// S1 + S2 = A1 (Home/Guide button)
static const button_combo_entry_t snes2usb_combos[] = {
    MAP_COMBO(JP_BUTTON_S1 | JP_BUTTON_S2, JP_BUTTON_A1),
};

// ============================================================================
// DEFAULT PROFILE
// ============================================================================

static const profile_t snes2usb_profiles[] = {
    {
        .name = "default",
        .description = "Standard mapping with Select+Start=Home",
        .button_map = NULL,
        .button_map_count = 0,
        .combo_map = snes2usb_combos,
        .combo_map_count = sizeof(snes2usb_combos) / sizeof(snes2usb_combos[0]),
        PROFILE_TRIGGERS_DEFAULT,
        PROFILE_ANALOG_DEFAULT,
        .adaptive_triggers = false,
    },
};

// ============================================================================
// PROFILE SET
// ============================================================================

static const profile_set_t snes2usb_profile_set = {
    .profiles = snes2usb_profiles,
    .profile_count = sizeof(snes2usb_profiles) / sizeof(snes2usb_profiles[0]),
    .default_index = 0,
};

#endif // SNES2USB_PROFILES_H
