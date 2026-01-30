// n64_device.c - N64 Output Device
//
// Outputs controller data to N64 via joybus protocol.
// Uses the universal profile system for button remapping.
// Follows the same pattern as gamecube_device.c.

#include "n64_device.h"
#include "n64_buttons.h"
#include "N64Console.h"
#include "joybus.pio.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "tusb.h"
#include "core/services/storage/flash.h"
#include "core/services/profiles/profile.h"
#include "core/services/players/manager.h"
#include "core/services/codes/codes.h"
#include "core/router/router.h"

// Declaration of global variables
N64Console_t n64;
n64_report_t n64_report;
PIO pio = pio0;

// N64-specific state
static uint8_t n64_rumble = 0;

static uint8_t n64_get_rumble(void) { return n64_rumble; }

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
    // N64 joybus runs at standard clock (no overclock needed unlike GameCube)
    // N64 joybus uses 4µs per bit vs GC's ~5µs, but the PIO handles timing

    // Configure custom UART pins (12=TX, 13=RX)
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    stdio_init_all();

    // Initialize flash settings system
    flash_init();

    // Profile system is initialized by app
    profile_set_player_count_callback(n64_get_player_count_for_profile);

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

    int sm = -1;
    int offset = -1;
    N64Console_init(&n64, N64_DATA_PIN, pio, sm, offset);
    n64_report = default_n64_report;

    const profile_t* profile = profile_get_active(OUTPUT_TARGET_N64);
    if (profile) {
        printf("[n64] Active profile: %s\n", profile->name);
    }
}

// ============================================================================
// CORE 1 TASK (Timing-Critical)
// ============================================================================

void __not_in_flash_func(core1_task)(void)
{
    // Initialize Core 1 for safe flash writes
    flash_safe_execute_core_init();

    while (1)
    {
        // Wait for N64 console to poll controller
        n64_rumble = N64Console_WaitForPoll(&n64) ? 255 : 0;

        // Send N64 controller report
        N64Console_SendReport(&n64, &n64_report);

        update_output();
    }
}

// ============================================================================
// USBR → N64 BUTTON MAPPING
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
#define C_BUTTON_THRESHOLD 64  // Distance from center (128 ± 64)

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

    if (event) {
        last_buttons = event->buttons;
    }

    // Check profile switching combo
    if (playersCount > 0) {
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
                  &output);

    // Map to N64 report
    map_usbr_to_n64_report(&output, &new_report);

    codes_task();

    // Atomically update global report
    n64_report = new_report;
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
    .task = NULL,
    .get_rumble = n64_get_rumble,
    .get_player_led = NULL,
    .get_profile_count = n64_get_profile_count,
    .get_active_profile = n64_get_active_profile_index,
    .set_active_profile = n64_set_active_profile,
    .get_profile_name = n64_get_profile_name,
    .get_trigger_threshold = NULL,
};
