// main.c - 3DOUSB App Entry Point
// USB to 3DO adapter with 8-player support and extension passthrough
//
// This file contains app-specific initialization and logic.
// The firmware calls app_init() after core system initialization.

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/profiles/profiles.h"
#include "common/output_interface.h"
#include "native/device/3do/3do_device.h"
#include <stdio.h>

// ============================================================================
// APP OUTPUT INTERFACE
// ============================================================================

// Provide output interface for firmware to use
extern const OutputInterface threedooutput_interface;

const OutputInterface* app_get_output_interface(void)
{
    return &threedooutput_interface;
}

// ============================================================================
// APP INITIALIZATION
// ============================================================================

void app_init(void)
{
    printf("[app:3dousb] Initializing 3DOUSB v%s\n", APP_VERSION);

    // Configure router for 3DOUSB
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_3DO] = TDO_OUTPUT_PORTS,  // 8 players via PBUS
        },
        .merge_all_inputs = false,  // Simple 1:1 mapping (each USB device → PBUS port)
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 8,
    };
    router_init(&router_cfg);

    // Add default route: USB → 3DO
    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_3DO, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Note: 3DO profiles are managed by 3do_device.c using its own profile system
    // (stored in 3do_config.h). The universal profile system is not used for 3DO.

    printf("[app:3dousb] Initialization complete\n");
    printf("[app:3dousb]   Routing: %s\n", "SIMPLE (USB → 3DO PBUS 1:1)");
    printf("[app:3dousb]   Player slots: %d (SHIFT mode - players shift on disconnect)\n", MAX_PLAYER_SLOTS);
    printf("[app:3dousb]   Mouse support: enabled\n");
    printf("[app:3dousb]   Extension passthrough: enabled (native 3DO controllers)\n");
    printf("[app:3dousb]   Profile switching: enabled (SELECT+2sec, D-pad Up/Down)\n");
}
