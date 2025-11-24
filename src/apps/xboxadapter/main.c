// main.c - Xbox Adapter App Entry Point
// USB to Xbox One adapter (hardware passthrough)
//
// This file contains app-specific initialization and logic.
// The firmware calls app_init() after core system initialization.

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "common/output_interface.h"
#include "native/device/xboxone/xboxone_device.h"
#include <stdio.h>

// ============================================================================
// APP OUTPUT INTERFACE
// ============================================================================

// Provide output interface for firmware to use
extern const OutputInterface xboxone_output_interface;

const OutputInterface* app_get_output_interface(void)
{
    return &xboxone_output_interface;
}

// ============================================================================
// APP INITIALIZATION
// ============================================================================

void app_init(void)
{
    printf("[app:xboxadapter] Initializing Xbox-Adapter v%s\n", APP_VERSION);

    // Configure router for Xbox Adapter
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_XBOXONE] = XBOXONE_OUTPUT_PORTS,  // Single player
        },
        .merge_all_inputs = false,  // Simple 1:1 mapping
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 8,
    };
    router_init(&router_cfg);

    // Add default route: USB → Xbox One
    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_XBOXONE, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    printf("[app:xboxadapter] Initialization complete\n");
    printf("[app:xboxadapter]   Routing: %s\n", "SIMPLE (USB → Xbox One 1:1)");
    printf("[app:xboxadapter]   Player slots: %d (single player)\n", MAX_PLAYER_SLOTS);
    printf("[app:xboxadapter]   Mouse support: enabled\n");
    printf("[app:xboxadapter]   I2C passthrough: enabled (GPIO expander emulation)\n");
    printf("[app:xboxadapter]   DAC analog: enabled (MCP4728 for sticks/triggers)\n");
}
