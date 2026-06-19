// app.c - N642NUON App Entry Point
// N64 controller to Nuon DVD player adapter
//
// Routes native N64 controller input to Nuon polyface output.
// Both protocols use PIO state machines:
// - Nuon: polyface on PIO0
// - N64: joybus on PIO1

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/profiles/profile.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/host/n64/n64_host.h"
#include "native/device/nuon/nuon_device.h"
#include "platform/platform.h"
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
// APP INPUT INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &n64_input_interface,
};

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = sizeof(input_interfaces) / sizeof(input_interfaces[0]);
    return input_interfaces;
}

// ============================================================================
// APP OUTPUT INTERFACES
// ============================================================================

extern const OutputInterface nuon_output_interface;

static const OutputInterface* output_interfaces[] = {
    &nuon_output_interface,
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
    printf("[app:n642nuon] Initializing N642NUON v%s\n", JOYPAD_VERSION);

    // Configure router
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_NUON] = NUON_OUTPUT_PORTS,
        },
        .merge_all_inputs = false,
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 0,
    };
    router_init(&router_cfg);

    // Add route: N64 -> Nuon
    router_add_route(INPUT_SOURCE_NATIVE_N64, OUTPUT_TARGET_NUON, 0);

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

    printf("[app:n642nuon] Initialization complete\n");
    printf("[app:n642nuon]   N64 data pin: GPIO%d\n", N64_DATA_PIN);
    printf("[app:n642nuon]   Profiles: %d (active: %s)\n", profile_count, active_name ? active_name : "none");
}

// ============================================================================
// APP TASK
// ============================================================================

void app_task(void)
{
    // LED status handled by leds_task() in main loop
}
