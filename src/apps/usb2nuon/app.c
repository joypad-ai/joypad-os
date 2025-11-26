// app.c - NUONUSB App Entry Point
// USB to Nuon DVD player adapter
//
// This file contains app-specific initialization and logic.
// The firmware calls app_init() after core system initialization.

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/profile/profile.h"
#include "core/output_interface.h"
#include "native/device/nuon/nuon_device.h"
#include <stdio.h>

// ============================================================================
// APP PROFILE CONFIGURATION
// ============================================================================

static const profile_config_t app_profile_config = {
    .output_profiles = {
        [OUTPUT_TARGET_NUON] = &nuon_profile_set,
    },
    .shared_profiles = NULL,
};

// ============================================================================
// APP OUTPUT INTERFACE
// ============================================================================

// Provide output interface for firmware to use
extern const OutputInterface nuon_output_interface;

const OutputInterface* app_get_output_interface(void)
{
    return &nuon_output_interface;
}

// ============================================================================
// APP INITIALIZATION
// ============================================================================

void app_init(void)
{
    printf("[app:usb2nuon] Initializing NUONUSB v%s\n", APP_VERSION);

    // Configure router for NUONUSB
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_NUON] = NUON_OUTPUT_PORTS,  // Single player
        },
        .merge_all_inputs = false,  // Simple 1:1 mapping
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 8,
    };
    router_init(&router_cfg);

    // Add default route: USB → Nuon
    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_NUON, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize profile system with app-defined profiles
    profile_init(&app_profile_config);

    uint8_t profile_count = profile_get_count(OUTPUT_TARGET_NUON);
    const char* active_name = profile_get_name(OUTPUT_TARGET_NUON,
                                                profile_get_active_index(OUTPUT_TARGET_NUON));

    printf("[app:usb2nuon] Initialization complete\n");
    printf("[app:usb2nuon]   Routing: %s\n", "SIMPLE (USB → Nuon 1:1)");
    printf("[app:usb2nuon]   Player slots: %d (single player)\n", MAX_PLAYER_SLOTS);
    printf("[app:usb2nuon]   Spinner support: enabled (right stick → spinner)\n");
    printf("[app:usb2nuon]   Soft reset: enabled (button combo)\n");
    printf("[app:usb2nuon]   Profiles: %d (active: %s)\n", profile_count, active_name ? active_name : "none");
}
