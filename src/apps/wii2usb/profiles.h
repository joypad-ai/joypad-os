// profiles.h - WII2USB App Profiles
// SPDX-License-Identifier: Apache-2.0

#ifndef WII2USB_PROFILES_H
#define WII2USB_PROFILES_H

#include "core/services/profiles/profile.h"

static const profile_t wii2usb_profiles[] = {
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

static const profile_set_t wii2usb_profile_set = {
    .profiles = wii2usb_profiles,
    .profile_count = sizeof(wii2usb_profiles) / sizeof(wii2usb_profiles[0]),
    .default_index = 0,
};

#endif // WII2USB_PROFILES_H
