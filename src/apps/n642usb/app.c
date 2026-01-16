// app.c - N642USB App Entry Point
// N64 controller to USB HID gamepad adapter
//
// This app polls native N64 controllers via joybus and outputs USB HID gamepad.

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "usb/usbd/usbd.h"
#include "native/host/n64/n64_host.h"
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
    printf("[app:n642usb] Initializing N642USB v%s\n", APP_VERSION);

    // Configure router for N64 -> USB routing
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_USB_DEVICE] = USB_OUTPUT_PORTS,
        },
        .merge_all_inputs = false,
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 0,
    };
    router_init(&router_cfg);

    // Add route: Native N64 -> USB Device
    router_add_route(INPUT_SOURCE_NATIVE_N64, OUTPUT_TARGET_USB_DEVICE, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    printf("[app:n642usb] Initialization complete\n");
    printf("[app:n642usb]   Routing: N64 -> USB HID Gamepad\n");
    printf("[app:n642usb]   N64 data pin: GPIO%d\n", N64_DATA_PIN);
}

// ============================================================================
// APP TASK
// ============================================================================

void app_task(void)
{
    // Forward rumble from USB host to N64 controller via feedback system
    // USB device receives rumble from host PC, N64 controller reads from feedback
    feedback_state_t* fb = feedback_get_state(0);
    if (fb && fb->rumble_dirty) {
        // N64 host will pick this up in its task via feedback_get_state()
        // Nothing extra needed here - n64_host_task handles it
    }
}
