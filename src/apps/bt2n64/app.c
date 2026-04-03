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
#include "pico/bootrom.h"
#include "platform/platform.h"
#include <stdio.h>

extern const bt_transport_t bt_transport_cyw43;
extern int playersCount;

// Stubs for CDC commands (bt2n64 has no USB device stack)
void cdc_commands_send_input_event(uint32_t buttons, const uint8_t* axes) { (void)buttons; (void)axes; }
void cdc_commands_send_output_event(uint32_t buttons, const uint8_t* axes) { (void)buttons; (void)axes; }
void cdc_commands_send_connect_event(uint8_t port, const char* name, uint16_t vid, uint16_t pid) { (void)port; (void)name; (void)vid; (void)pid; }
void cdc_commands_send_disconnect_event(uint8_t port) { (void)port; }

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
extern volatile uint32_t n64_diag_poll_count;
extern volatile uint8_t n64_diag_last_cmd;
extern volatile uint32_t n64_diag_probe_count;
extern volatile uint32_t n64_diag_rx_count;
extern volatile uint32_t n64_diag_pak_read_count;
extern volatile uint32_t n64_diag_pak_write_count;
extern volatile uint16_t n64_diag_last_read_addr;
extern volatile uint16_t n64_diag_last_write_addr;
extern volatile uint8_t n64_diag_write_crc;
extern volatile uint8_t n64_diag_write_data[4];
extern volatile uint8_t n64_diag_last_rx;
extern volatile uint8_t n64_diag_phase;
extern volatile uint32_t n64_diag_listen_time_us;
extern volatile uint32_t n64_diag_first_rx_us;
extern volatile uint8_t n64_boot_cmds[];
extern volatile uint8_t n64_boot_cmd_count;
extern volatile uint16_t n64_diag_first_write_addr;
extern volatile uint8_t n64_diag_first_write_crc;
extern volatile uint8_t n64_diag_first_write_data[4];
extern volatile uint16_t n64_diag_first_read_addr;

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
    // N64 late init: flash, profiles, GPIO — deferred from n64_init() so Core 1
    // could start listening for console probes as early as possible.
    n64_late_init();

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

    // Defer BT init to app_task — it takes ~1s and blocks console detection.
    // N64 output + Core 1 joybus listener must start before BT init so the
    // console sees us during its boot probe window.
    printf("[app:bt2n64] BT init deferred (will start after joybus ready)\n");
    printf("[app:bt2n64]   Routing: Bluetooth -> N64 (merge)\n");
    printf("[app:bt2n64]   Player slots: %d (single player)\n", MAX_PLAYER_SLOTS);
    printf("[app:bt2n64]   Profiles: %d (active: %s)\n", profile_count, active_name ? active_name : "none");
    printf("[app:bt2n64]   Click BOOTSEL for 60s BT scan\n");
    printf("[app:bt2n64]   Hold BOOTSEL to disconnect all + clear bonds\n");
}

// ============================================================================
// APP TASK (Called from main loop)
// ============================================================================

// Deferred BT initialization state
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
        printf("[app:bt2n64] Initializing Bluetooth...\n");
        bt_init(&bt_transport_cyw43);
        printf("[app:bt2n64] Bluetooth initialized\n");
    }

    // Forward rumble from N64 console to BT controllers
    // N64 rumble pak is binary (on/off) with a single weak motor.
    // Use moderate intensity on left (heavy) motor only to approximate the feel.
    if (n64_output_interface.get_rumble) {
        uint8_t rumble = n64_output_interface.get_rumble();
        uint8_t left = rumble ? 128 : 0;   // Heavy motor at ~50%
        uint8_t right = rumble ? 40 : 0;   // Light motor gentle
        for (int i = 0; i < playersCount; i++) {
            feedback_set_rumble(i, left, right);
        }
    }

    // Process button input
    button_task();

    // Process Bluetooth transport
    bt_task();

    // Periodic diagnostic (every ~2 seconds)
    static uint32_t diag_last = 0;
    uint32_t now = platform_time_ms();
    if (now - diag_last >= 2000) {
        diag_last = now;

        // Count GPIO transitions over 20ms (N64 polls at 60Hz, one poll every ~16.6ms)
        int edges = 0;
        bool last_pin = gpio_get(N64_DATA_PIN);
        absolute_time_t end = make_timeout_time_ms(20);
        while (!time_reached(end)) {
            bool cur = gpio_get(N64_DATA_PIN);
            if (cur != last_pin) {
                edges++;
                last_pin = cur;
            }
        }

        // Print boot command sequence when it changes
        static uint8_t last_boot_count = 0;
        if (n64_boot_cmd_count > 0 && n64_boot_cmd_count != last_boot_count) {
            last_boot_count = n64_boot_cmd_count;
            printf("[diag] Boot cmds (%d):", n64_boot_cmd_count);
            for (int i = 0; i < n64_boot_cmd_count; i++) {
                printf(" %02x", n64_boot_cmds[i]);
            }
            printf("\n");
            printf("[diag] 1st rd=0x%04x 1st wr=0x%04x 1stWrCrc=0x%02x 1stWrD=%02x%02x%02x%02x\n",
                   n64_diag_first_read_addr, n64_diag_first_write_addr,
                   n64_diag_first_write_crc,
                   n64_diag_first_write_data[0], n64_diag_first_write_data[1],
                   n64_diag_first_write_data[2], n64_diag_first_write_data[3]);
        }

        extern n64_report_t n64_report;
        extern volatile bool n64_router_has_data;
        extern volatile bool n64_player_assigned;
        uint8_t *rpt = (uint8_t *)&n64_report;
        // Also grab raw router event to trace button mapping
        const input_event_t* evt = router_get_output(OUTPUT_TARGET_N64, 0);
        uint32_t raw_btn = evt ? evt->buttons : 0;
        printf("[diag] rpt=%02x%02x sx=%d sy=%d btn=0x%08lx data=%d plyr=%d pc=%d\n",
               rpt[0], rpt[1], n64_report.stick_x, n64_report.stick_y,
               (unsigned long)raw_btn,
               (int)n64_router_has_data, (int)n64_player_assigned, playersCount);
        printf("[diag] rx=%lu probe=%lu poll=%lu rd=%lu wr=%lu rdA=0x%04x wrA=0x%04x crc=0x%02x d=%02x%02x%02x%02x edges=%d\n",
               (unsigned long)n64_diag_rx_count,
               (unsigned long)n64_diag_probe_count,
               (unsigned long)n64_diag_poll_count,
               (unsigned long)n64_diag_pak_read_count,
               (unsigned long)n64_diag_pak_write_count,
               n64_diag_last_read_addr,
               n64_diag_last_write_addr,
               n64_diag_write_crc,
               n64_diag_write_data[0], n64_diag_write_data[1],
               n64_diag_write_data[2], n64_diag_write_data[3],
               edges);
        extern volatile uint32_t n64_diag_total_reads, n64_diag_total_writes;
        extern volatile uint32_t n64_diag_total_probes, n64_diag_total_unknown;
        extern volatile uint32_t n64_diag_read_addr_fail, n64_diag_write_buf_fail;
        printf("[diag] TOTAL: probes=%lu rd=%lu wr=%lu unk=%lu(0x%02x) rdFail=%lu wrFail=%lu\n",
               (unsigned long)n64_diag_total_probes,
               (unsigned long)n64_diag_total_reads,
               (unsigned long)n64_diag_total_writes,
               (unsigned long)n64_diag_total_unknown,
               n64_diag_last_cmd,
               (unsigned long)n64_diag_read_addr_fail,
               (unsigned long)n64_diag_write_buf_fail);
    }

    // Update LED status
    leds_set_connected_devices(btstack_classic_get_connection_count());
    led_status_update();
}
