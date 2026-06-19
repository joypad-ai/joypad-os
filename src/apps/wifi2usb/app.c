// app.c - WiFi2USB App Entry Point
// WiFi to USB HID gamepad adapter for Pico W
//
// Uses Pico W's CYW43 WiFi in AP mode to receive JOCP controller packets,
// outputs as USB HID device.

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/services/button/button.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "usb/usbd/usbd.h"
#include "core/services/leds/leds.h"
#include "wifi/jocp/jocp.h"
#include "wifi/jocp/wifi_transport.h"
#include "wifi/ble_beacon.h"

#include "tusb.h"
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include <stdio.h>

// ============================================================================
// LED STATUS
// ============================================================================

static uint32_t led_last_toggle = 0;
static bool led_state = false;

// LED blink patterns:
// - Fast blink (4Hz): WiFi AP starting
// - Slow blink (1Hz): Pairing mode (SSID visible, waiting for controller)
// - Solid on: Controller connected, not pairing
// - Double blink: Pairing mode with controller connected
static void led_status_update(void)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());

    if (!wifi_transport_is_ready()) {
        // WiFi not ready - fast blink (125ms on/off = 4Hz)
        if (now - led_last_toggle >= 125) {
            led_state = !led_state;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state ? 1 : 0);
            led_last_toggle = now;
        }
    } else if (wifi_transport_is_pairing_mode()) {
        // Pairing mode - slow blink (500ms = 1Hz) to show SSID is broadcasting
        if (now - led_last_toggle >= 500) {
            led_state = !led_state;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state ? 1 : 0);
            led_last_toggle = now;
        }
    } else if (jocp_get_connected_count() > 0) {
        // Controller connected, not pairing - solid on
        if (!led_state) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            led_state = true;
        }
    } else {
        // No controllers, not pairing - solid off
        if (led_state) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
            led_state = false;
        }
    }
}

// ============================================================================
// BUTTON EVENT HANDLER
// ============================================================================

// Pairing timeout in seconds (SSID visible for this long after button press)
#define PAIRING_TIMEOUT_SEC 30

static void on_button_event(button_event_t event)
{
    switch (event) {
        case BUTTON_EVENT_CLICK:
            // Single click - enter pairing mode (broadcast SSID)
            printf("[app:wifi2usb] Button click - entering pairing mode\n");
            printf("[app:wifi2usb] SSID will be visible for %d seconds\n", PAIRING_TIMEOUT_SEC);
            wifi_transport_start_pairing(PAIRING_TIMEOUT_SEC);
            break;

        case BUTTON_EVENT_DOUBLE_CLICK: {
            // Cycle to next mode (usbd_set_mode flushes debug + saves to flash)
            usb_output_mode_t next = usbd_get_next_mode();
            printf("[app:wifi2usb] Double-click - switching USB mode → %s\n",
                   usbd_get_mode_name(next));
            usbd_set_mode(next);
            break;
        }

        case BUTTON_EVENT_TRIPLE_CLICK:
            // Triple-click to reset to default SInput mode
            printf("[app:wifi2usb] Triple-click - resetting to SInput mode...\n");
            if (!usbd_reset_to_hid()) {
                printf("[app:wifi2usb] Already in SInput mode\n");
            }
            break;

        case BUTTON_EVENT_HOLD:
            // Long press to restart WiFi AP
            printf("[app:wifi2usb] Restarting WiFi AP...\n");
            wifi_transport_restart();
            break;

        default:
            break;
    }
}

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

// WiFi2USB has no InputInterface - JOCP transport handles input internally
// via jocp_input that calls router_submit_input()

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
    printf("[app:wifi2usb] Initializing WiFi2USB v%s\n", JOYPAD_VERSION);
    printf("[app:wifi2usb] Pico W WiFi AP -> USB HID\n");

    // Initialize button service (uses BOOTSEL button on Pico W)
    button_init();
    button_set_callback(on_button_event);

    // Configure router for WiFi2USB
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_USB_DEVICE] = USB_OUTPUT_PORTS,
        },
        .merge_all_inputs = true,  // Merge all WiFi inputs to single output
        .transform_flags = TRANSFORM_FLAGS,
    };
    router_init(&router_cfg);

    // Add default route: WiFi Input → USB Device
    router_add_route(INPUT_SOURCE_WIFI, OUTPUT_TARGET_USB_DEVICE, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize WiFi transport (CYW43 AP mode)
    printf("[app:wifi2usb] Initializing WiFi AP...\n");
    wifi_transport_config_t wifi_cfg = {
        .ssid_prefix = WIFI_AP_SSID_PREFIX,
        .password = WIFI_AP_PASSWORD,
        .channel = WIFI_AP_CHANNEL,
        .max_connections = WIFI_MAX_CONNECTIONS,
        .udp_port = JOCP_UDP_PORT,
        .tcp_port = JOCP_TCP_PORT,
    };
    wifi_transport_init(&wifi_cfg);

    // Initialize BLE beacon for iOS app discovery
    // Broadcasts SSID via BLE so iOS can find and connect to this dongle
    printf("[app:wifi2usb] Initializing BLE beacon...\n");
    if (ble_beacon_init(wifi_transport_get_ssid())) {
        ble_beacon_start();
        printf("[app:wifi2usb] BLE beacon advertising: %s\n", wifi_transport_get_ssid());
    } else {
        printf("[app:wifi2usb] WARNING: BLE beacon init failed\n");
    }

    printf("[app:wifi2usb] Initialization complete\n");
    printf("[app:wifi2usb]   Routing: WiFi -> USB Device (SInput)\n");
    printf("[app:wifi2usb]   Player slots: %d\n", MAX_PLAYER_SLOTS);
    printf("[app:wifi2usb] Button actions:\n");
    printf("[app:wifi2usb]   Click:        Enter pairing mode (broadcast SSID)\n");
    printf("[app:wifi2usb]   Double-click: Switch USB output mode\n");
    printf("[app:wifi2usb]   Triple-click: Reset to SInput mode\n");
    printf("[app:wifi2usb]   Hold:         Restart WiFi AP\n");
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

    // Process WiFi transport (CYW43 poll + JOCP packet handling)
    wifi_transport_task();

    // Process BLE beacon
    ble_beacon_task();

    // Update LED status
    led_status_update();

    // Route feedback from USB device output to WiFi controllers
    if (usbd_output_interface.get_feedback) {
        output_feedback_t fb;
        if (usbd_output_interface.get_feedback(&fb)) {
            // Send feedback to all connected JOCP controllers
            jocp_send_feedback_all(&fb);
        }
    }
}
