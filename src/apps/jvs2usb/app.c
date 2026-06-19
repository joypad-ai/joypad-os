// app.c - JVS2USB App Entry Point
// JVS native controller input to USB HID gamepad output adapter
//
// This app polls native JVS controllers and routes input to USB device output.

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/button/button.h"
#include "core/services/players/manager.h"
#include "core/services/profiles/profile.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "usb/usbd/usbd.h"
#include "native/host/jvs/jvs_host.h"
#include <stdio.h>

// ============================================================================
// BUTTON EVENT HANDLER
// ============================================================================

static void on_button_event(button_event_t event)
{
    switch (event) {
        case BUTTON_EVENT_CLICK:
            printf("[app:jvs2usb] Button click - current mode: %s\n",
                   usbd_get_mode_name(usbd_get_mode()));
            break;

        case BUTTON_EVENT_DOUBLE_CLICK: {
            // Cycle to next mode (usbd_set_mode flushes debug + saves to flash)
            usb_output_mode_t next = usbd_get_next_mode();
            printf("[app:jvs2usb] Double-click - switching USB mode → %s\n",
                   usbd_get_mode_name(next));
            usbd_set_mode(next);
            break;
        }

        case BUTTON_EVENT_TRIPLE_CLICK:
            // Triple-click to reset to default HID mode
            printf("[app:jvs2usb] Triple-click - resetting to HID mode...\n");
            if (!usbd_reset_to_hid()) {
                printf("[app:jvs2usb] Already in HID mode\n");
            }
            break;

        default:
            break;
    }
}

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &jvs_input_interface,
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
    printf("[app:jvs2usb] Initializing JVS2USB v%s\n", JOYPAD_VERSION);

    // Initialize button service
    button_init();
    button_set_callback(on_button_event);

    // Configure router for JVS → USB routing
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

    // Add route: Native JVS → USB Device
    router_add_route(INPUT_SOURCE_NATIVE_JVS, OUTPUT_TARGET_USB_DEVICE, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize profile system with button combos (Select+Start=Home)
    static const profile_config_t profile_cfg = {
        .output_profiles = { NULL },
        .shared_profiles = &jvs2usb_profile_set,
    };
    profile_init(&profile_cfg);

    printf("[app:jvs2usb] Initialization complete\n");
    printf("[app:jvs2usb]   Routing: JVS → USB HID Gamepad\n");
}

// ============================================================================
// APP TASK
// ============================================================================

void app_task(void)
{
    button_task();
}
