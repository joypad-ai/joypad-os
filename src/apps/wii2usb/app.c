// app.c - WII2USB App Entry Point
// Wii extension controllers (Nunchuck / Classic / Classic Pro) to USB HID.

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/profiles/profile.h"
#include "core/services/players/feedback.h"
#include "core/services/button/button.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "usb/usbd/usbd.h"
#include "native/host/wii_ext/wii_ext_host.h"
#include "core/services/leds/leds.h"
#include "tusb.h"
#include "platform/platform.h"
#include <stdio.h>

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &wii_input_interface,
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
// BUTTON (BOOTSEL) — USB output mode switching
// ============================================================================

static void on_button_event(button_event_t event)
{
    switch (event) {
        case BUTTON_EVENT_DOUBLE_CLICK: {
            // Cycle to next USB output mode (e.g. SInput → XInput → PS3 → ...).
            printf("[app:wii2usb] Double-click — cycling USB output mode\n");
            tud_task_ext(1, false);
            platform_sleep_ms(50);
            tud_task_ext(1, false);

            usb_output_mode_t next = usbd_get_next_mode();
            printf("[app:wii2usb] Switching to %s\n", usbd_get_mode_name(next));
            usbd_set_mode(next);
            break;
        }
        case BUTTON_EVENT_TRIPLE_CLICK:
            // Reset to default HID gamepad mode (so CDC console comes back).
            printf("[app:wii2usb] Triple-click — resetting to HID mode\n");
            if (!usbd_reset_to_hid()) {
                printf("[app:wii2usb] Already in HID mode\n");
            }
            break;
        default:
            break;
    }
}

// ============================================================================
// APP INITIALIZATION
// ============================================================================

void app_init(void)
{
    printf("[app:wii2usb] Initializing WII2USB v%s\n", JOYPAD_VERSION);

    button_init();
    button_set_callback(on_button_event);

#if defined(WII_PIN_SDA2) && WII_PIN_SDA2 != 255
    wii_host_init_dual(WII_PIN_SDA, WII_PIN_SCL, WII_PIN_SDA2, WII_PIN_SCL2);
    printf("[app:wii2usb]   Dual nunchuck: SDA2=%d SCL2=%d\n", WII_PIN_SDA2, WII_PIN_SCL2);
#else
    wii_host_init_pins(WII_PIN_SDA, WII_PIN_SCL);
#endif

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

    router_add_route(INPUT_SOURCE_NATIVE_WII, OUTPUT_TARGET_USB_DEVICE, 0);

    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    static const profile_config_t profile_cfg = {
        .output_profiles = { NULL },
        .shared_profiles = &wii2usb_profile_set,
    };
    profile_init(&profile_cfg);

    printf("[app:wii2usb] Initialization complete\n");
    printf("[app:wii2usb]   Routing: Wii extension -> USB HID Gamepad\n");
    printf("[app:wii2usb]   Wii pins: SDA=%d SCL=%d @ %u Hz\n",
           WII_PIN_SDA, WII_PIN_SCL, (unsigned)WII_I2C_FREQ_HZ);
}

// ============================================================================
// APP TASK
// ============================================================================

void app_task(void)
{
    button_task();

    // Update LED color when USB output mode changes (mirrors snes2usb).
    static usb_output_mode_t last_led_mode = USB_OUTPUT_MODE_COUNT;
    usb_output_mode_t mode = usbd_get_mode();
    if (mode != last_led_mode) {
        uint8_t r, g, b;
        usbd_get_mode_color(mode, &r, &g, &b);
        leds_set_color(r, g, b);
        last_led_mode = mode;
    }
}
