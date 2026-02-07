// app.c - SNES2USB App Entry Point
// SNES/NES native controller input to USB HID gamepad output adapter
//
// This app polls native SNES/NES controllers and routes input to USB device output.

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/profiles/profile.h"
#include "core/services/players/feedback.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "usb/usbd/usbd.h"
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

static const OutputInterface* output_interfaces[] = {
    &usbd_output_interface,
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
    printf("[app:snes2usb] Initializing SNES2USB v%s\n", APP_VERSION);

    // Initialize SNES host driver (native SNES controller input)
    snes_host_init_pins(SNES_PIN_CLOCK, SNES_PIN_LATCH, SNES_PIN_DATA0,
                        SNES_PIN_DATA1, SNES_PIN_IOBIT);

    // Configure router for SNES → USB routing
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_USB_DEVICE] = USB_OUTPUT_PORTS,
        },
        .merge_all_inputs = false,
        .transform_flags = TRANSFORM_NONE,
        .mouse_drain_rate = 8,
    };
    router_init(&router_cfg);

    // Add route: Native SNES → USB Device
    router_add_route(INPUT_SOURCE_NATIVE_SNES, OUTPUT_TARGET_USB_DEVICE, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize profile system (combos handled in snes_host.c)
    static const profile_config_t profile_cfg = {
        .output_profiles = { NULL },
        .shared_profiles = &snes2usb_profile_set,
    };
    profile_init(&profile_cfg);

    printf("[app:snes2usb] Initialization complete\n");
    printf("[app:snes2usb]   Routing: SNES/NES → USB HID Gamepad\n");
    printf("[app:snes2usb]   SNES pins: CLK=%d LATCH=%d D0=%d D1=%d IO=%d\n",
           SNES_PIN_CLOCK, SNES_PIN_LATCH, SNES_PIN_DATA0,
           SNES_PIN_DATA1, SNES_PIN_IOBIT);
}

// ============================================================================
// APP TASK
// ============================================================================

void app_task(void)
{
    // Forward rumble from USB host to SNES controller
    if (usbd_output_interface.get_feedback) {
        output_feedback_t fb;
        if (usbd_output_interface.get_feedback(&fb) && fb.dirty) {
            feedback_set_rumble(0, fb.rumble_left, fb.rumble_right);
        }
    }

    // Apply feedback to SNES controller
    feedback_state_t* state = feedback_get_state(0);
    if (state && state->rumble_dirty) {
        snes_host_set_rumble(0, state->rumble.left, state->rumble.right);
        feedback_clear_dirty(0);
    }
}
