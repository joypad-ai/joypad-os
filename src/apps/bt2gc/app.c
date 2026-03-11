// app.c - BT2GC App Entry Point
// Bluetooth to GameCube console adapter for Pico W
//
// Uses Pico W's built-in CYW43 Bluetooth to receive controllers,
// outputs to GameCube via joybus PIO protocol.

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/services/profiles/profile.h"
#include "core/services/button/button.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/device/gamecube/gamecube_device.h"
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

static void platform_led_set(bool on)
{
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on ? 1 : 0);
}

// LED patterns:
//   Slow blink  (400ms): No BT device connected
//   Solid on:            BT device connected
static void led_status_update(void)
{
    uint32_t now = platform_time_ms();

    if (btstack_classic_get_connection_count() > 0) {
        if (!led_state) {
            platform_led_set(true);
            led_state = true;
        }
    } else {
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
            printf("[app:bt2gc] Starting BT scan (60s)...\n");
            btstack_host_start_timed_scan(60000);
            break;

        case BUTTON_EVENT_HOLD:
            printf("[app:bt2gc] Disconnecting all devices and clearing bonds...\n");
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
        [OUTPUT_TARGET_GAMECUBE] = &gc_profile_set,
    },
    .shared_profiles = NULL,
};

// ============================================================================
// APP INPUT INTERFACES
// ============================================================================

// BT2GC has no InputInterface - BT transport handles input internally
// via bthid drivers that call router_submit_input()

const InputInterface** app_get_input_interfaces(uint8_t* count)
{
    *count = 0;
    return NULL;
}

// ============================================================================
// APP OUTPUT INTERFACES
// ============================================================================

extern const OutputInterface gamecube_output_interface;

static const OutputInterface* output_interfaces[] = {
    &gamecube_output_interface,
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
    printf("[app:bt2gc] Initializing BT2GC v%s\n", APP_VERSION);
    printf("[app:bt2gc] Pico W built-in Bluetooth -> GameCube\n");

    // Initialize button service (uses BOOTSEL button on Pico W)
    button_init();
    button_set_callback(on_button_event);

    // Configure router for BT2GC
    router_config_t router_cfg = {
        .mode = ROUTING_MODE,
        .merge_mode = MERGE_MODE,
        .max_players_per_output = {
            [OUTPUT_TARGET_GAMECUBE] = GAMECUBE_OUTPUT_PORTS,
        },
        .merge_all_inputs = true,
        .transform_flags = TRANSFORM_FLAGS,
        .mouse_drain_rate = 8,
    };
    router_init(&router_cfg);

    // Add default route: BLE Central -> GameCube
    router_add_route(INPUT_SOURCE_BLE_CENTRAL, OUTPUT_TARGET_GAMECUBE, 0);

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize profile system with app-defined profiles
    profile_init(&app_profile_config);

    uint8_t profile_count = profile_get_count(OUTPUT_TARGET_GAMECUBE);
    const char* active_name = profile_get_name(OUTPUT_TARGET_GAMECUBE,
                                                profile_get_active_index(OUTPUT_TARGET_GAMECUBE));

    // Defer BT init to app_task — it takes ~1s and blocks console detection.
    // GC output + Core 1 joybus listener must start before BT init so the
    // console sees us during its boot probe window.
    printf("[app:bt2gc] BT init deferred (will start after joybus ready)\n");
    printf("[app:bt2gc]   Routing: Bluetooth -> GameCube (merge)\n");
    printf("[app:bt2gc]   Player slots: %d\n", MAX_PLAYER_SLOTS);
    printf("[app:bt2gc]   Profiles: %d (active: %s)\n", profile_count, active_name ? active_name : "none");
    printf("[app:bt2gc]   Click BOOTSEL for 60s BT scan\n");
    printf("[app:bt2gc]   Hold BOOTSEL to disconnect all + clear bonds\n");
}

// ============================================================================
// APP TASK (Called from main loop)
// ============================================================================

static bool bt_initialized = false;

void app_task(void)
{
    // Check for bootloader command on CDC serial ('B' = reboot to bootloader)
    int c = getchar_timeout_us(0);
    if (c == 'B') {
        reset_usb_boot(0, 0);
    }

    // Deferred BT init: runs once after joybus listener is active on Core 1
    if (!bt_initialized) {
        bt_initialized = true;
        printf("[app:bt2gc] Initializing Bluetooth...\n");
        bt_init(&bt_transport_cyw43);
        printf("[app:bt2gc] Bluetooth initialized\n");
    }

    // Forward rumble from GameCube console to BT controllers
    if (gamecube_output_interface.get_rumble) {
        uint8_t rumble = gamecube_output_interface.get_rumble();
        for (int i = 0; i < playersCount; i++) {
            feedback_set_rumble(i, rumble, rumble);
        }
    }

    // Process button input
    button_task();

    // Process Bluetooth transport
    bt_task();

    // Update LED status
    leds_set_connected_devices(btstack_classic_get_connection_count());
    led_status_update();
}
