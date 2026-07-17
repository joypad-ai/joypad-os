// profiles.h - JAG2USB App Profiles
// SPDX-License-Identifier: Apache-2.0
//
// Profile definitions for JAG2USB adapter. Pro Controller mode is handled
// in the host driver (Pause+Option 2s hold), not via profiles, because the
// profile system remaps gamepad buttons only — it can't move keys between
// the keypad's keyboard report and the gamepad report.

#ifndef JAG2USB_PROFILES_H
#define JAG2USB_PROFILES_H

#include "core/services/profiles/profile.h"

// ============================================================================
// DEFAULT PROFILE
// ============================================================================

static const profile_t jag2usb_profiles[] = {
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

static const profile_set_t jag2usb_profile_set = {
    .profiles = jag2usb_profiles,
    .profile_count = sizeof(jag2usb_profiles) / sizeof(jag2usb_profiles[0]),
    .default_index = 0,
};

#endif // JAG2USB_PROFILES_H
