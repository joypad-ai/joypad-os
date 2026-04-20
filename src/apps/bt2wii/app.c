// app.c - BT2WII App Entry Point
// BT input → Wii extension-port I2C slave output on Pico W.

#include "app.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/button/button.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/device/wii_ext/wii_ext_device.h"
#include "usb/usbd/usbd.h"
#include "core/services/storage/flash.h"
#include "bt/transport/bt_transport.h"
#include "bt/btstack/btstack_host.h"

#include "pico/cyw43_arch.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "platform/platform.h"
#include <stdio.h>

extern const bt_transport_t bt_transport_cyw43;

// BT host enabled by default, togglable via web config
static bool bt_input_enabled = true;

// ============================================================================
// INPUT / OUTPUT REGISTRATION
// ============================================================================

#ifdef SENSOR_PAD
#include "pad/pad_input.h"
#include "pad/pad_config_flash.h"
#endif

static const InputInterface* input_interfaces[] = {
#ifdef SENSOR_PAD
    &pad_input_interface,
#endif
};

const InputInterface** app_get_input_interfaces(uint8_t* count) {
    *count = sizeof(input_interfaces) / sizeof(input_interfaces[0]);
    return input_interfaces;
}

static const OutputInterface* output_interfaces[] = {
    &wii_output_interface,
    &usbd_output_interface,  // CDC web config alongside Wii I2C output
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
        // Solid on when a controller is connected.
        if (!state) { led_set(true); state = true; }
    } else if (bt_input_enabled && btstack_host_is_scanning()) {
        // Slow blink while scanning.
        if (now - last_toggle >= 800) {
            state = !state;
            led_set(state);
            last_toggle = now;
        }
    } else {
        // Off — BT disabled or idle.
        if (state) { led_set(false); state = false; }
    }
}

// ============================================================================
// BUTTON (BOOTSEL) — same UX as bt2n64
// ============================================================================

static void on_button_event(button_event_t event)
{
    switch (event) {
        case BUTTON_EVENT_CLICK:
            printf("[app:bt2wiiext] Starting BT scan (60s)...\n");
            btstack_host_start_timed_scan(60000);
            break;
        case BUTTON_EVENT_HOLD:
            printf("[app:bt2wiiext] Disconnecting all devices + clearing bonds\n");
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
    printf("[app:bt2wiiext] Initializing BT2WII v%s\n", APP_VERSION);

    // Expose Wii output for web config (pin/mode config via OUTPUT.NATIVE.GET/SET)
    native_output = &wii_output_interface;

    // Determine emulation mode from flash (0=default/classic, 1=classic, 2=classic_pro, 3=nunchuck)
    wii_device_emulation_t emu = WII_DEV_EMULATE_CLASSIC;
    {
        flash_t flash_data;
        if (flash_load(&flash_data) && flash_data.wii_mode > 0) {
            emu = (wii_device_emulation_t)(flash_data.wii_mode - 1);
            if (emu > WII_DEV_EMULATE_NUNCHUCK) emu = WII_DEV_EMULATE_CLASSIC;
        }
    }

    // I2C slave up first so a plugged-in Wiimote gets a valid register
    // file immediately — even before the router and BT stack come up
    // the ID / calibration / neutral report bytes are already seeded.
    wii_device_init(emu);

    button_init();
    button_set_callback(on_button_event);

    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_WII_EXTENSION] = WII_OUTPUT_PORTS,
        },
        .merge_all_inputs = true,
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 0,
    };
    router_init(&router_cfg);
    router_add_route(INPUT_SOURCE_BLE_CENTRAL, OUTPUT_TARGET_WII_EXTENSION, 0);
    router_add_route(INPUT_SOURCE_BLE_CENTRAL, OUTPUT_TARGET_USB_DEVICE, 0);
#ifdef SENSOR_PAD
    router_add_route(INPUT_SOURCE_GPIO, OUTPUT_TARGET_WII_EXTENSION, 0);
    router_add_route(INPUT_SOURCE_GPIO, OUTPUT_TARGET_USB_DEVICE, 0);

    // Load pad config from flash (user configures pins via web config)
    const pad_device_config_t* pad_cfg = pad_config_load_runtime();
    if (pad_cfg) {
        pad_input_add_device(pad_cfg);
        printf("[app:bt2wiiext] Pad: %s\n", pad_cfg->name);
    }
#endif

    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Check flash for BT host toggle (default: enabled)
    {
        flash_t flash_data;
        if (flash_load(&flash_data) && flash_data.router_saved) {
            bt_input_enabled = flash_data.bt_input_enabled != 0;
        }
    }

    printf("[app:bt2wiiext] BT host: %s\n", bt_input_enabled ? "enabled" : "disabled");
    printf("[app:bt2wiiext] BT init deferred to first task tick\n");
    printf("[app:bt2wiiext]   Routing: Bluetooth -> Wii extension (0x52)\n");
    printf("[app:bt2wiiext]   Click BOOTSEL for 60s BT scan\n");
    printf("[app:bt2wiiext]   Hold BOOTSEL to disconnect all + clear bonds\n");
}

// ============================================================================
// TASK
// ============================================================================

void app_task(void)
{
    // Reboot-to-bootloader escape hatch on the CDC console.
    int c = getchar_timeout_us(0);
    if (c == 'B') reset_usb_boot(0, 0);

    // Deferred BT init runs once — CYW43 must always initialize (Pico W
    // needs it), but scanning only starts if BT host is enabled.
    static bool bt_initialized = false;
    if (!bt_initialized) {
        bt_initialized = true;
        printf("[app:bt2wiiext] Initializing CYW43...\n");
        if (!bt_input_enabled) {
            btstack_host_suppress_scan(true);
        }
        bt_init(&bt_transport_cyw43);
        printf("[app:bt2wiiext] BT host %s\n", bt_input_enabled ? "scanning" : "disabled");
    }

    button_task();
    bt_task();
    led_status_update();
}
