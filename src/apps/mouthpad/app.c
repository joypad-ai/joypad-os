// app.c - MouthPad App Entry Point
//
// BLE in (MouthPad HID + NUS) -> USB out (SInput composite + CDC relay).
// HID is routed through the JoypadOS pipeline; the NUS stream is bridged to
// the desktop utility over CDC by mp_bridge.

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
#include "bt/bthid/devices/vendors/augmental/mouthpad_ble.h"
#include "bt/mouthpad/mp_bridge.h"
#include "core/services/leds/leds.h"

#include "tusb.h"
#include "platform/platform.h"
#include <stdio.h>

#ifdef BTSTACK_USE_ESP32
#include "driver/gpio.h"
extern const bt_transport_t bt_transport_esp32;
#elif defined(BTSTACK_USE_NRF)
extern const bt_transport_t bt_transport_nrf;
#else
#include "pico/cyw43_arch.h"
extern const bt_transport_t bt_transport_cyw43;
#endif

// ============================================================================
// USB BUS SUSPEND / RESUME — drop the BT link so the MouthPad sleeps on host sleep
// ============================================================================
static volatile bool usb_bus_suspended = false;
static bool          usb_bus_suspended_seen = false;

void tud_suspend_cb(bool remote_wakeup_en) { (void)remote_wakeup_en; usb_bus_suspended = true; }
void tud_resume_cb(void) { usb_bus_suspended = false; }

static void usb_suspend_check(void)
{
    bool now = usb_bus_suspended;
    if (now == usb_bus_suspended_seen) return;
    usb_bus_suspended_seen = now;
    if (now) btstack_host_disconnect_all_devices();
}

void app_on_console_shutdown(void)
{
    btstack_host_disconnect_all_devices();
}

// ============================================================================
// LED STATUS — blink while searching, solid once a MouthPad is connected
// ============================================================================
static uint32_t led_last_toggle = 0;
static bool led_state = false;

static void platform_led_set(bool on)
{
#if defined(BTSTACK_USE_NRF)
    (void)on;  // RGB LED driven by ws2812_nrf.c / leds service
#elif defined(BTSTACK_USE_ESP32)
    (void)on;
#else
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on ? 1 : 0);
#endif
}

static void led_status_update(void)
{
    uint32_t now = platform_time_ms();
    if (btstack_classic_get_connection_count() > 0) {
        if (!led_state) { platform_led_set(true); led_state = true; }
    } else if (now - led_last_toggle >= 400) {
        led_state = !led_state;
        platform_led_set(led_state);
        led_last_toggle = now;
    }
}

// ============================================================================
// BUTTON — click: 60s scan;  hold: disconnect + clear bonds
// ============================================================================
static void on_button_event(button_event_t event)
{
    switch (event) {
        case BUTTON_EVENT_CLICK:
            printf("[app:mouthpad] Starting BT scan (60s)...\n");
            btstack_host_start_timed_scan(60000);
            break;
        case BUTTON_EVENT_HOLD:
            printf("[app:mouthpad] Disconnecting + clearing bonds...\n");
            btstack_host_disconnect_all_devices();
            btstack_host_delete_all_bonds();
            break;
        default:
            break;
    }
}

// ============================================================================
// APP INTERFACES
// ============================================================================
const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = 0;
    return NULL;  // BT transport submits input via bthid drivers
}

static const OutputInterface* output_interfaces[] = {
    &usbd_output_interface,
};

const OutputInterface** app_get_output_interfaces(uint8_t* count)
{
    *count = sizeof(output_interfaces) / sizeof(output_interfaces[0]);
    return output_interfaces;
}

// ============================================================================
// INIT
// ============================================================================
void app_init(void)
{
    printf("[app:mouthpad] Initializing MouthPad bridge v%s\n", JOYPAD_VERSION);

    button_init();
    button_set_callback(on_button_event);

    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_USB_DEVICE] = USB_OUTPUT_PORTS,
        },
        .merge_all_inputs = true,
        .transform_flags = TRANSFORM_FLAGS,
    };
    router_init(&router_cfg);
    router_add_route(INPUT_SOURCE_BLE_CENTRAL, OUTPUT_TARGET_USB_DEVICE, 0);

    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // mouthpad-relay is a PURE passthrough dongle: mouse + keyboard + NUS relay,
    // no gamepad translation. (Gamepad translation — pointing→stick etc. — stays
    // the driver default, so the MouthPad is a controller in every OTHER app.)
    mouthpad_ble_set_mode(MP_MODE_PASSTHROUGH);

    printf("[app:mouthpad] Initializing Bluetooth...\n");
#ifdef BTSTACK_USE_ESP32
    bt_init(&bt_transport_esp32);
#elif defined(BTSTACK_USE_NRF)
    bt_init(&bt_transport_nrf);
#else
    bt_init(&bt_transport_cyw43);
#endif

    // NUS <-> CDC relay is ambient: mp_bridge self-attaches to the CDC port via
    // cdc_init()'s relay hook (it's linked in), demuxing MouthPad relay frames
    // from JoypadOS commands. No explicit init needed here.

    printf("[app:mouthpad] Ready — SInput out (mouse+kbd+gamepad) + NUS relay over CDC\n");
}

// ============================================================================
// TASK
// ============================================================================
void app_task(void)
{
    usb_suspend_check();
    button_task();
    bt_task();

    // NUS <-> CDC relay runs inside cdc_task() (driven by the USB output
    // interface), so nothing to pump here.

    leds_set_connected_devices(btstack_classic_get_connection_count());
    led_status_update();

    // Route rumble/LED feedback from USB output back to BT (MouthPad has none,
    // but harmless for combined setups with other controllers).
    if (usbd_output_interface.get_feedback) {
        output_feedback_t fb;
        if (usbd_output_interface.get_feedback(&fb)) {
            for (int i = 0; i < playersCount; i++) {
                feedback_set_rumble(i, fb.rumble_left, fb.rumble_right);
            }
        }
    }
}
