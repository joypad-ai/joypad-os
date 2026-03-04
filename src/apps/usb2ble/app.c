// app.c - USB2BLE App Entry Point
// USB controllers → BLE HID gamepad adapter (Pico W)
//
// Reads USB controllers via PIO-USB host, outputs as BLE gamepad
// using the BLE output interface (HOGP peripheral).

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/button/button.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "usb/usbh/usbh.h"
#include "bt/ble_output/ble_output.h"
#include "bt/transport/bt_transport.h"
#include "bt/btstack/btstack_host.h"
#include "core/services/leds/leds.h"
#include "core/buttons.h"

#include "pico/cyw43_arch.h"
extern const bt_transport_t bt_transport_cyw43;

#include "tusb.h"
#include "pico/stdlib.h"
#include <stdio.h>

// ============================================================================
// CYW43 LED STATUS
// ============================================================================

static uint32_t cyw43_led_last_toggle = 0;
static bool cyw43_led_state = false;

// Blink when idle (advertising), solid when BLE connected
static void cyw43_led_update(int devices)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (devices > 0) {
        if (!cyw43_led_state) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            cyw43_led_state = true;
        }
    } else {
        if (now - cyw43_led_last_toggle >= 400) {
            cyw43_led_state = !cyw43_led_state;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, cyw43_led_state ? 1 : 0);
            cyw43_led_last_toggle = now;
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
            printf("[app:usb2ble] Button click\n");
            break;

        case BUTTON_EVENT_HOLD:
            printf("[app:usb2ble] Long press - future: clear BLE bonds\n");
            break;

        default:
            break;
    }
}

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &usbh_input_interface,
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
    &ble_output_interface,
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
    printf("[app:usb2ble] Initializing USB2BLE v%s\n", APP_VERSION);

    // Initialize button service (BOOTSEL button on Pico W)
    button_init();
    button_set_callback(on_button_event);

    // Configure router
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_BLE_PERIPHERAL] = 1,
        },
        .merge_all_inputs = true,
        .transform_flags = TRANSFORM_FLAGS,
    };
    router_init(&router_cfg);

    // Route: USB Host → BLE Peripheral
    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_BLE_PERIPHERAL, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize CYW43 Bluetooth (must be before BLE output init)
    // CYW43 SPI claims PIO1 and DMA channels dynamically.
    // PIO-USB uses PIO0 and a high DMA channel (10) to avoid conflicts.
    printf("[app:usb2ble] Initializing CYW43 Bluetooth...\n");
    bt_init(&bt_transport_cyw43);

    printf("[app:usb2ble] Initialization complete\n");
    printf("[app:usb2ble]   Routing: USB Host → BLE Peripheral (Gamepad)\n");
    printf("[app:usb2ble]   Player slots: %d\n", MAX_PLAYER_SLOTS);
}

// ============================================================================
// APP TASK (Called from main loop)
// ============================================================================

void app_task(void)
{
    // Process button input
    button_task();

    // Process CYW43 Bluetooth transport
    bt_task();

    // Count connected USB devices for LED status
    int devices = 0;
    for (uint8_t addr = 1; addr < MAX_DEVICES; addr++) {
        if (tuh_mounted(addr) && tuh_hid_instance_count(addr) > 0) {
            devices++;
        }
    }
    leds_set_connected_devices(devices);

    // Update CYW43 onboard LED
    cyw43_led_update(devices);
}
