// app.c - BT2WII App Entry Point
// BT input → Wii extension-port I2C slave output on Pico W.

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/button/button.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/device/wii/wii_device.h"
#include "bt/transport/bt_transport.h"
#include "bt/btstack/btstack_host.h"

#include "pico/cyw43_arch.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "platform/platform.h"
#include <stdio.h>

extern const bt_transport_t bt_transport_cyw43;

// CDC stubs (bt2wii has no USB device stack).
void cdc_commands_send_input_event(uint32_t b, const uint8_t* a) { (void)b; (void)a; }
void cdc_commands_send_output_event(uint32_t b, const uint8_t* a) { (void)b; (void)a; }
void cdc_commands_send_connect_event(uint8_t p, const char* n, uint16_t v, uint16_t pid) {
    (void)p; (void)n; (void)v; (void)pid;
}
void cdc_commands_send_disconnect_event(uint8_t p) { (void)p; }

// ============================================================================
// INPUT / OUTPUT REGISTRATION
// ============================================================================

// BT transport submits to the router directly — no InputInterface needed.
const InputInterface** app_get_input_interfaces(uint8_t* count) {
    *count = 0;
    return NULL;
}

static const OutputInterface* output_interfaces[] = {
    &wii_output_interface,
};

const OutputInterface** app_get_output_interfaces(uint8_t* count) {
    *count = sizeof(output_interfaces) / sizeof(output_interfaces[0]);
    return output_interfaces;
}

// ============================================================================
// LED (Pico W CYW43 onboard LED)
// ============================================================================

static void led_set(bool on) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on ? 1 : 0);
}

static void led_status_update(void)
{
    static uint32_t last_toggle = 0;
    static bool state = false;
    uint32_t now = platform_time_ms();
    bool bt_connected = btstack_classic_get_connection_count() > 0;

    if (bt_connected) {
        // Solid on when a controller is paired + feeding events.
        if (!state) { led_set(true); state = true; }
    } else {
        // Slow blink while scanning / idle.
        if (now - last_toggle >= 800) {
            state = !state;
            led_set(state);
            last_toggle = now;
        }
    }
}

// ============================================================================
// BUTTON (BOOTSEL) — same UX as bt2n64
// ============================================================================

static void on_button_event(button_event_t event)
{
    switch (event) {
        case BUTTON_EVENT_CLICK:
            printf("[app:bt2wii] Starting BT scan (60s)...\n");
            btstack_host_start_timed_scan(60000);
            break;
        case BUTTON_EVENT_HOLD:
            printf("[app:bt2wii] Disconnecting all devices + clearing bonds\n");
            btstack_host_disconnect_all_devices();
            btstack_host_delete_all_bonds();
            break;
        default: break;
    }
}

// ============================================================================
// INIT
// ============================================================================

void app_init(void)
{
    printf("[app:bt2wii] Initializing BT2WII v%s\n", APP_VERSION);

    // I2C slave up first so a plugged-in Wiimote gets a valid register
    // file immediately — even before the router and BT stack come up
    // the ID / calibration / neutral report bytes are already seeded.
    wii_device_init(WII_DEV_EMULATE_CLASSIC);

    button_init();
    button_set_callback(on_button_event);

    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_WII] = WII_OUTPUT_PORTS,
        },
        .merge_all_inputs = true,
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 0,
    };
    router_init(&router_cfg);
    router_add_route(INPUT_SOURCE_BLE_CENTRAL, OUTPUT_TARGET_WII, 0);

    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    printf("[app:bt2wii] BT init deferred to first task tick\n");
    printf("[app:bt2wii]   Routing: Bluetooth -> Wii extension (0x52)\n");
    printf("[app:bt2wii]   Click BOOTSEL for 60s BT scan\n");
    printf("[app:bt2wii]   Hold BOOTSEL to disconnect all + clear bonds\n");
}

// ============================================================================
// TASK
// ============================================================================

void app_task(void)
{
    // Reboot-to-bootloader escape hatch on the CDC console.
    int c = getchar_timeout_us(0);
    if (c == 'B') reset_usb_boot(0, 0);

    // Deferred BT init runs once — waits one tick after the I2C slave is
    // quiescent so the CYW43 bring-up doesn't stall the slave ISR.
    static bool bt_initialized = false;
    if (!bt_initialized) {
        bt_initialized = true;
        printf("[app:bt2wii] Initializing Bluetooth...\n");
        bt_init(&bt_transport_cyw43);
        printf("[app:bt2wii] Bluetooth initialized\n");
    }

    button_task();
    bt_task();
    led_status_update();
}
