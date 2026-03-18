// app.c - NUON2USB App Entry Point
// Nuon controller to USB HID gamepad adapter
//
// This app polls native Nuon controllers via Polyface protocol and outputs USB HID gamepad.

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/services/profiles/profile.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "usb/usbd/usbd.h"
#include "native/host/nuon/nuon_host.h"
#include "core/services/leds/leds.h"
#include <stdio.h>

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &nuon_input_interface,
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
    printf("[app:nuon2usb] Initializing NUON2USB v%s\n", APP_VERSION);

    // Configure router for Nuon -> USB routing
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

    // Add route: Native Nuon -> USB Device
    router_add_route(INPUT_SOURCE_NATIVE_NUON, OUTPUT_TARGET_USB_DEVICE, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize profile system with Nuon profiles
    static const profile_config_t profile_cfg = {
        .output_profiles = { NULL },
        .shared_profiles = &nuon2usb_profile_set,
    };
    profile_init(&profile_cfg);

    printf("[app:nuon2usb] Initialization complete\n");
    printf("[app:nuon2usb]   Routing: Nuon -> USB HID Gamepad\n");
    printf("[app:nuon2usb]   Nuon data pin: GPIO%d\n", NUON_DATA_PIN);
    printf("[app:nuon2usb]   Profiles: %d (Select+DPad to cycle)\n", nuon2usb_profile_set.profile_count);
}

// ============================================================================
// APP TASK
// ============================================================================

void app_task(void)
{
    // Update LED color when USB output mode changes
    static usb_output_mode_t last_led_mode = USB_OUTPUT_MODE_COUNT;
    usb_output_mode_t mode = usbd_get_mode();
    if (mode != last_led_mode) {
        uint8_t r, g, b;
        usbd_get_mode_color(mode, &r, &g, &b);
        leds_set_color(r, g, b);
        last_led_mode = mode;
    }

    // Note: Nuon controllers don't have rumble, so no feedback forwarding needed
}
