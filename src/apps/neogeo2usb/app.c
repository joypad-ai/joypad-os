// app.c - NEOGEO2USB App Entry Point
// NEOGEO native controller input to USB HID gamepad output adapter
//
// This app polls native NEOGEO controllers and routes input to USB device output.

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/profiles/profile.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "core/services/button/button.h"
#include "usb/usbd/usbd.h"
#include "native/host/arcade/arcade_host.h"
#include <stdio.h>

static arcade_config_t arcade_config[ARCADE_MAX_PORTS] = {
    [0] = {
        .pin_du = NEOGEO_PIN_DU,
        .pin_dd = NEOGEO_PIN_DD,
        .pin_dl = NEOGEO_PIN_DL,
        .pin_dr = NEOGEO_PIN_DR,

        // Action Buttons
        .pin_p1 = NEOGEO_PIN_B1,
        .pin_p2 = NEOGEO_PIN_B2,
        .pin_p3 = NEOGEO_PIN_B3,
        .pin_p4 = GPIO_DISABLED,
        .pin_k1 = NEOGEO_PIN_B4,
        .pin_k2 = NEOGEO_PIN_B5,
        .pin_k3 = NEOGEO_PIN_B6,
        .pin_k4 = GPIO_DISABLED,

        // Meta Buttons
        .pin_s1 = NEOGEO_PIN_S1,
        .pin_s2 = NEOGEO_PIN_S2,
        .pin_a1 = GPIO_DISABLED,
        .pin_a2 = GPIO_DISABLED,

        // Extra Buttons
        .pin_l3 = GPIO_DISABLED,
        .pin_r3 = GPIO_DISABLED,
        .pin_l4 = GPIO_DISABLED,
        .pin_r4 = GPIO_DISABLED,
    }
};

// ============================================================================
// BUTTON EVENT HANDLER
// ============================================================================

static void on_button_event(button_event_t event)
{
    switch (event) {
        case BUTTON_EVENT_CLICK:
            printf("[app:usb2neogeo] Button click - current mode: %s\n",
                   usbd_get_mode_name(usbd_get_mode()));
            break;

        case BUTTON_EVENT_DOUBLE_CLICK: {
            // Double-click to cycle USB output mode
            printf("[app:usb2neogeo] Double-click - switching USB output mode...\n");
            tud_task();
            sleep_ms(50);
            tud_task();

            usb_output_mode_t next = usbd_get_next_mode();
            printf("[app:usb2neogeo] Switching to %s\n", usbd_get_mode_name(next));
            usbd_set_mode(next);
            break;
        }

        case BUTTON_EVENT_TRIPLE_CLICK:
            // Triple-click to reset to default HID mode
            printf("[app:usb2neogeo] Triple-click - resetting to HID mode...\n");
            if (!usbd_reset_to_hid()) {
                printf("[app:usb2neogeo] Already in HID mode\n");
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
    &arcade_input_interface,
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
    printf("[app:neogeo2usb] Initializing NEOGEO2USB v%s\n", APP_VERSION);

    // Initialize button service
    button_init();
    button_set_callback(on_button_event);

    // Initialize Arcade host driver input
    arcade_host_init_pins(&arcade_config[0]);

    // Configure router for NEOGEO → USB routing
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

    // Add route: Native NEOGEO → USB Device
    router_add_route(INPUT_SOURCE_NATIVE_ARCADE, OUTPUT_TARGET_USB_DEVICE, 0);

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
        .shared_profiles = &neogeo2usb_profile_set,
    };
    profile_init(&profile_cfg);

    printf("[app:neogeo2usb] Initialization complete\n");
    printf("[app:neogeo2usb]   Routing: NEOGEO → USB HID Gamepad\n");
    printf("[app:neogeo2usb]   NEOGEO pins: B1=%d B2=%d B3=%d B4=%d B5=%d B6=%d\n",
           arcade_config[0].pin_p1, arcade_config[0].pin_p2, arcade_config[0].pin_p3,
           arcade_config[0].pin_k1, arcade_config[0].pin_k2, arcade_config[0].pin_k3);
     printf("[app:neogeo2usb]   NEOGEO pins: DU=%d DD=%d DL=%d DR=%d S1=%d S2=%d\n",
           arcade_config[0].pin_du, arcade_config[0].pin_dd, arcade_config[0].pin_dl,
           arcade_config[0].pin_dr, arcade_config[0].pin_s1, arcade_config[0].pin_s2);
}

// ============================================================================
// APP TASK
// ============================================================================

void app_task(void)
{
    button_task();
}
