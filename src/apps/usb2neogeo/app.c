// app.c - USB2NEOGEO App Entry Point
// USB to NEOGEO+ adapter
//
// This file contains app-specific initialization and logic.
// The firmware calls app_init() after core system initialization.

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/device/neogeo/neogeo_device.h"
#include "usb/usbh/usbh.h"
#include <stdio.h>

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &usbh_input_interface,
};

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = sizeof(input_interfaces) / sizeof(input_interfaces[0]);
    return input_interfaces;
}

// ============================================================================
// APP OUTPUT INTERFACES
// ============================================================================

extern const OutputInterface neogeo_output_interface;

static const OutputInterface* output_interfaces[] = {
    &neogeo_output_interface,
};

const OutputInterface** app_get_output_interfaces(uint8_t* count)
{
    *count = sizeof(output_interfaces) / sizeof(output_interfaces[0]);
    return output_interfaces;
}

// ============================================================================
// APP INITIALIZATION
// ============================================================================

void app_init(void)
{
    printf("[app:usb2neogeo] Initializing USB2NEOGEO v%s\n", APP_VERSION);

    // Configure router for USB2NEOGEO
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_NEOGEO] = NEOGEO_OUTPUT_PORTS,  // 1 players
        },
        .merge_all_inputs = false,  // Simple 1:1 mapping (each USB device → NEOGEO adapter)
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 8,
    };
    router_init(&router_cfg);

    // Add default route: USB → NEOGEO
    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_NEOGEO, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    printf("[app:usb2neogeo] Initialization complete\n");
    printf("[app:usb2neogeo]   Routing: %s\n", "SIMPLE (USB → NEOGEO+ adapter 1:1)");
    printf("[app:usb2neogeo]   Player slots: %d (SHIFT mode - players shift on disconnect)\n", MAX_PLAYER_SLOTS);
}

// ============================================================================
// APP TASK (called in main loop)
// ============================================================================

void app_task(void)
{
    // No app-specific task work needed for usb2neogeo
}
