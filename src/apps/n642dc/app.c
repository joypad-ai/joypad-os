// app.c - N642DC App Entry Point
// N64 controller to Dreamcast adapter
//
// Routes native N64 controller input to Dreamcast Maple Bus output.
// Both protocols use PIO state machines:
// - N64: joybus on PIO0
// - Dreamcast: Maple TX on PIO0, Maple RX on PIO1

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/host/n64/n64_host.h"
#include "native/device/dreamcast/dreamcast_device.h"
#include <stdio.h>

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

static const OutputInterface* output_interfaces[] = {
    &dreamcast_output_interface,
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
    printf("[app:n642dc] Initializing N642DC v%s\n", APP_VERSION);

    // Configure router
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_DREAMCAST] = DREAMCAST_OUTPUT_PORTS,
        },
        .merge_all_inputs = false,
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 0,
    };
    router_init(&router_cfg);

    // Add route: N64 -> Dreamcast
    router_add_route(INPUT_SOURCE_NATIVE_N64, OUTPUT_TARGET_DREAMCAST, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    printf("[app:n642dc] Initialization complete\n");
    printf("[app:n642dc]   N64 data pin: GPIO%d\n", N64_DATA_PIN);
    printf("[app:n642dc]   Dreamcast Maple pins: GPIO%d, GPIO%d\n",
           DC_MAPLE_PIN1, DC_MAPLE_PIN5);
}

// ============================================================================
// APP TASK
// ============================================================================

void app_task(void)
{
    // Forward rumble from Dreamcast to N64 controller
    if (dreamcast_output_interface.get_rumble) {
        uint8_t rumble = dreamcast_output_interface.get_rumble();
        n64_host_set_rumble(0, rumble > 0);
    }
}
