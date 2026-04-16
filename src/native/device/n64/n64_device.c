// n64_device.c - N64 Output Device
//
// Outputs controller data to N64 via joybus protocol.
// Uses the universal profile system for button remapping.
// Follows the same pattern as gamecube_device.c.

#include "n64_device.h"
#include "n64_buttons.h"
#include "N64Console.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "tusb.h"
#include "core/services/storage/flash.h"
#include "core/services/profiles/profile.h"
#include "core/services/players/manager.h"
#include "core/services/codes/codes.h"
#include "core/router/router.h"
#include "platform/platform.h"

// Defined in N64Console.c
extern n64_report_t default_n64_report;
extern n64_status_t default_n64_status;
extern volatile uint8_t n64_rumble_state;

// Declaration of global variables
N64Console_t n64;
n64_report_t n64_report;
PIO pio = pio0;

// Diagnostic: set by Core 1 when N64 console is communicating
volatile bool n64_console_active = false;

// Diagnostic: data path status (set by Core 1, read by Core 0 for LED)
volatile bool n64_router_has_data = false;  // router_get_output returned non-NULL at least once
volatile bool n64_player_assigned = false;  // playersCount > 0 seen in update_output

static uint8_t n64_get_rumble(void) { return n64_rumble_state; }

// ============================================================================
// PROFILE SYSTEM ACCESSORS (for OutputInterface)
// ============================================================================

static uint8_t n64_get_player_count_for_profile(void) {
    return router_get_player_count(OUTPUT_TARGET_N64);
}

static uint8_t n64_get_profile_count(void) {
    return profile_get_count(OUTPUT_TARGET_N64);
}

static uint8_t n64_get_active_profile_index(void) {
    return profile_get_active_index(OUTPUT_TARGET_N64);
}

static void n64_set_active_profile(uint8_t index) {
    profile_set_active(OUTPUT_TARGET_N64, index);
}

static const char* n64_get_profile_name(uint8_t index) {
    return profile_get_name(OUTPUT_TARGET_N64, index);
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void n64_init()
{
    // ONLY PIO/joybus setup here — nothing else!
    // Core 1 is signaled immediately after this returns, so every μs counts.
    // Console probes for controllers at boot and may stop if no response.
    // Non-PIO init (flash, profiles, GPIO) deferred to n64_late_init().
    int sm = -1;
    int offset = -1;

    N64Console_init(&n64, N64_DATA_PIN, pio, sm, offset);
    n64_report = default_n64_report;
}

// Deferred init: called after Core 1 is already listening for console probes.
// Safe to do slow operations (flash, printf, GPIO) here.
void n64_late_init()
{
    printf("[n64] Joybus on PIO%d GP%d SM=%d (ready)\n",
           pio == pio0 ? 0 : 1, N64_DATA_PIN, n64._port.sm);

    // Configure custom UART pins (only for boards with dedicated UART)
    #ifdef UART_TX_PIN
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    #endif

    // Initialize flash settings system
    flash_init();

    // Profile system is initialized by app
    profile_set_player_count_callback(n64_get_player_count_for_profile);

    // KB2040-specific hardware setup (shield pins, 3V3 detect, BOOTSEL)
    #ifdef CONFIG_N64
    // Ground GPIO attached to shielding
    gpio_init(SHIELD_PIN_L);
    gpio_set_dir(SHIELD_PIN_L, GPIO_OUT);
    gpio_init(SHIELD_PIN_L + 1);
    gpio_set_dir(SHIELD_PIN_L + 1, GPIO_OUT);
    gpio_init(SHIELD_PIN_R);
    gpio_set_dir(SHIELD_PIN_R, GPIO_OUT);
    gpio_init(SHIELD_PIN_R + 1);
    gpio_set_dir(SHIELD_PIN_R + 1, GPIO_OUT);

    gpio_put(SHIELD_PIN_L, 0);
    gpio_put(SHIELD_PIN_L + 1, 0);
    gpio_put(SHIELD_PIN_R, 0);
    gpio_put(SHIELD_PIN_R + 1, 0);

    // Initialize the BOOTSEL_PIN as input
    gpio_init(BOOTSEL_PIN);
    gpio_set_dir(BOOTSEL_PIN, GPIO_IN);
    gpio_pull_up(BOOTSEL_PIN);

    // Reboot into bootsel mode if N64 3.3V not detected
    gpio_init(N64_3V3_PIN);
    gpio_set_dir(N64_3V3_PIN, GPIO_IN);
    gpio_pull_down(N64_3V3_PIN);

    sleep_ms(200);
    if (!gpio_get(N64_3V3_PIN)) reset_usb_boot(0, 0);
    #endif

    const profile_t* profile = profile_get_active(OUTPUT_TARGET_N64);
    if (profile) {
        printf("[n64] Active profile: %s\n", profile->name);
    }
}

// ============================================================================
// CORE 1 TASK (Timing-Critical)
// ============================================================================

// Core 1: timing-critical joybus protocol only.
// WaitForPoll handles PROBE/RESET/READ/WRITE internally, returns on POLL.
// No SM cleanup between sends/receives — PIO transitions write→read via jmp.
//
// IMPORTANT: On RP2350 (Pico 2 W), Core 0's CYW43 periodically locks flash.
// Core 1 must NOT call flash-resident functions after CYW43 init.
// update_output() (which calls router/profile functions in flash) runs on Core 0
// via n64_task() instead.
void __not_in_flash_func(core1_task)(void)
{
    // No printf here — Core 1 starts before stdio_init_all() for fastest boot.
    //
    // WaitForPoll is entirely RAM-safe (no flash-resident calls).
    // Cannot use N64Console_Detect here — it calls busy_wait_us/joybus_send_bytes
    // (flash-resident), and Core 0's CYW43 may lock flash at any time after boot.
    // WaitForPoll's n64_send_bytes replicates joybus_send_bytes behavior using
    // only inline PIO functions (pio_sm_set_config, pio_sm_restart, etc.).
    while (1) {
        N64Console_WaitForPoll(&n64);
    }
}

// Core 0 task: update n64_report from router/profile system.
// Must run on Core 0 because router_get_output, profile_apply, etc. are in flash.
static void n64_task(void)
{
    if (n64_console_active) {
        update_output();
    }
}

// ============================================================================
// USBR -> N64 BUTTON MAPPING
// ============================================================================

// Convert unsigned 8-bit analog (0-255, 128=center) to N64 signed (-128 to +127)
static inline int8_t analog_u8_to_n64(uint8_t val) {
    // HID: 0=up/left, 128=center, 255=down/right
    // N64: -128=left/down, 0=center, +127=right/up
    int16_t centered = (int16_t)val - 128;

    // Clamp to N64 range
    if (centered > 127) centered = 127;
    if (centered < -128) centered = -128;

    return (int8_t)centered;
}

// Map C-buttons from right analog stick position
// When right stick exceeds threshold, activate corresponding C-button
#define C_BUTTON_THRESHOLD 96  // Distance from center (128 +/- 96) — high to avoid drift

static void map_c_buttons_from_stick(uint8_t rx, uint8_t ry, n64_report_t* report) {
    if (rx > (128 + C_BUTTON_THRESHOLD)) report->c_right = 1;
    if (rx < (128 - C_BUTTON_THRESHOLD)) report->c_left = 1;
    if (ry < (128 - C_BUTTON_THRESHOLD)) report->c_up = 1;    // HID: 0=up
    if (ry > (128 + C_BUTTON_THRESHOLD)) report->c_down = 1;  // HID: 255=down
}

static void map_usbr_to_n64_report(const profile_output_t* output, n64_report_t* report)
{
    uint32_t buttons = output->buttons;

    // D-pad
    report->dpad_up    = ((buttons & JP_BUTTON_DU) != 0) ? 1 : 0;
    report->dpad_down  = ((buttons & JP_BUTTON_DD) != 0) ? 1 : 0;
    report->dpad_left  = ((buttons & JP_BUTTON_DL) != 0) ? 1 : 0;
    report->dpad_right = ((buttons & JP_BUTTON_DR) != 0) ? 1 : 0;

    // Face buttons
    report->a = ((buttons & N64_BUTTON_A) != 0) ? 1 : 0;
    report->b = ((buttons & N64_BUTTON_B) != 0) ? 1 : 0;

    // Z trigger
    report->z = ((buttons & N64_BUTTON_Z) != 0) ? 1 : 0;

    // Shoulder buttons
    report->l = ((buttons & N64_BUTTON_L) != 0) ? 1 : 0;
    report->r = ((buttons & N64_BUTTON_R) != 0) ? 1 : 0;

    // Start
    report->start = ((buttons & N64_BUTTON_START) != 0) ? 1 : 0;

    // C-buttons from digital button presses (profile mappings)
    report->c_up    = ((buttons & N64_BUTTON_CU) != 0) ? 1 : 0;
    report->c_down  = ((buttons & N64_BUTTON_CD) != 0) ? 1 : 0;
    report->c_left  = ((buttons & N64_BUTTON_CL) != 0) ? 1 : 0;
    report->c_right = ((buttons & N64_BUTTON_CR) != 0) ? 1 : 0;

    // Also map C-buttons from right stick (OR with button mappings)
    map_c_buttons_from_stick(output->right_x, output->right_y, report);

    // Analog stick: convert from HID unsigned to N64 signed
    // N64: positive X = right, positive Y = up
    // HID: 0=up/left, 128=center, 255=down/right
    report->stick_x = analog_u8_to_n64(output->left_x);
    report->stick_y = -analog_u8_to_n64(output->left_y) - 1;  // Invert Y: HID 0=up, N64 +127=up
}

// ============================================================================
// OUTPUT UPDATE
// ============================================================================

void __not_in_flash_func(update_output)(void)
{
    static uint32_t last_buttons = 0;

    // Get input from router (N64 uses MERGE mode, single player)
    const input_event_t* event = router_get_output(OUTPUT_TARGET_N64, 0);

    // Diagnostic: track data path status
    if (event) {
        n64_router_has_data = true;
        last_buttons = event->buttons;
    }
    if (playersCount > 0) {
        n64_player_assigned = true;
        profile_check_switch_combo(last_buttons);
    }

    if (!event || playersCount == 0) return;

    // Build new report
    n64_report_t new_report = default_n64_report;

    // Get active profile and apply
    const profile_t* profile = profile_get_active(OUTPUT_TARGET_N64);

    profile_output_t output;
    profile_apply(profile,
                  event->buttons,
                  event->analog[ANALOG_LX], event->analog[ANALOG_LY],
                  event->analog[ANALOG_RX], event->analog[ANALOG_RY],
                  event->analog[ANALOG_L2], event->analog[ANALOG_R2],
                  event->analog[ANALOG_RZ],
                  &output);

    // Map to N64 report
    map_usbr_to_n64_report(&output, &new_report);

    codes_task();

    // Atomically update global report
    n64_report = new_report;
}

// ============================================================================
// NATIVE OUTPUT CONFIG (web config: Output > Joybus page)
// ============================================================================
// Mirrors gamecube_device.c — both consoles share the same joybus protocol
// and the same flash_t.joybus_data_pin override field. Currently dead code
// for bt2n64 (no web config transport) and usb2n64 (no CDC config mode);
// becomes live as soon as either of those grows a CDC/NUS transport.

static bool n64_json_get_int(const char* json, const char* key, int* out_val) {
    char search[32];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char* start = strstr(json, search);
    if (!start) return false;
    start += strlen(search);
    while (*start == ' ' || *start == '\t') start++;
    if (*start == '-' || (*start >= '0' && *start <= '9')) {
        *out_val = atoi(start);
        return true;
    }
    return false;
}

static uint16_t n64_get_native_config(char* buf, uint16_t buf_size) {
    flash_t* settings = flash_get_settings();
    int current_pin = N64_DATA_PIN;
    if (settings && settings->joybus_data_pin > 0 && settings->joybus_data_pin <= 28) {
        current_pin = settings->joybus_data_pin;
    }
    int n = snprintf(buf, buf_size,
        "\"type\":\"joybus\","
        "\"modes\":[\"nintendo64\"],"
        "\"current_mode\":\"nintendo64\","
        "\"pins\":{"
            "\"data\":{\"label\":\"Data\",\"value\":%d,\"min\":0,\"max\":28,\"default\":%d}"
        "}",
        current_pin, N64_DATA_PIN);
    return (n > 0 && n < buf_size) ? (uint16_t)n : 0;
}

static bool n64_set_native_config(const char* json, char* response_buf, uint16_t response_size) {
    int pin = -1;
    if (!n64_json_get_int(json, "data", &pin)) {
        snprintf(response_buf, response_size, "{\"ok\":false,\"err\":\"missing pins.data\"}");
        return false;
    }
    if (pin < 0 || pin > 28) {
        snprintf(response_buf, response_size, "{\"ok\":false,\"err\":\"pin out of range\"}");
        return false;
    }
    flash_t* settings = flash_get_settings();
    if (!settings) {
        snprintf(response_buf, response_size, "{\"ok\":false,\"err\":\"flash not initialized\"}");
        return false;
    }
    settings->joybus_data_pin = (uint8_t)pin;
    flash_save_force(settings);
    snprintf(response_buf, response_size, "{\"ok\":true,\"reboot\":true}");

    sleep_ms(150);
    platform_reboot();
    return true;
}

// ============================================================================
// OUTPUT INTERFACE
// ============================================================================

#include "core/output_interface.h"

const OutputInterface n64_output_interface = {
    .name = "N64",
    .target = OUTPUT_TARGET_N64,
    .init = n64_init,
    .core1_task = core1_task,
    .task = n64_task,
    .get_rumble = n64_get_rumble,
    .get_player_led = NULL,
    .get_profile_count = n64_get_profile_count,
    .get_active_profile = n64_get_active_profile_index,
    .set_active_profile = n64_set_active_profile,
    .get_profile_name = n64_get_profile_name,
    .get_trigger_threshold = NULL,
    .get_native_config = n64_get_native_config,
    .set_native_config = n64_set_native_config,
};
