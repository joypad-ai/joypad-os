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
#include "core/services/leds/leds.h"

#include "tusb.h"
#include "platform/platform.h"
#include <stdio.h>

#ifdef BTSTACK_USE_ESP32
#include "driver/gpio.h"
extern const bt_transport_t bt_transport_esp32;
// Status LED GPIO (Seeed XIAO ESP32-S3 = GPIO 21, active low)
#ifndef STATUS_LED_GPIO
#define STATUS_LED_GPIO 21
#endif
#ifndef STATUS_LED_ACTIVE_LOW
#define STATUS_LED_ACTIVE_LOW 1
#endif
#else
#include "pico/cyw43_arch.h"
extern const bt_transport_t bt_transport_cyw43;
#endif

// ============================================================================
// LED STATUS
// ============================================================================

static uint32_t led_last_toggle = 0;
static bool led_state = false;

// Update LED based on connection status
// - Blink (0.8s): No device connected (scanning, connecting, or idle)
// - Solid on: Device connected
static void platform_led_set(bool on)
{
#ifdef BTSTACK_USE_ESP32
    gpio_set_level(STATUS_LED_GPIO, (on ^ STATUS_LED_ACTIVE_LOW) ? 1 : 0);
#else
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on ? 1 : 0);
#endif
}

static void led_status_update(void)
{
    uint32_t now = platform_time_ms();

    if (btstack_classic_get_connection_count() > 0) {
        // Device connected - solid on
        if (!led_state) {
            platform_led_set(true);
            led_state = true;
        }
    } else {
        // No device connected - blink
        if (now - led_last_toggle >= 400) {
            led_state = !led_state;
            platform_led_set(led_state);
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
            // Start/extend 60-second BT scan for additional devices
            printf("[app:bt2usb] Starting BT scan (60s)...\n");
            btstack_host_start_timed_scan(60000);
            break;

        case BUTTON_EVENT_DOUBLE_CLICK: {
            // Double-click to cycle USB output mode
            printf("[app:bt2usb] Double-click - switching USB output mode...\n");
            tud_task();
            platform_sleep_ms(50);
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
            // Long press to disconnect all devices and clear all bonds
            printf("[app:bt2usb] Disconnecting all devices and clearing bonds...\n");
            btstack_host_disconnect_all_devices();
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
#ifdef BTSTACK_USE_ESP32
    printf("[app:bt2usb] ESP32-S3 BLE -> USB HID\n");
    // Init status LED GPIO
    gpio_config_t led_cfg = {
        .pin_bit_mask = (1ULL << STATUS_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&led_cfg);
    gpio_set_level(STATUS_LED_GPIO, STATUS_LED_ACTIVE_LOW ? 1 : 0);  // Start OFF
#else
    printf("[app:bt2usb] Pico W built-in Bluetooth -> USB HID\n");
#endif

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

    // Initialize Bluetooth transport
    // Must use bt_init() to set global transport pointer and register drivers
    printf("[app:bt2usb] Initializing Bluetooth...\n");
#ifdef BTSTACK_USE_ESP32
    bt_init(&bt_transport_esp32);
#else
    bt_init(&bt_transport_cyw43);
#endif

    printf("[app:bt2usb] Initialization complete\n");
    printf("[app:bt2usb]   Routing: Bluetooth -> USB Device (HID Gamepad)\n");
    printf("[app:bt2usb]   Player slots: %d\n", MAX_PLAYER_SLOTS);
    printf("[app:bt2usb]   Click BOOTSEL for 60s BT scan\n");
    printf("[app:bt2usb]   Hold BOOTSEL to disconnect all + clear bonds\n");
    printf("[app:bt2usb]   Double-click BOOTSEL to switch USB mode\n");
}

// ============================================================================
// APP TASK (Called from main loop)
// ============================================================================

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

    // Process Bluetooth transport
    bt_task();

    // Update LED status
    leds_set_connected_devices(btstack_classic_get_connection_count());
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
