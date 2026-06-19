// app.c - LoopyUSB App Entry Point
// USB to Casio Loopy adapter (experimental)
//
// This file contains app-specific initialization and logic.
// The firmware calls app_init() after core system initialization.

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/profiles/profile.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/device/loopy/loopy_device.h"
#include "usb/usbh/usbh.h"
#include <stdio.h>

// ============================================================================
// APP PROFILE CONFIGURATION
// ============================================================================

static const profile_config_t app_profile_config = {
    .output_profiles = {
        [OUTPUT_TARGET_LOOPY] = &loopy_profile_set,
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

extern const OutputInterface loopy_output_interface;

static const OutputInterface* output_interfaces[] = {
    &loopy_output_interface,
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
    printf("[app:usb2loopy] Initializing LoopyUSB v%s (EXPERIMENTAL)\n", JOYPAD_VERSION);

    // Configure router for LoopyUSB
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_LOOPY] = LOOPY_OUTPUT_PORTS,  // 4 players
        },
        .merge_all_inputs = false,  // Simple 1:1 mapping (each USB device → Loopy port)
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 8,
    };
    router_init(&router_cfg);

    // Add default route: USB → Loopy
    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_LOOPY, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize profile system with app-defined profiles
    profile_init(&app_profile_config);

    uint8_t profile_count = profile_get_count(OUTPUT_TARGET_LOOPY);
    const char* active_name = profile_get_name(OUTPUT_TARGET_LOOPY,
                                                profile_get_active_index(OUTPUT_TARGET_LOOPY));

    printf("[app:usb2loopy] Initialization complete\n");
    printf("[app:usb2loopy]   Routing: %s\n", "SIMPLE (USB → Loopy 1:1)");
    printf("[app:usb2loopy]   Player slots: %d (SHIFT mode - players shift on disconnect)\n", MAX_PLAYER_SLOTS);
    printf("[app:usb2loopy]   Profiles: %d (active: %s)\n", profile_count, active_name ? active_name : "none");
    printf("[app:usb2loopy]   Status: EXPERIMENTAL - protocol partially implemented\n");
}

// ============================================================================
// APP TASK
// ============================================================================

void app_task(void)
{
    // Nothing extra needed - output interface task handles everything
}
