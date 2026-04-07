// app.c - GC2USB App Entry Point
// GameCube controller to USB HID gamepad adapter
//
// This app polls native GameCube controllers via joybus and outputs USB HID gamepad.

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/services/profiles/profile.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "usb/usbd/usbd.h"
#include "native/host/gc/gc_host.h"
#include "core/services/leds/leds.h"
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
    printf("[app:gc2usb] Initializing GC2USB v%s\n", APP_VERSION);

    // Configure router for GC -> USB routing
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

    // Add route: Native GC -> USB Device
    router_add_route(INPUT_SOURCE_NATIVE_GC, OUTPUT_TARGET_USB_DEVICE, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize profile system with GC profiles
    static const profile_config_t profile_cfg = {
        .output_profiles = { NULL },
        .shared_profiles = &gc2usb_profile_set,
    };
    profile_init(&profile_cfg);

    // Initialize GC host with per-board pin configuration
#if GC_MAX_PORTS >= 4 && defined(GC_PIN_DATA_1) && defined(GC_PIN_DATA_2) && defined(GC_PIN_DATA_3)
    {
        uint8_t pins[] = { GC_PIN_DATA, GC_PIN_DATA_1, GC_PIN_DATA_2, GC_PIN_DATA_3 };
        gc_host_init_pins(pins, 4);
    }
#else
    gc_host_init_pin(GC_PIN_DATA);
#endif

    printf("[app:gc2usb] Initialization complete\n");
    printf("[app:gc2usb]   Routing: GC -> USB HID Gamepad\n");
    printf("[app:gc2usb]   GC ports: %d\n", GC_MAX_PORTS);
    printf("[app:gc2usb]   Profiles: %d (Select+DPad to cycle)\n", gc2usb_profile_set.profile_count);
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

    // Forward rumble from USB host to GC controller via feedback system
    // USB device receives rumble from host PC, GC controller reads from feedback
    if (usbd_output_interface.get_feedback) {
        output_feedback_t fb;
        if (usbd_output_interface.get_feedback(&fb) && fb.dirty) {
            // Set rumble for player 0 (GC controller)
            // Pass actual values so both on AND off commands are applied
            feedback_set_rumble(0, fb.rumble_left, fb.rumble_right);
        }
    }
}
