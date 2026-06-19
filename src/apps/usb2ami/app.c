// app.c - USB2AMI App Entry Point
// USB to Amiga/Atari DE9 adapter
//
// Initializes the Amiga/CD32 output driver and USB host input.
// Single player, simple 1:1 routing.

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/profiles/profile.h"
#include "core/services/storage/flash.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/device/amiga/amiga_device.h"
#include "usb/usbh/usbh.h"
#include <stdio.h>

// ============================================================================
// APP PROFILE CONFIGURATION
// ============================================================================

static const profile_config_t app_profile_config = {
    .output_profiles = {
        [OUTPUT_TARGET_AMIGA] = &ami_profile_set,
    },
    .shared_profiles = NULL,
};

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

static const OutputInterface* output_interfaces[] = {
    &amiga_output_interface,
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
    printf("[app:usb2ami] Initializing usb2ami v%s\n", JOYPAD_VERSION);

    // Initialize flash storage early so settings are available to device init
    // flash_init();

    // Configure router for single player USB -> Amiga
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_AMIGA] = 1,
        },
        .merge_all_inputs = false,
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 0,
    };
    router_init(&router_cfg);

    // Add route: USB -> Amiga
    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_AMIGA, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode       = PLAYER_SLOT_MODE,
        .max_slots       = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize profile system
    profile_init(&app_profile_config);

    printf("[app:usb2ami] Init complete\n");
    printf("[app:usb2ami]   Board:  %s\n", BOARD);
    printf("[app:usb2ami]   Mode:   CD32 (7-button serial)\n");
    printf("[app:usb2ami]   Output: Amiga DE9 with TXS0108E level shifting\n");
}

// ============================================================================
// APP TASK
// ============================================================================

void app_task(void)
{
    // No app-specific periodic work needed
}
