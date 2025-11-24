// main.c - USB2PCE App Entry Point
// USB to PCEngine/TurboGrafx-16 adapter
//
// This file contains app-specific initialization and logic.
// The firmware calls app_init() after core system initialization.

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "common/output_interface.h"
#include "native/device/pcengine/pcengine_device.h"
#include <stdio.h>

// ============================================================================
// APP OUTPUT INTERFACE
// ============================================================================

// Provide output interface for firmware to use
extern const OutputInterface pcengine_output_interface;

const OutputInterface* app_get_output_interface(void)
{
    return &pcengine_output_interface;
}

// ============================================================================
// APP INITIALIZATION
// ============================================================================

void app_init(void)
{
    printf("[app:usb2pce] Initializing USB2PCE v%s\n", APP_VERSION);

    // Configure router for USB2PCE
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_PCENGINE] = PCENGINE_OUTPUT_PORTS,  // 5 players via multitap
        },
        .merge_all_inputs = false,  // Simple 1:1 mapping (each USB device → multitap port)
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 8,
    };
    router_init(&router_cfg);

    // Add default route: USB → PCEngine
    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_PCENGINE, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    printf("[app:usb2pce] Initialization complete\n");
    printf("[app:usb2pce]   Routing: %s\n", "SIMPLE (USB → PCE multitap 1:1)");
    printf("[app:usb2pce]   Player slots: %d (SHIFT mode - players shift on disconnect)\n", MAX_PLAYER_SLOTS);
    printf("[app:usb2pce]   Mouse support: enabled (Populous)\n");
}
