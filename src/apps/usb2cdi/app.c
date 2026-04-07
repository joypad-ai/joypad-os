// app.c - USB2CDI App Entry Point
// USB/BT controllers to Philips CD-i console adapter

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/profiles/profile.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/device/cdi/cdi_device.h"
#include <stdio.h>

// ============================================================================
// APP PROFILE CONFIGURATION
// ============================================================================

static const profile_config_t app_profile_config = {
    .output_profiles = {
        [OUTPUT_TARGET_CDI] = &cdi_profile_set,
    },
    .shared_profiles = NULL,
};

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = 0;
    return NULL;  // USB host input handled by COMMON_SOURCES
}

// ============================================================================
// APP OUTPUT INTERFACES
// ============================================================================

extern const OutputInterface cdi_output_interface;

static const OutputInterface* output_interfaces[] = {
    &cdi_output_interface,
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
    printf("[app:usb2cdi] Initializing USB2CDI v%s\n", APP_VERSION);

    // Configure router
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_CDI] = CDI_OUTPUT_PORTS,
        },
        .merge_all_inputs = false,
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 8,
    };
    router_init(&router_cfg);

    // Add route: USB/BT → CD-i
    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_CDI, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize profile system
    profile_init(&app_profile_config);

    printf("[app:usb2cdi] Initialization complete\n");
    printf("[app:usb2cdi]   Routing: USB/BT -> CD-i\n");
    printf("[app:usb2cdi]   TX=GPIO%d RTS=GPIO%d\n", CDI_TX_PIN, CDI_RTS_PIN);
}

// ============================================================================
// APP TASK
// ============================================================================

void app_task(void)
{
    // Nothing extra needed — cdi_task() handles everything via OutputInterface
}
