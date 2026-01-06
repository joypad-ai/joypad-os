// app.c - BT2USB App Entry Point
// Bluetooth to USB HID gamepad adapter for Pico W
//
// Uses Pico W's built-in CYW43 Bluetooth to receive controllers,
// outputs as USB HID device.

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/services/button/button.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "usb/usbd/usbd.h"
#include "bt/transport/bt_transport.h"
#include "bt/btstack/btstack_host.h"

#include "tusb.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include <stdio.h>

// External: CYW43 transport
extern const bt_transport_t bt_transport_cyw43;

// ============================================================================
// LED STATUS
// ============================================================================

static uint32_t led_last_toggle = 0;
static bool led_state = false;

// Update LED based on connection status
// - Slow blink (1Hz): No controllers connected, waiting
// - Solid on: Controller connected
static void led_status_update(void)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());

    if (playersCount > 0) {
        // Controller connected - solid on
        if (!led_state) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            led_state = true;
        }
    } else {
        // No controllers - slow blink (500ms on/off = 1Hz)
        if (now - led_last_toggle >= 500) {
            led_state = !led_state;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state ? 1 : 0);
            led_last_toggle = now;
        }
    }
}

// ============================================================================
// BUTTON EVENT HANDLER
// ============================================================================

static void on_button_event(button_event_t event)
{
    switch (event) {
        case BUTTON_EVENT_CLICK:
            printf("[app:bt2usb] Button click - current mode: %s\n",
                   usbd_get_mode_name(usbd_get_mode()));
            break;

        case BUTTON_EVENT_DOUBLE_CLICK: {
            // Double-click to cycle USB output mode
            printf("[app:bt2usb] Double-click - switching USB output mode...\n");
            tud_task();
            sleep_ms(50);
            tud_task();

            usb_output_mode_t next = usbd_get_next_mode();
            printf("[app:bt2usb] Switching to %s\n", usbd_get_mode_name(next));
            usbd_set_mode(next);
            break;
        }

        case BUTTON_EVENT_TRIPLE_CLICK:
            // Triple-click to reset to default HID mode
            printf("[app:bt2usb] Triple-click - resetting to HID mode...\n");
            if (!usbd_reset_to_hid()) {
                printf("[app:bt2usb] Already in HID mode\n");
            }
            break;

        case BUTTON_EVENT_HOLD:
            // Long press to clear all Bluetooth bonds
            printf("[app:bt2usb] Clearing all Bluetooth bonds...\n");
            btstack_host_delete_all_bonds();
            break;

        default:
            break;
    }
}

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

// BT2USB has no InputInterface - BT transport handles input internally
// via bthid drivers that call router_submit_input()

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = 0;
    return NULL;
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
    printf("[app:bt2usb] Initializing BT2USB v%s\n", APP_VERSION);
    printf("[app:bt2usb] Pico W built-in Bluetooth -> USB HID\n");

    // Initialize button service (uses BOOTSEL button on Pico W)
    button_init();
    button_set_callback(on_button_event);

    // Configure router for BT2USB
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_USB_DEVICE] = USB_OUTPUT_PORTS,
        },
        .merge_all_inputs = true,  // Merge all BT inputs to single output
        .transform_flags = TRANSFORM_FLAGS,
    };
    router_init(&router_cfg);

    // Add default route: BLE Central → USB Device
    router_add_route(INPUT_SOURCE_BLE_CENTRAL, OUTPUT_TARGET_USB_DEVICE, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize Bluetooth transport (CYW43)
    // Must use bt_init() to set global transport pointer and register drivers
    printf("[app:bt2usb] Initializing Bluetooth...\n");
    bt_init(&bt_transport_cyw43);

    printf("[app:bt2usb] Initialization complete\n");
    printf("[app:bt2usb]   Routing: Bluetooth -> USB Device (HID Gamepad)\n");
    printf("[app:bt2usb]   Player slots: %d\n", MAX_PLAYER_SLOTS);
    printf("[app:bt2usb]   Hold BOOTSEL to clear BT bonds\n");
    printf("[app:bt2usb]   Double-click BOOTSEL to switch USB mode\n");
}

// ============================================================================
// APP TASK (Called from main loop)
// ============================================================================

void app_task(void)
{
    // Process button input
    button_task();

    // Process Bluetooth transport
    bt_task();

    // Update LED status
    led_status_update();

    // Route feedback from USB device output to BT controllers
    if (usbd_output_interface.get_feedback) {
        output_feedback_t fb;
        if (usbd_output_interface.get_feedback(&fb)) {
            for (int i = 0; i < playersCount; i++) {
                feedback_set_rumble(i, fb.rumble_left, fb.rumble_right);
                if (fb.led_player > 0) {
                    feedback_set_led_player(i, fb.led_player);
                }
                if (fb.led_r || fb.led_g || fb.led_b) {
                    feedback_set_led_rgb(i, fb.led_r, fb.led_g, fb.led_b);
                }
            }
        }
    }
}
