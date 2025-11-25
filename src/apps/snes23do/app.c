// app.c - SNES23DO App Entry Point
// SNES/NES native controller input to 3DO output adapter
//
// This app polls native SNES/NES controllers and routes input to 3DO output.

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/output_interface.h"
#include "native/device/3do/3do_device.h"
#include "native/host/snes/snes_host.h"
#include <stdio.h>

// ============================================================================
// APP OUTPUT INTERFACE (wraps 3DO interface + SNES host polling)
// ============================================================================

// Base 3DO output interface
extern const OutputInterface threedooutput_interface;

// Wrapper task that polls SNES input AND runs 3DO output task
static void snes23do_task(void)
{
    // Poll native SNES controller input
    snes_host_task();

    // Run 3DO output task (if any)
    if (threedooutput_interface.task) {
        threedooutput_interface.task();
    }
}

// Wrapped output interface with SNES polling in task
static const OutputInterface snes23do_output_interface = {
    .name = "SNES23DO",
    .init = NULL,  // 3DO init called separately, SNES init in app_init
    .handle_input = NULL,  // Uses router
    .core1_entry = NULL,  // Set dynamically from 3DO interface
    .task = snes23do_task,  // Our wrapper task
    .get_rumble = NULL,
    .get_player_led = NULL,
    .get_profile_count = NULL,
    .get_active_profile = NULL,
    .set_active_profile = NULL,
    .get_profile_name = NULL,
    .get_trigger_threshold = NULL,
};

// Runtime-assembled interface (copies 3DO interface, overrides task)
static OutputInterface runtime_interface;

const OutputInterface* app_get_output_interface(void)
{
    // Copy 3DO interface as base
    runtime_interface = threedooutput_interface;

    // Override name and task
    runtime_interface.name = "SNES23DO";
    runtime_interface.task = snes23do_task;

    return &runtime_interface;
}

// ============================================================================
// APP INITIALIZATION
// ============================================================================

void app_init(void)
{
    printf("[app:snes23do] Initializing SNES23DO v%s\n", APP_VERSION);

    // Initialize SNES host driver (native SNES controller input)
    snes_host_init_pins(SNES_PIN_CLOCK, SNES_PIN_LATCH, SNES_PIN_DATA0,
                        SNES_PIN_DATA1, SNES_PIN_IOBIT);

    // Configure router for SNES → 3DO routing
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_3DO] = TDO_OUTPUT_PORTS,
        },
        .merge_all_inputs = false,
        .transform_flags = TRANSFORM_NONE,
        .mouse_drain_rate = 8,
    };
    router_init(&router_cfg);

    // Add route: Native SNES → 3DO
    router_add_route(INPUT_SOURCE_NATIVE_SNES, OUTPUT_TARGET_3DO, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    printf("[app:snes23do] Initialization complete\n");
    printf("[app:snes23do]   Routing: SNES/NES → 3DO\n");
    printf("[app:snes23do]   SNES pins: CLK=%d LATCH=%d D0=%d D1=%d IO=%d\n",
           SNES_PIN_CLOCK, SNES_PIN_LATCH, SNES_PIN_DATA0,
           SNES_PIN_DATA1, SNES_PIN_IOBIT);
}
