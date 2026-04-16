// profiles.h - WII2N64 App Profiles

#ifndef WII2N64_PROFILES_H
#define WII2N64_PROFILES_H

#include "core/services/profiles/profile.h"

static const profile_t wii2n64_profiles[] = {
    {
        .name = "default",
        .description = "Wii passthrough to N64",
        .button_map = NULL,
        .button_map_count = 0,
        .combo_map = NULL,
        .combo_map_count = 0,
        PROFILE_TRIGGERS_DEFAULT,
        PROFILE_ANALOG_DEFAULT,
        .adaptive_triggers = false,
    },
};

static const profile_set_t wii2n64_profile_set = {
    .profiles = wii2n64_profiles,
    .profile_count = sizeof(wii2n64_profiles) / sizeof(wii2n64_profiles[0]),
    .default_index = 0,
};

#endif // WII2N64_PROFILES_H
