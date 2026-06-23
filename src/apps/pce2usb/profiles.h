// profiles.h - PCE2USB App Profiles
// SPDX-License-Identifier: Apache-2.0
//
// Profile definitions for PCE2USB adapter.

#ifndef PCE2USB_PROFILES_H
#define PCE2USB_PROFILES_H

#include "core/services/profiles/profile.h"

// ============================================================================
// DEFAULT PROFILE
// ============================================================================

static const profile_t pce2usb_profiles[] = {
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

static const profile_set_t pce2usb_profile_set = {
    .profiles = pce2usb_profiles,
    .profile_count = sizeof(pce2usb_profiles) / sizeof(pce2usb_profiles[0]),
    .default_index = 0,
};

#endif // PCE2USB_PROFILES_H
