// app.c - BT2NUON App Entry Point
// Bluetooth to Nuon DVD player adapter for Pico W
//
// Uses Pico W's built-in CYW43 Bluetooth to receive controllers,
// outputs to Nuon via Polyface PIO protocol.

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/services/profiles/profile.h"
#include "core/services/button/button.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/device/nuon/nuon_device.h"
#include "bt/transport/bt_transport.h"
#include "bt/btstack/btstack_host.h"
#include "core/services/leds/leds.h"

#include "pico/cyw43_arch.h"
#include "pico/bootrom.h"
#include "platform/platform.h"
#include <stdio.h>

extern const bt_transport_t bt_transport_cyw43;
extern int playersCount;

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
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on ? 1 : 0);
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
            // Enable scanning (deferred on bt2nuon to avoid SPI burst disrupting polyface)
            printf("[app:bt2nuon] Enabling BT scan...\n");
#ifdef BTSTACK_DEFER_SCAN
            extern void btstack_host_enable_scan(void);
            btstack_host_enable_scan();
#else
            btstack_host_start_timed_scan(60000);
#endif
            break;

        case BUTTON_EVENT_HOLD:
            // Long press to disconnect all devices and clear all bonds
            printf("[app:bt2nuon] Disconnecting all devices and clearing bonds...\n");
            btstack_host_disconnect_all_devices();
            btstack_host_delete_all_bonds();
            break;

        default:
            break;
    }
}

// ============================================================================
// APP PROFILE CONFIGURATION
// ============================================================================

static const profile_config_t app_profile_config = {
    .output_profiles = {
        [OUTPUT_TARGET_NUON] = &nuon_profile_set,
    },
    .shared_profiles = NULL,
};

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

// BT2NUON has no InputInterface - BT transport handles input internally
// via bthid drivers that call router_submit_input()

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = 0;
    return NULL;
}

// ============================================================================
// APP OUTPUT INTERFACES
// ============================================================================

extern const OutputInterface nuon_output_interface;

static const OutputInterface* output_interfaces[] = {
    &nuon_output_interface,
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
    printf("[app:bt2nuon] Initializing BT2NUON v%s\n", APP_VERSION);
    printf("[app:bt2nuon] Pico W built-in Bluetooth -> Nuon\n");

    // Initialize button service (uses BOOTSEL button on Pico W)
    button_init();
    button_set_callback(on_button_event);

    // Configure router for BT2NUON
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_NUON] = NUON_OUTPUT_PORTS,
        },
        .merge_all_inputs = true,  // Merge all BT inputs to single output
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 8,
    };
    router_init(&router_cfg);

    // Add default route: BLE Central -> Nuon
    router_add_route(INPUT_SOURCE_BLE_CENTRAL, OUTPUT_TARGET_NUON, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize profile system with app-defined profiles
    profile_init(&app_profile_config);

    uint8_t profile_count = profile_get_count(OUTPUT_TARGET_NUON);
    const char* active_name = profile_get_name(OUTPUT_TARGET_NUON,
                                                profile_get_active_index(OUTPUT_TARGET_NUON));

    // Defer BT init to app_task — it takes ~1s and blocks console detection.
    // Nuon output + Core 1 polyface listener must start before BT init so the
    // console sees us during its boot probe window.
    printf("[app:bt2nuon] BT init deferred (will start after polyface ready)\n");

    printf("[app:bt2nuon] Initialization complete\n");
    printf("[app:bt2nuon]   Routing: Bluetooth -> Nuon (merge)\n");
    printf("[app:bt2nuon]   Player slots: %d (single player)\n", MAX_PLAYER_SLOTS);
    printf("[app:bt2nuon]   Spinner support: enabled (right stick -> spinner)\n");
    printf("[app:bt2nuon]   Soft reset: enabled (button combo)\n");
    printf("[app:bt2nuon]   Profiles: %d (active: %s)\n", profile_count, active_name ? active_name : "none");
    printf("[app:bt2nuon]   Click BOOTSEL for 60s BT scan\n");
    printf("[app:bt2nuon]   Hold BOOTSEL to disconnect all + clear bonds\n");
}

// ============================================================================
// APP TASK (Called from main loop)
// ============================================================================

// Deferred BT initialization state
static bool bt_initialized = false;

void app_task(void)
{
    // Check for bootloader command on UART ('B' = reboot to bootloader)
    // Must be before BT init delay so it works immediately after flash.
    int c = getchar_timeout_us(0);
    if (c == 'B') {
        reset_usb_boot(0, 0);
    }

    // Deferred BT init: wait 10 seconds for polyface handshake to complete
    if (!bt_initialized) {
        static uint32_t boot_time = 0;
        if (boot_time == 0) boot_time = to_ms_since_boot(get_absolute_time());
        if (to_ms_since_boot(get_absolute_time()) - boot_time > 10000) {
            bt_initialized = true;
            printf("[app:bt2nuon] Initializing Bluetooth...\n");
            bt_init(&bt_transport_cyw43);
            printf("[app:bt2nuon] Bluetooth initialized\n");
        }
        return;
    }

    // Process button input (BOOTSEL reads are throttled in button_task)
    button_task();

    // BT poll + LED
    bt_task();
    leds_set_connected_devices(btstack_classic_get_connection_count());
    led_status_update();
}
