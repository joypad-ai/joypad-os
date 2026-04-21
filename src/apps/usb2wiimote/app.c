// app.c - USB2WIIMOTE App Entry Point (Pico W)
// USB controllers -> Wiimote-over-BT-Classic output.

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/button/button.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "usb/usbh/usbh.h"
#include "bt/bt_output/wiimote/bt_output_wiimote.h"
#include "usb/usbd/usbd.h"
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
// LED STATUS
// ============================================================================

static uint32_t led_last_toggle = 0;
static bool led_state = false;

static void led_update(bool bt_connected) {
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (bt_connected) {
        if (!led_state) {
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
            led_state = true;
        }
    } else {
        if (now - led_last_toggle >= 400) {
            led_state = !led_state;
            cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, led_state ? 1 : 0);
            led_last_toggle = now;
        }
    }
}

// ============================================================================
// BUTTON (BOOTSEL on Pico W)
// ============================================================================

static void on_button_event(button_event_t event) {
    switch (event) {
        case BUTTON_EVENT_CLICK:
            printf("[app:usb2wiimote] Click — triggering Wii reconnect\n");
            // TODO: wiimote_sdp_reconnect() once a Wii addr is stored.
            break;
        case BUTTON_EVENT_DOUBLE_CLICK: {
            wiimote_ext_mode_t cur = bt_output_wiimote_get_ext();
            wiimote_ext_mode_t next = (cur + 1) % WIIMOTE_EXT_COUNT;
            printf("[app:usb2wiimote] Double-click — ext mode %d -> %d\n", cur, next);
            bt_output_wiimote_set_ext(next);
            break;
        }
        case BUTTON_EVENT_HOLD:
            printf("[app:usb2wiimote] Long press — clearing BT bonds\n");
            btstack_host_delete_all_bonds();
            break;
        default:
            break;
    }
}

// ============================================================================
// APP INTERFACES
// ============================================================================

static const InputInterface* input_interfaces[] = {
    &usbh_input_interface,
};

const InputInterface** app_get_input_interfaces(uint8_t* count) {
    *count = sizeof(input_interfaces) / sizeof(input_interfaces[0]);
    return input_interfaces;
}

static const OutputInterface* output_interfaces[] = {
    &bt_output_wiimote_interface,
    &usbd_output_interface,
};

const OutputInterface** app_get_output_interfaces(uint8_t* count) {
    *count = sizeof(output_interfaces) / sizeof(output_interfaces[0]);
    return output_interfaces;
}

// ============================================================================
// APP INIT / TASK
// ============================================================================

void app_init(void) {
    printf("[app:usb2wiimote] Initializing USB2WIIMOTE v%s\n", APP_VERSION);

    button_init();
    button_set_callback(on_button_event);

    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_WIIMOTE_BT] = 1,
            [OUTPUT_TARGET_USB_DEVICE] = 1,
        },
        .merge_all_inputs = true,
        .transform_flags = TRANSFORM_FLAGS,
    };
    router_init(&router_cfg);

    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_WIIMOTE_BT, 0);
    router_add_route(INPUT_SOURCE_USB_HOST, OUTPUT_TARGET_USB_DEVICE, 0);

    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // CYW43 BT must come up before bt_output_init does SDP registration.
    printf("[app:usb2wiimote] Initializing CYW43 Bluetooth...\n");
    bt_init(&bt_transport_cyw43);

    // Wiimote BT output registers HID + custom SDP + PIN handler with BTstack.
    // bt_output_wiimote_interface.init will be called by the OS wiring, but
    // we need BTstack up first — so main() / OS should order init correctly.
    // If the OS picks this up automatically via the OutputInterface vtable we
    // don't need to call it here.

    printf("[app:usb2wiimote] Routing: USB Host -> Wiimote (BT Classic) + USB Device (CDC)\n");
}

void app_task(void) {
    button_task();
    bt_task();

    bt_output_task();

    int devices = 0;
    for (uint8_t addr = 1; addr < MAX_DEVICES; addr++) {
        if (tuh_mounted(addr) && tuh_hid_instance_count(addr) > 0) {
            devices++;
        }
    }
    leds_set_connected_devices(devices);
    led_update(bt_output_is_connected());
}
