// app.c - SNES23DO App Entry Point
// SNES/NES native controller input to 3DO output adapter
//
// This app polls native SNES/NES controllers and routes input to 3DO output.

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/device/3do/3do_device.h"
#include "native/host/snes/snes_host.h"
#include <stdio.h>

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &snes_input_interface,
};

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = sizeof(input_interfaces) / sizeof(input_interfaces[0]);
    return input_interfaces;
}

// ============================================================================
// APP OUTPUT INTERFACES
// ============================================================================

extern const OutputInterface tdo_output_interface;

static const OutputInterface* output_interfaces[] = {
    &tdo_output_interface,
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

// ============================================================================
// APP TASK (Optional - called from main loop)
// ============================================================================

void app_task(void)
{
    // No periodic tasks needed for SNES23DO
}
