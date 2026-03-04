// app.c - BT2N64 App Entry Point
// Bluetooth to N64 console adapter for Pico W
//
// Uses Pico W's built-in CYW43 Bluetooth to receive controllers,
// outputs to N64 via joybus PIO protocol.

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/services/profiles/profile.h"
#include "core/services/button/button.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/device/n64/n64_device.h"
#include "bt/transport/bt_transport.h"
#include "bt/btstack/btstack_host.h"
#include "core/services/leds/leds.h"

#include "pico/cyw43_arch.h"
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
// - Fast blink (200ms): N64 console not communicating (check wiring)
// - Slow blink (800ms): N64 OK but no BT device connected
// - Solid on: N64 OK and BT device connected
static void platform_led_set(bool on)
{
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on ? 1 : 0);
}

// Diagnostic: set by Core 1 when N64 console is communicating
extern volatile bool n64_console_active;
extern volatile bool n64_router_has_data;
extern volatile bool n64_player_assigned;

// LED patterns:
//   Fast blink  (100ms): N64 console not communicating
//   Slow blink  (400ms): N64 OK but no BT device connected
//   Solid on:            N64 OK + BT connected + data flowing
//   Very fast   (50ms):  N64 OK + BT connected but NO data flowing
//                        (router or player issue)
static void led_status_update(void)
{
    uint32_t now = platform_time_ms();

    if (!n64_console_active) {
        // N64 console not communicating - fast blink
        if (now - led_last_toggle >= 100) {
            led_state = !led_state;
            platform_led_set(led_state);
            led_last_toggle = now;
        }
    } else if (btstack_classic_get_connection_count() > 0) {
        if (n64_player_assigned && n64_router_has_data) {
            // N64 OK + BT connected + data flowing - solid on
            if (!led_state) {
                platform_led_set(true);
                led_state = true;
            }
        } else {
            // N64 OK + BT connected but NO data path - very fast blink
            if (now - led_last_toggle >= 50) {
                led_state = !led_state;
                platform_led_set(led_state);
                led_last_toggle = now;
            }
        }
    } else {
        // N64 OK but no BT device - slow blink
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
            printf("[app:bt2n64] Starting BT scan (60s)...\n");
            btstack_host_start_timed_scan(60000);
            break;

        case BUTTON_EVENT_HOLD:
            // Long press to disconnect all devices and clear all bonds
            printf("[app:bt2n64] Disconnecting all devices and clearing bonds...\n");
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
        [OUTPUT_TARGET_N64] = &n64_profile_set,
    },
    .shared_profiles = NULL,
};

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

// BT2N64 has no InputInterface - BT transport handles input internally
// via bthid drivers that call router_submit_input()

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = 0;
    return NULL;
}

// ============================================================================
// APP OUTPUT INTERFACES
// ============================================================================

extern const OutputInterface n64_output_interface;

static const OutputInterface* output_interfaces[] = {
    &n64_output_interface,
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
    printf("[app:bt2n64] Initializing BT2N64 v%s\n", APP_VERSION);
    printf("[app:bt2n64] Pico W built-in Bluetooth -> N64\n");

    // Initialize button service (uses BOOTSEL button on Pico W)
    button_init();
    button_set_callback(on_button_event);

    // Configure router for BT2N64
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_N64] = N64_OUTPUT_PORTS,
        },
        .merge_all_inputs = true,  // Merge all BT inputs to single output
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 8,
    };
    router_init(&router_cfg);

    // Add default route: BLE Central -> N64
    router_add_route(INPUT_SOURCE_BLE_CENTRAL, OUTPUT_TARGET_N64, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize profile system with app-defined profiles
    profile_init(&app_profile_config);

    uint8_t profile_count = profile_get_count(OUTPUT_TARGET_N64);
    const char* active_name = profile_get_name(OUTPUT_TARGET_N64,
                                                profile_get_active_index(OUTPUT_TARGET_N64));

    // Initialize Bluetooth transport
    printf("[app:bt2n64] Initializing Bluetooth...\n");
    bt_init(&bt_transport_cyw43);

    printf("[app:bt2n64] Initialization complete\n");
    printf("[app:bt2n64]   Routing: Bluetooth -> N64 (merge)\n");
    printf("[app:bt2n64]   Player slots: %d (single player)\n", MAX_PLAYER_SLOTS);
    printf("[app:bt2n64]   Profiles: %d (active: %s)\n", profile_count, active_name ? active_name : "none");
    printf("[app:bt2n64]   Click BOOTSEL for 60s BT scan\n");
    printf("[app:bt2n64]   Hold BOOTSEL to disconnect all + clear bonds\n");
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

    // Periodic diagnostic (every ~5 seconds)
    static uint32_t diag_last = 0;
    uint32_t now = platform_time_ms();
    if (now - diag_last >= 5000) {
        diag_last = now;
        printf("[diag] n64_active=%d bt_conns=%d players=%d router_data=%d player_assigned=%d\n",
               n64_console_active ? 1 : 0,
               btstack_classic_get_connection_count(),
               playersCount,
               n64_router_has_data ? 1 : 0,
               n64_player_assigned ? 1 : 0);
    }

    // Update LED status
    leds_set_connected_devices(btstack_classic_get_connection_count());
    led_status_update();
}
