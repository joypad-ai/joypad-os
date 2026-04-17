// gc_host.c - Native GameCube Controller Host Driver
//
// Polls native GameCube controllers via the joybus-pio library and submits
// input events to the router.

#include "gc_host.h"
#include "GamecubeController.h"
#include "gamecube_definitions.h"
#include "joybus.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include "core/services/players/feedback.h"
#include <hardware/pio.h>
#include <stdio.h>


// ============================================================================
// INTERNAL STATE
// ============================================================================

static GamecubeController gc_controllers[GC_MAX_PORTS];
static bool initialized = false;
static bool rumble_state[GC_MAX_PORTS] = {false};
static uint8_t disconnect_debounce[GC_MAX_PORTS] = {0};  // Debounce brief disconnects
static bool was_connected[GC_MAX_PORTS] = {false};  // Track connection state

// Track previous state for edge detection
static uint32_t prev_buttons[GC_MAX_PORTS] = {0};
static uint8_t prev_stick_x[GC_MAX_PORTS] = {128};
static uint8_t prev_stick_y[GC_MAX_PORTS] = {128};
static uint8_t prev_cstick_x[GC_MAX_PORTS] = {128};
static uint8_t prev_cstick_y[GC_MAX_PORTS] = {128};
static uint8_t prev_l_analog[GC_MAX_PORTS] = {0};
static uint8_t prev_r_analog[GC_MAX_PORTS] = {0};

// ============================================================================
// AUTO-CALIBRATING STICK RANGE
// ============================================================================
// GC sticks rarely reach the full 0-255 range. Track the min/max seen per
// axis per session and scale the output to 0-255. Starts with a conservative
// range (~30-225) and widens as the user moves the sticks to their physical
// limits. Center (128) is preserved.
//
// TODO: make the initial range and dead zone configurable via web config
//       (pad_config or a dedicated gc_host config section).

#define GC_STICK_INIT_MIN  40   // Conservative initial min (widens on use)
#define GC_STICK_INIT_MAX  215  // Conservative initial max (widens on use)
#define GC_STICK_DEADZONE  3    // Ignore values within ±3 of center for calibration

static struct {
    uint8_t min;
    uint8_t max;
} stick_range[GC_MAX_PORTS][4];  // [port][0=LX, 1=LY, 2=RX, 3=RY]
static bool stick_range_init = false;

static void stick_range_reset(void)
{
    for (int p = 0; p < GC_MAX_PORTS; p++) {
        for (int a = 0; a < 4; a++) {
            stick_range[p][a].min = GC_STICK_INIT_MIN;
            stick_range[p][a].max = GC_STICK_INIT_MAX;
        }
    }
    stick_range_init = true;
}

// Scale raw value using tracked min/max → 0-255 with 128 center preserved
static uint8_t stick_scale(uint8_t raw, uint8_t port, uint8_t axis)
{
    // Update tracked range (ignore values near center — stick rest noise)
    if (raw < 128 - GC_STICK_DEADZONE || raw > 128 + GC_STICK_DEADZONE) {
        if (raw < stick_range[port][axis].min) stick_range[port][axis].min = raw;
        if (raw > stick_range[port][axis].max) stick_range[port][axis].max = raw;
    }

    uint8_t lo = stick_range[port][axis].min;
    uint8_t hi = stick_range[port][axis].max;

    // Avoid division by zero or degenerate range
    if (hi <= lo || hi - lo < 20) return raw;

    // Scale: map [lo..hi] → [0..255]
    if (raw <= lo) return 0;
    if (raw >= hi) return 255;
    return (uint8_t)(((uint16_t)(raw - lo) * 255) / (hi - lo));
}

// ============================================================================
// BUTTON MAPPING: GC -> JP
// ============================================================================

// Map GameCube controller state to Joypad button format
static uint32_t map_gc_to_jp(const gc_report_t* report)
{
    uint32_t buttons = 0x00000000;

    // Face buttons (GC layout: A=right, B=down, X=top, Y=left)
    if (report->a)      buttons |= JP_BUTTON_B2;  // GC A -> B2
    if (report->b)      buttons |= JP_BUTTON_B1;  // GC B -> B1
    if (report->x)      buttons |= JP_BUTTON_B4;  // GC X -> B4
    if (report->y)      buttons |= JP_BUTTON_B3;  // GC Y -> B3

    // Shoulder buttons
    // GC L/R digital clicks map to L2/R2 (triggers)
    if (report->l)      buttons |= JP_BUTTON_L2;  // GC L digital -> L2
    if (report->r)      buttons |= JP_BUTTON_R2;  // GC R digital -> R2

    // Z button
    if (report->z)      buttons |= JP_BUTTON_R1;  // GC Z -> R1

    // Start
    if (report->start)  buttons |= JP_BUTTON_S2;  // Start -> S2

    // D-pad
    if (report->dpad_up)    buttons |= JP_BUTTON_DU;
    if (report->dpad_down)  buttons |= JP_BUTTON_DD;
    if (report->dpad_left)  buttons |= JP_BUTTON_DL;
    if (report->dpad_right) buttons |= JP_BUTTON_DR;

    return buttons;
}

// ============================================================================
// PUBLIC API
// ============================================================================

void gc_host_init(void)
{
    if (initialized) return;
    gc_host_init_pin(GC_PIN_DATA);
}

void gc_host_init_pin(uint8_t data_pin)
{
    printf("[gc_host] Initializing GC host driver\n");
    printf("[gc_host]   DATA=%d, rate=%dHz\n", data_pin, GC_POLLING_RATE);

    // Enable pull-up before joybus init (open-drain protocol needs pull-up)
    gpio_init(data_pin);
    gpio_set_dir(data_pin, GPIO_IN);
    gpio_pull_up(data_pin);
    printf("[gc_host]   GPIO%d pull-up enabled, state=%d\n", data_pin, gpio_get(data_pin));

    // Initialize GameCube controller on port 0
    GamecubeController_init(&gc_controllers[0], data_pin, GC_POLLING_RATE,
                            pio0, -1, -1);
    printf("[gc_host]   joybus loaded at PIO0 offset %d\n", GamecubeController_GetOffset(&gc_controllers[0]));

    // Initialize state tracking
    for (int i = 0; i < GC_MAX_PORTS; i++) {
        prev_buttons[i] = 0xFFFFFFFF;  // Force first event
        prev_stick_x[i] = 128;
        prev_stick_y[i] = 128;
        prev_cstick_x[i] = 128;
        prev_cstick_y[i] = 128;
        prev_l_analog[i] = 0;
        prev_r_analog[i] = 0;
        rumble_state[i] = false;
        was_connected[i] = false;
    }

    initialized = true;
    printf("[gc_host] Initialization complete\n");
}

void gc_host_task(void)
{
    if (!initialized) return;


    // Check feedback system for rumble updates
    for (int port = 0; port < GC_MAX_PORTS; port++) {
        feedback_state_t* feedback = feedback_get_state(port);
        if (feedback && feedback->rumble_dirty) {
            // GC rumble is binary (on/off), use max of left/right motors
            bool want_rumble = (feedback->rumble.left > 0 || feedback->rumble.right > 0);
            if (want_rumble != rumble_state[port]) {
                gc_host_set_rumble(port, want_rumble);
            }
            // Clear dirty flag after processing
            feedback_clear_dirty(port);
        }
    }

    for (int port = 0; port < GC_MAX_PORTS; port++) {
        GamecubeController* controller = &gc_controllers[port];

        // Poll the controller (rumble state passed in poll command)
        gc_report_t report;
        bool success = GamecubeController_Poll(controller, &report, rumble_state[port]);

        // Check connection state
        bool is_connected = GamecubeController_IsInitialized(controller);

        if (!is_connected) {
            // Debounce: require 30 consecutive disconnects before reporting
            if (was_connected[port]) {
                disconnect_debounce[port]++;
                if (disconnect_debounce[port] >= 30) {
                    was_connected[port] = false;
                    disconnect_debounce[port] = 0;
                    printf("[gc_host] Port %d: disconnected\n", port);

                    // Send cleared input to prevent stuck buttons
                    input_event_t event;
                    init_input_event(&event);
                    event.dev_addr = 0xD0 + port;  // Use 0xD0+ range for GC native inputs
                    event.instance = 0;
                    event.type = INPUT_TYPE_GAMEPAD;
                    event.buttons = 0;
                    event.analog[ANALOG_LX] = 128;
                    event.analog[ANALOG_LY] = 128;
                    event.analog[ANALOG_RX] = 128;
                    event.analog[ANALOG_RY] = 128;
                    event.analog[ANALOG_L2] = 0;
                    event.analog[ANALOG_R2] = 0;
                    router_submit_input(&event);

                    // Reset previous state tracking
                    prev_buttons[port] = 0;
                    prev_stick_x[port] = 128;
                    prev_stick_y[port] = 128;
                    prev_cstick_x[port] = 128;
                    prev_cstick_y[port] = 128;
                    prev_l_analog[port] = 0;
                    prev_r_analog[port] = 0;
                }
            }
        } else {
            // Connected - reset debounce counter
            disconnect_debounce[port] = 0;
            if (!was_connected[port]) {
                was_connected[port] = true;
                // Reset stick calibration for this port on fresh connect
                for (int a = 0; a < 4; a++) {
                    stick_range[port][a].min = GC_STICK_INIT_MIN;
                    stick_range[port][a].max = GC_STICK_INIT_MAX;
                }
                printf("[gc_host] Port %d: connected\n", port);
            }
        }

        // Skip input processing if poll didn't return data
        if (!success) {
            continue;
        }

        // Map buttons
        uint32_t buttons = map_gc_to_jp(&report);

        // GC sticks rarely reach full 0-255. Auto-calibrate by tracking
        // min/max per axis and scaling to full range.
        // Y-axis is inverted (GC: 0=down, we want 0=up standard HID)
        if (!stick_range_init) stick_range_reset();

        uint8_t stick_x  = stick_scale(report.stick_x,           port, 0);
        uint8_t stick_y  = stick_scale(255 - report.stick_y,     port, 1);
        uint8_t cstick_x = stick_scale(report.cstick_x,          port, 2);
        uint8_t cstick_y = stick_scale(255 - report.cstick_y,    port, 3);

        // Analog triggers (0-255)
        uint8_t l_analog = report.l_analog;
        uint8_t r_analog = report.r_analog;

        // Always submit input events - USB output needs continuous reports
        // even when controller state hasn't changed (held stick positions)
        // Note: keeping prev_* tracking for future use (e.g., edge detection)
        prev_buttons[port] = buttons;
        prev_stick_x[port] = stick_x;
        prev_stick_y[port] = stick_y;
        prev_cstick_x[port] = cstick_x;
        prev_cstick_y[port] = cstick_y;
        prev_l_analog[port] = l_analog;
        prev_r_analog[port] = r_analog;

        // Build input event
        input_event_t event;
        init_input_event(&event);

        event.dev_addr = 0xD0 + port;  // Use 0xD0+ range for GC native inputs
        event.instance = 0;
        event.type = INPUT_TYPE_GAMEPAD;
        event.buttons = buttons;
        event.analog[ANALOG_LX] = stick_x;
        event.analog[ANALOG_LY] = stick_y;
        event.analog[ANALOG_RX] = cstick_x;
        event.analog[ANALOG_RY] = cstick_y;
        event.analog[ANALOG_L2] = l_analog;
        event.analog[ANALOG_R2] = r_analog;

        // Submit to router
        router_submit_input(&event);
    }
}

bool gc_host_is_connected(void)
{
    if (!initialized) return false;

    for (int i = 0; i < GC_MAX_PORTS; i++) {
        if (GamecubeController_IsInitialized(&gc_controllers[i])) {
            return true;
        }
    }
    return false;
}

int16_t gc_host_get_device_type(uint8_t port)
{
    if (!initialized || port >= GC_MAX_PORTS) {
        return -1;
    }

    if (!GamecubeController_IsInitialized(&gc_controllers[port])) {
        return -1;
    }

    const gc_status_t* status = GamecubeController_GetStatus(&gc_controllers[port]);
    return (int16_t)status->device;
}

void gc_host_set_rumble(uint8_t port, bool enabled)
{
    if (port >= GC_MAX_PORTS) return;
    if (!initialized) return;

    // GC rumble is controlled via the poll command, just update state
    rumble_state[port] = enabled;
}

// ============================================================================
// INPUT INTERFACE
// ============================================================================

static uint8_t gc_get_device_count(void)
{
    uint8_t count = 0;
    for (int i = 0; i < GC_MAX_PORTS; i++) {
        if (GamecubeController_IsInitialized(&gc_controllers[i])) {
            count++;
        }
    }
    return count;
}

const InputInterface gc_input_interface = {
    .name = "GC",
    .source = INPUT_SOURCE_NATIVE_GC,
    .init = gc_host_init,
    .task = gc_host_task,
    .is_connected = gc_host_is_connected,
    .get_device_count = gc_get_device_count,
};
