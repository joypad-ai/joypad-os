// app.c - BT2GC App Entry Point
// Bluetooth to GameCube console adapter for Pico W, with USB output fallback.
//
// Uses Pico W's built-in CYW43 Bluetooth to receive controllers. At boot it
// probes the joybus data pin: a powered GameCube holds it HIGH -> GameCube
// output (joybus PIO). No console -> USB device output, which defaults to CDC
// (web config) but is toggleable to Switch/SInput/XInput/etc. via the usbd
// mode system (double-click the button), just like bt2usb. The user runs the
// single adapter on a GameCube or over USB (e.g. Switch) — generally one or
// the other, selected automatically by what it's plugged into.

#include "app.h"
#include "profiles.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/services/profiles/profile.h"
#include "core/services/button/button.h"
#include "core/services/storage/flash.h"
#include "core/input_interface.h"
#include "core/output_interface.h"
#include "native/device/gamecube/gamecube_device.h"
#include "usb/usbd/usbd.h"
#include "bt/transport/bt_transport.h"
#include "bt/btstack/btstack_host.h"
#include "core/services/leds/leds.h"

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "platform/platform.h"
#include "tusb.h"
#include <stdio.h>

extern const bt_transport_t bt_transport_cyw43;
extern int playersCount;

// true = no GameCube console detected at boot -> USB device output mode.
static bool g_usb_mode = false;

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

        case BUTTON_EVENT_DOUBLE_CLICK:
            // Cycle USB output mode (CDC -> SInput -> Switch -> ...). Only
            // meaningful in USB mode; on a GameCube the USB device is inactive.
            if (g_usb_mode) {
                tud_task_ext(1, false);
                platform_sleep_ms(50);
                tud_task_ext(1, false);
                usb_output_mode_t next = usbd_get_next_mode();
                printf("[app:bt2gc] USB output mode -> %s\n", usbd_get_mode_name(next));
                usbd_set_mode(next);
            }
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

static const OutputInterface* gc_outputs[]  = { &gamecube_output_interface };
static const OutputInterface* usb_outputs[] = { &usbd_output_interface };

const OutputInterface** app_get_output_interfaces(uint8_t* count)
{
    // Console detect via the GC_DATA joybus pin (same approach as wii2gc):
    // a powered GameCube's pull-up (~1kΩ to 3.3V) holds the line HIGH; our
    // pull-down (~50kΩ) dominates when no console is present → LOW → USB mode.
    // Honour a runtime pin override from web config.
    flash_init();  // idempotent — needed early to read pin override
    flash_t* settings = flash_get_settings();
    uint detect_pin = GC_DATA_PIN;
    if (settings && settings->joybus_data_pin > 0 && settings->joybus_data_pin <= 28) {
        detect_pin = settings->joybus_data_pin;
    }

    gpio_init(detect_pin);
    gpio_set_dir(detect_pin, GPIO_IN);
    gpio_pull_down(detect_pin);
    sleep_ms(200);

    if (!gpio_get(detect_pin)) {
        g_usb_mode = true;
        *count = sizeof(usb_outputs) / sizeof(usb_outputs[0]);
        return usb_outputs;
    }

    g_usb_mode = false;
    *count = sizeof(gc_outputs) / sizeof(gc_outputs[0]);
    return gc_outputs;
}

// ============================================================================
// APP INITIALIZATION
// ============================================================================

void app_init(void)
{
    // Expose GC output for web config (joybus pin settings) in both modes.
    native_output = &gamecube_output_interface;

    printf("[app:bt2gc] Initializing BT2GC v%s (%s output)\n",
           JOYPAD_VERSION, g_usb_mode ? "USB" : "GameCube");

    // Initialize button service (uses BOOTSEL button on Pico W)
    button_init();
    button_set_callback(on_button_event);

    if (g_usb_mode) {
        router_config_t router_cfg = {
            .mode = ROUTING_MODE,
            .merge_mode = MERGE_MODE,
            .max_players_per_output = {
                [OUTPUT_TARGET_USB_DEVICE] = 1,
            },
            .merge_all_inputs = true,
            .transform_flags = TRANSFORM_FLAGS,
            .mouse_drain_rate = 8,
        };
        router_init(&router_cfg);
        router_add_route(INPUT_SOURCE_BLE_CENTRAL, OUTPUT_TARGET_USB_DEVICE, 0);
    } else {
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
        router_add_route(INPUT_SOURCE_BLE_CENTRAL, OUTPUT_TARGET_GAMECUBE, 0);
    }

    // Configure player management
    player_config_t player_cfg = {
        .slot_mode = PLAYER_SLOT_MODE,
        .max_slots = MAX_PLAYER_SLOTS,
        .auto_assign_on_press = AUTO_ASSIGN_ON_PRESS,
    };
    players_init_with_config(&player_cfg);

    // Initialize profile system with app-defined profiles
    profile_init(&app_profile_config);

    // Defer BT init to app_task — it takes ~1s and would block console
    // detection / USB enumeration; outputs must start first.
    if (g_usb_mode) {
        printf("[app:bt2gc]   No GC console -> USB output, mode: %s (double-click to cycle)\n",
               usbd_get_mode_name(usbd_get_mode()));
    } else {
        printf("[app:bt2gc]   GC console detected -> GameCube output (merge)\n");
    }
    printf("[app:bt2gc]   BT init deferred (will start after output ready)\n");
    printf("[app:bt2gc]   Click BOOTSEL for 60s BT scan; hold to clear bonds\n");
}

// ============================================================================
// APP TASK (Called from main loop)
// ============================================================================

static bool bt_initialized = false;

void app_task(void)
{
    // Check for bootloader command on UART serial ('B' = reboot to bootloader)
    int c = getchar_timeout_us(0);
    if (c == 'B') {
        reset_usb_boot(0, 0);
    }

    // Deferred BT init: runs once after the output is active.
    if (!bt_initialized) {
        bt_initialized = true;
        printf("[app:bt2gc] Initializing Bluetooth...\n");
        bt_init(&bt_transport_cyw43);
        printf("[app:bt2gc] Bluetooth initialized\n");
    }

    if (g_usb_mode) {
        // Forward rumble/LED feedback from the USB host to BT controllers.
        if (usbd_output_interface.get_feedback) {
            output_feedback_t fb;
            if (usbd_output_interface.get_feedback(&fb)) {
                for (int i = 0; i < playersCount; i++) {
                    feedback_set_rumble(i, fb.rumble_left, fb.rumble_right);
                }
            }
        }
    } else {
        // Forward rumble from the GameCube console to BT controllers.
        if (gamecube_output_interface.get_rumble) {
            uint8_t rumble = gamecube_output_interface.get_rumble();
            for (int i = 0; i < playersCount; i++) {
                feedback_set_rumble(i, rumble, rumble);
            }
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
