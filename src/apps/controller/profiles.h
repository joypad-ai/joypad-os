// profiles.h - Controller App Profiles
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Profile definitions for GPIO-based controllers.

#ifndef CONTROLLER_PROFILES_H
#define CONTROLLER_PROFILES_H

#include "core/services/profiles/profile.h"

// ============================================================================
// BUTTON COMBOS
// ============================================================================

// S1 + S2 = A1 (Home/Guide button)
static const button_combo_entry_t controller_combos[] = {
    MAP_COMBO(JP_BUTTON_S1 | JP_BUTTON_S2, JP_BUTTON_A1),
};

// ============================================================================
// DEFAULT PROFILE
// ============================================================================

static const profile_t controller_profiles[] = {
    {
        .name = "default",
        .description = "Standard mapping with S1+S2=Home",
        .button_map = NULL,
        .button_map_count = 0,
        .combo_map = controller_combos,
        .combo_map_count = sizeof(controller_combos) / sizeof(controller_combos[0]),
        PROFILE_TRIGGERS_DEFAULT,
        PROFILE_ANALOG_DEFAULT,
        .adaptive_triggers = false,
    },
};

// ============================================================================
// PROFILE SET
// ============================================================================

static const profile_set_t controller_profile_set = {
    .profiles = controller_profiles,
    .profile_count = sizeof(controller_profiles) / sizeof(controller_profiles[0]),
    .default_index = 0,
};

#endif // CONTROLLER_PROFILES_H
