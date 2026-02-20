// profiles.h - NES2USB App Profiles
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 James Ian Murchison
//
// Profile definitions for NES2USB adapter.

#ifndef NES2USB_PROFILES_H
#define NES2USB_PROFILES_H

#include "core/services/profiles/profile.h"

// ============================================================================
// DEFAULT PROFILE
// ============================================================================

static const profile_t nes2usb_profiles[] = {
    {
        .name = "default",
        .description = "Standard passthrough",
        .button_map = NULL,
        .button_map_count = 0,
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

static const profile_set_t nes2usb_profile_set = {
    .profiles = nes2usb_profiles,
    .profile_count = sizeof(nes2usb_profiles) / sizeof(nes2usb_profiles[0]),
    .default_index = 0,
};

#endif // NES2USB_PROFILES_H
