// main.c - GCUSB App Entry Point
// USB to GameCube adapter
//
// This file contains app-specific initialization and logic.
// The firmware calls app_init() after core system initialization.

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/profiles/profiles.h"
#include "common/output_interface.h"
#include "native/device/gamecube/gamecube_device.h"
#include <stdio.h>

// ============================================================================
// APP OUTPUT INTERFACE
// ============================================================================

// Provide output interface for firmware to use
extern const OutputInterface gamecube_output_interface;

const OutputInterface* app_get_output_interface(void)
{
    return &gamecube_output_interface;
}

// ============================================================================
// APP INITIALIZATION
// ============================================================================

void app_init(void)
{
    printf("[app:gcusb] Initializing GCUSB v%s\n", APP_VERSION);

    // Configure router for GCUSB
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_GAMECUBE] = GAMECUBE_OUTPUT_PORTS,
        },
        .merge_all_inputs = true,  // Merge all USB inputs to single port
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 8,
    };
    router_init(&router_cfg);

    // Add default route: USB → GameCube
    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_GAMECUBE, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Configure profile system
    profile_system_config_t profile_cfg = {
        .profile_count = 5,  // GameCube has 5 profiles
        .default_profile_index = 0,
    };
    profiles_init(&profile_cfg);

    printf("[app:gcusb] Initialization complete\n");
    printf("[app:gcusb]   Routing: %s\n", "MERGE_ALL (all USB → single GC port)");
    printf("[app:gcusb]   Player slots: %d (FIXED mode for future 4-port)\n", MAX_PLAYER_SLOTS);
    printf("[app:gcusb]   Profiles: %d\n", profile_cfg.profile_count);
}

// ============================================================================
// APP TASK (Optional - called from main loop)
// ============================================================================

void app_task(void)
{
    // App-specific periodic tasks go here
    // For GCUSB, most logic is in gamecube_device.c
}
