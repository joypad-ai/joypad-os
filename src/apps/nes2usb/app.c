#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/profiles/profile.h"
#include "core/services/button/button.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "usb/usbd/usbd.h"
#include "nes_host.h"

#include "tusb.h"

static const InputInterface* input_interfaces[] = {
    &nes_input_interface
};

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = sizeof(input_interfaces) / sizeof(input_interfaces[0]);
    return input_interfaces;
}

static const OutputInterface* output_interfaces[] = {
    &usbd_output_interface,
};

const OutputInterface** app_get_output_interfaces(uint8_t* count)
{
    *count = sizeof(output_interfaces) / sizeof(output_interfaces[0]);
    return output_interfaces;
}

static void on_button_event(button_event_t event)
{
    switch (event) {

        case BUTTON_EVENT_DOUBLE_CLICK: {
            // Double-click to cycle USB output mode
            printf("[app:nes2usb] Double-click - switching USB output mode...\n");
            tud_task();
            sleep_ms(50);
            tud_task();

            usb_output_mode_t next = usbd_get_next_mode();
            printf("[app:nes2usb] Switching to %s\n", usbd_get_mode_name(next));
            usbd_set_mode(next);
            break;
        }

        case BUTTON_EVENT_TRIPLE_CLICK:
            // Triple-click to reset to default HID mode
            printf("[app:nes2usb] Triple-click - resetting to HID mode...\n");
            if (!usbd_reset_to_hid()) {
                printf("[app:nes2usb] Already in HID mode\n");
            }
            break;

        default:
            break;
    }
}

void app_init(void)
{
    printf("[app:nes2usb] Initializing NES2USB v%s\n", APP_VERSION);

    // Initialize button service (uses BOOTSEL button on Pico W)
    button_init();
    button_set_callback(on_button_event);

    // Configure router for NES -> USB routing
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_USB_DEVICE] = USB_OUTPUT_PORTS,
        },
        .merge_all_inputs = false,
        .transform_flags = TRANSFORM_NONE,
    };
    router_init(&router_cfg);

    // Add route: Native NES -> USB Device
    router_add_route(INPUT_SOURCE_NATIVE_NES, OUTPUT_TARGET_USB_DEVICE, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize profile system
    static const profile_config_t profile_cfg = {
        .output_profiles = { NULL },
        .shared_profiles = &nes2usb_profile_set
    };

    profile_init(&profile_cfg);

    printf("[app:nes2usb] Initialization complete\n");
    printf("[app:nes2usb]   Routing: NES → USB HID Gamepad\n");
    printf("[app:nes2usb]   NES pins: CLK=%d LATCH=%d D0=%d\n",
           NES_PIN_CLOCK, NES_PIN_LATCH, NES_PIN_DATA0);
    
}

void app_task(void)
{
    // Process button input
    button_task();

    // Update LED color when USB output mode changes
    static usb_output_mode_t last_led_mode = USB_OUTPUT_MODE_COUNT;
    usb_output_mode_t mode = usbd_get_mode();
    if (mode != last_led_mode) {
        uint8_t r, g, b;
        usbd_get_mode_color(mode, &r, &g, &b);
        leds_set_color(r, g, b);
        last_led_mode = mode;
    }
}
