// n64_host.c - Native N64 Controller Host Driver
//
// Polls native N64 controllers via the joybus-pio library and submits
// input events to the router.

#include "n64_host.h"
#include "N64Controller.h"
#include "n64_definitions.h"
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

static N64Controller n64_controllers[N64_MAX_PORTS];
static bool initialized = false;
static bool rumble_state[N64_MAX_PORTS] = {false};
static bool rumble_pending[N64_MAX_PORTS] = {false};  // Deferred rumble update flag
static bool rumble_pak_initialized[N64_MAX_PORTS] = {false};  // Track pak init state
static uint8_t connected_polls[N64_MAX_PORTS] = {0};  // Count polls after connect for pak init
static uint8_t disconnect_debounce[N64_MAX_PORTS] = {0};  // Debounce brief disconnects

// Track previous state for edge detection
static uint32_t prev_buttons[N64_MAX_PORTS] = {0};
static int8_t prev_stick_x[N64_MAX_PORTS] = {0};
static int8_t prev_stick_y[N64_MAX_PORTS] = {0};
static bool prev_l[N64_MAX_PORTS] = {false};
static bool prev_r[N64_MAX_PORTS] = {false};

// ============================================================================
// BUTTON MAPPING: N64 -> JP
// ============================================================================

// Map N64 controller state to Joypad button format
static uint32_t map_n64_to_jp(const n64_report_t* report)
{
    uint32_t buttons = 0x00000000;

    // Face buttons (matching DC layout: A, B, X, Y)
    if (report->a)       buttons |= JP_BUTTON_B1;  // N64 A -> B1 (DC A)
    if (report->c_down)  buttons |= JP_BUTTON_B2;  // N64 C-Down -> B2 (DC B)
    if (report->b)       buttons |= JP_BUTTON_B3;  // N64 B -> B3 (DC X)
    if (report->c_left)  buttons |= JP_BUTTON_B4;  // N64 C-Left -> B4 (DC Y)

    // Remaining C-buttons to stick clicks (for DC Z/C)
    if (report->c_up)    buttons |= JP_BUTTON_L3;  // C-Up -> L3 (DC Z)
    if (report->c_right) buttons |= JP_BUTTON_R3;  // C-Right -> R3 (DC C)

    // N64 L/R are the primary triggers -> L2/R2 (consistent with GC/DC)
    if (report->l)      buttons |= JP_BUTTON_L2;  // N64 L -> L2
    if (report->r)      buttons |= JP_BUTTON_R2;  // N64 R -> R2

    // N64 Z is the shoulder/bumper button -> R1 (consistent with GC Z)
    if (report->z)      buttons |= JP_BUTTON_R1;  // N64 Z -> R1

    // Start
    if (report->start)  buttons |= JP_BUTTON_S2;  // Start -> S2

    // D-pad
    if (report->dpad_up)    buttons |= JP_BUTTON_DU;
    if (report->dpad_down)  buttons |= JP_BUTTON_DD;
    if (report->dpad_left)  buttons |= JP_BUTTON_DL;
    if (report->dpad_right) buttons |= JP_BUTTON_DR;

    return buttons;
}

// Convert N64 signed stick to unsigned (0-255, 128 = center)
// N64 sticks typically only reach ±80 to ±85, not ±128
// Scale up to use full range
#define N64_STICK_MAX 80  // Typical max deflection

static uint8_t convert_stick_axis(int8_t value)
{
    // Scale from N64 range [-80, +80] to [-128, +127]
    int32_t scaled = ((int32_t)value * 127) / N64_STICK_MAX;

    // Clamp to valid range
    if (scaled > 127) scaled = 127;
    if (scaled < -128) scaled = -128;

    // Convert to unsigned (0-255, center = 128)
    return (uint8_t)(scaled + 128);
}


// Convert C-buttons to right analog stick
static void map_c_buttons_to_analog(const n64_report_t* report, uint8_t* rx, uint8_t* ry)
{
    // Default to center
    *rx = 128;
    *ry = 128;

    // C-buttons act as digital right stick
    if (report->c_left)  *rx = 0;
    if (report->c_right) *rx = 255;
    if (report->c_up)    *ry = 0;    // Up = low Y (inverted from stick)
    if (report->c_down)  *ry = 255;  // Down = high Y
}

// ============================================================================
// PUBLIC API
// ============================================================================

void n64_host_init(void)
{
#ifdef CONFIG_N642DC_DISABLE_JOYBUS
    printf("[n64_host] JOYBUS DISABLED FOR TESTING\n");
    return;
#endif
    if (initialized) return;
    n64_host_init_pin(N64_PIN_DATA);
}

void n64_host_init_pin(uint8_t data_pin)
{
#ifdef CONFIG_N642DC_DISABLE_JOYBUS
    // Temporarily disable joybus to test DC stability
    printf("[n64_host] JOYBUS DISABLED FOR TESTING\n");
    initialized = false;
    return;
#endif

    printf("[n64_host] Initializing N64 host driver\n");
    printf("[n64_host]   DATA=%d, rate=%dHz\n", data_pin, N64_POLLING_RATE);

    // Enable pull-up before joybus init (open-drain protocol needs pull-up)
    gpio_init(data_pin);
    gpio_set_dir(data_pin, GPIO_IN);
    gpio_pull_up(data_pin);
    printf("[n64_host]   GPIO%d pull-up enabled, state=%d\n", data_pin, gpio_get(data_pin));

    // Initialize N64 controller on port 0
#ifdef CONFIG_DC
    // For DC builds: PIO0 has maple_tx (29 inst), no room for joybus (22 inst)
    // Use PIO1 which has maple_rx (10 inst), room for joybus
    // CRITICAL: maple_rx loads AFTER joybus and needs 10 slots (2+4+4)
    // joybus is 22 instructions, so place it at offset 10 to use slots 10-31
    // This leaves slots 0-9 (10 exactly) for maple_rx
    N64Controller_init(&n64_controllers[0], data_pin, N64_POLLING_RATE,
                       pio1, 3, 10);  // PIO1 SM3, offset 10 (leaves 0-9 for maple_rx)
    printf("[n64_host]   joybus loaded at PIO1 offset %d\n", N64Controller_GetOffset(&n64_controllers[0]));
#else
    N64Controller_init(&n64_controllers[0], data_pin, N64_POLLING_RATE,
                       pio0, -1, -1);
    printf("[n64_host]   joybus loaded at PIO0 offset %d\n", N64Controller_GetOffset(&n64_controllers[0]));
#endif

    prev_buttons[0] = 0xFFFFFFFF;
    prev_stick_x[0] = 0;
    prev_stick_y[0] = 0;
    rumble_state[0] = false;

    initialized = true;
    printf("[n64_host] Initialization complete\n");
}

void n64_host_task(void)
{
    if (!initialized) return;

    // Check feedback system for rumble updates (works with any output: DC, USB, etc.)
    for (int port = 0; port < N64_MAX_PORTS; port++) {
        feedback_state_t* feedback = feedback_get_state(port);
        if (feedback && feedback->rumble_dirty) {
            // N64 rumble is binary (on/off), use max of left/right motors
            bool want_rumble = (feedback->rumble.left > 0 || feedback->rumble.right > 0);
            if (want_rumble != rumble_state[port]) {
                n64_host_set_rumble(port, want_rumble);
            }
            // Clear dirty flag after processing
            feedback_clear_dirty(port);
        }
    }

    for (int port = 0; port < N64_MAX_PORTS; port++) {
        N64Controller* controller = &n64_controllers[port];

        // Poll the controller
        n64_report_t report;
        bool success = N64Controller_Poll(controller, &report, rumble_state[port]);

        // Check connection state using IsInitialized (not poll return value)
        bool is_connected = N64Controller_IsInitialized(controller);

        if (!is_connected) {
            // Debounce: require 30 consecutive disconnects (~500ms) before reporting
            // Brief disconnects are normal during pak commands
            if (connected_polls[port] > 0) {
                disconnect_debounce[port]++;
                if (disconnect_debounce[port] >= 30) {
                    connected_polls[port] = 0;
                    disconnect_debounce[port] = 0;
                    rumble_pak_initialized[port] = false;  // Real disconnect - reset pak
                    printf("[n64_host] Port %d: disconnected\n", port);

                    // Send cleared input to prevent stuck buttons
                    input_event_t event;
                    init_input_event(&event);
                    event.dev_addr = 0xE0 + port;
                    event.instance = 0;
                    event.type = INPUT_TYPE_GAMEPAD;
                    event.buttons = 0;
                    event.analog[ANALOG_LX] = 128;
                    event.analog[ANALOG_LY] = 128;
                    event.analog[ANALOG_RX] = 128;
                    event.analog[ANALOG_RY] = 128;
                    router_submit_input(&event);

                    // Reset previous state tracking
                    prev_buttons[port] = 0;
                    prev_stick_x[port] = 0;
                    prev_stick_y[port] = 0;
                    prev_l[port] = false;
                    prev_r[port] = false;
                }
            }
        } else {
            // Connected - reset debounce counter
            disconnect_debounce[port] = 0;
            if (connected_polls[port] == 0) {
                // Just connected - start counting polls
                connected_polls[port] = 1;
                printf("[n64_host] Port %d: connected\n", port);
            }
        }

        // Init rumble pak ONCE after first stable connection
        // Don't re-init on brief disconnects (pak commands can cause poll failures)
        if (is_connected && connected_polls[port] > 0 && connected_polls[port] < 255) {
            connected_polls[port]++;

            // Init pak after 10 polls (~170ms at 60Hz), only if never initialized
            if (connected_polls[port] == 10 && !rumble_pak_initialized[port]) {
                if (N64Controller_HasPak(controller)) {
                    printf("[n64_host] Port %d: pak detected, initializing rumble\n", port);
                    if (N64Controller_InitRumblePak(controller)) {
                        rumble_pak_initialized[port] = true;
                        printf("[n64_host] Port %d: rumble pak initialized\n", port);
                    }
                }
            }
        }

        // Skip input processing if poll didn't return data
        if (!success) {
            continue;
        }

        // Convert analog stick
        uint8_t stick_x = convert_stick_axis(report.stick_x);
        uint8_t stick_y = convert_stick_axis(-report.stick_y);  // Invert Y for standard convention

        // Map buttons and analog via router
        uint32_t buttons = map_n64_to_jp(&report);

        // Map C-buttons to right stick
        uint8_t c_rx, c_ry;
        map_c_buttons_to_analog(&report, &c_rx, &c_ry);

        // N64 has no analog triggers - L/R are digital buttons (L1/R1)
        uint8_t lt = 0;
        uint8_t rt = 0;

        // Only submit if state changed
        if (buttons == prev_buttons[port] &&
            report.stick_x == prev_stick_x[port] &&
            report.stick_y == prev_stick_y[port] &&
            report.l == prev_l[port] &&
            report.r == prev_r[port]) {
            continue;
        }
        prev_buttons[port] = buttons;
        prev_stick_x[port] = report.stick_x;
        prev_stick_y[port] = report.stick_y;
        prev_l[port] = report.l;
        prev_r[port] = report.r;

        // Build input event
        input_event_t event;
        init_input_event(&event);

        event.dev_addr = 0xE0 + port;  // Use 0xE0+ range for N64 native inputs
        event.instance = 0;
        event.type = INPUT_TYPE_GAMEPAD;
        event.buttons = buttons;
        event.analog[ANALOG_LX] = stick_x;
        event.analog[ANALOG_LY] = stick_y;
        event.analog[ANALOG_RX] = c_rx;
        event.analog[ANALOG_RY] = c_ry;
        event.analog[ANALOG_L2] = lt;
        event.analog[ANALOG_R2] = rt;

        // Submit to router
        router_submit_input(&event);
    }

    // Flush any pending rumble commands after polling
    n64_host_flush_rumble();
}

bool n64_host_is_connected(void)
{
    if (!initialized) return false;

    for (int i = 0; i < N64_MAX_PORTS; i++) {
        if (N64Controller_IsInitialized(&n64_controllers[i])) {
            return true;
        }
    }
    return false;
}

int8_t n64_host_get_device_type(uint8_t port)
{
    if (!initialized || port >= N64_MAX_PORTS) {
        return -1;
    }

    if (!N64Controller_IsInitialized(&n64_controllers[port])) {
        return -1;
    }

    const n64_status_t* status = N64Controller_GetStatus(&n64_controllers[port]);
    // Device types from status byte:
    // 0x00 = no pak, 0x01 = controller pak, 0x02 = rumble pak
    return (int8_t)(status->status & 0x03);
}

void n64_host_set_rumble(uint8_t port, bool enabled)
{
    if (port >= N64_MAX_PORTS) return;
    if (!initialized) return;

    // Only mark pending if state actually changes
    if (rumble_state[port] == enabled) return;
    rumble_state[port] = enabled;

    // Mark as pending - actual send deferred to n64_host_flush_rumble()
    // This prevents blocking the main loop before Dreamcast response
    rumble_pending[port] = true;
}

// Send any pending rumble commands to N64 controller
// Call this AFTER time-critical tasks (like DC Maple response)
// Rate-limited to prevent rapid on/off from blocking DC maple timing
#define RUMBLE_MIN_INTERVAL_MS 50  // Minimum 50ms between rumble commands
static absolute_time_t last_rumble_time[N64_MAX_PORTS] = {0};

void n64_host_flush_rumble(void)
{
    if (!initialized) return;

    for (uint8_t port = 0; port < N64_MAX_PORTS; port++) {
        // Skip if controller not initialized (but don't reset pak state)
        if (!N64Controller_IsInitialized(&n64_controllers[port])) {
            continue;
        }

        if (rumble_pending[port]) {
            // Rate limit: skip if we sent a command too recently
            if (!time_reached(last_rumble_time[port])) {
                // Keep pending, will try again next iteration
                continue;
            }

            rumble_pending[port] = false;
            last_rumble_time[port] = make_timeout_time_ms(RUMBLE_MIN_INTERVAL_MS);

            // Only send rumble if pak was initialized on connect
            if (rumble_pak_initialized[port]) {
                N64Controller_SetRumble(&n64_controllers[port], rumble_state[port]);
            }
        }
    }
}

// ============================================================================
// INPUT INTERFACE
// ============================================================================

static uint8_t n64_get_device_count(void)
{
    uint8_t count = 0;
    for (int i = 0; i < N64_MAX_PORTS; i++) {
        if (N64Controller_IsInitialized(&n64_controllers[i])) {
            count++;
        }
    }
    return count;
}

const InputInterface n64_input_interface = {
    .name = "N64",
    .source = INPUT_SOURCE_NATIVE_N64,
    .init = n64_host_init,
    .task = n64_host_task,
    .is_connected = n64_host_is_connected,
    .get_device_count = n64_get_device_count,
};
