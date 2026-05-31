// app.c - GC2DC App Entry Point
// GameCube controller -> Dreamcast adapter
//
// Routes native GameCube controller input to Dreamcast Maple Bus output.
// Both protocols use PIO state machines:
// - Dreamcast: Maple TX on PIO0 (SM0), Maple RX on PIO1 (SM0-2) using 10 slots
// - GameCube: joybus on PIO1 (SM3) at offset 10, leaving room for maple_rx
// Same PIO layout n642dc uses; gc_host shares the joybus.pio program with the
// N64 driver, so this slots in without any new state-machine contention.

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/host/gc/gc_host.h"
#include "native/device/dreamcast/dreamcast_device.h"
#include <stdio.h>

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &gc_input_interface,
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
    printf("[app:gc2dc] Initializing GC2DC v%s\n", APP_VERSION);

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

    // Native GC controller -> Dreamcast port 0
    router_add_route(INPUT_SOURCE_NATIVE_GC, OUTPUT_TARGET_DREAMCAST, 0);

    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    printf("[app:gc2dc] Initialization complete\n");
    printf("[app:gc2dc]   GameCube data pin: GPIO%d\n", GC_PIN_DATA);
    printf("[app:gc2dc]   Dreamcast Maple pins: GPIO%d, GPIO%d\n",
           DC_MAPLE_PIN1, DC_MAPLE_PIN5);
}

// ============================================================================
// APP TASK
// ============================================================================

void app_task(void)
{
    // Forward rumble from Dreamcast (Puru Puru) to the GC controller's
    // built-in motor via the feedback system. gc_host reads this and drives
    // the rumble bit on the next poll command.
    static uint8_t last_rumble = 0;
    if (dreamcast_output_interface.get_rumble) {
        uint8_t rumble = dreamcast_output_interface.get_rumble();
        if (rumble != last_rumble) {
            last_rumble = rumble;
            feedback_set_rumble(0, rumble, rumble);
        }
    }
}
