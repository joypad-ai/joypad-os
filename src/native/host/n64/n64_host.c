// n64_host.c - Native N64 Controller Host Driver
//
// Polls native N64 controllers via the joybus-pio library and submits
// input events to the router.

#include "n64_host.h"
#include "native/host/host_interface.h"
#include "N64Controller.h"
#include "n64_definitions.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include <hardware/pio.h>
#include <stdio.h>


// ============================================================================
// INTERNAL STATE
// ============================================================================

static N64Controller n64_controllers[N64_MAX_PORTS];
static bool initialized = false;
static bool rumble_state[N64_MAX_PORTS] = {false};

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

    // N64 L/R are shoulder buttons -> L1/R1
    if (report->l)      buttons |= JP_BUTTON_L1;  // N64 L -> L1
    if (report->r)      buttons |= JP_BUTTON_R1;  // N64 R -> R1

    // N64 Z is a trigger -> L2 (unique for profile remapping)
    if (report->z)      buttons |= JP_BUTTON_L2;  // N64 Z -> L2

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

    static bool first_task = true;
    if (first_task) printf("[n64_host] task: starting poll loop\n");

    for (int port = 0; port < N64_MAX_PORTS; port++) {
        N64Controller* controller = &n64_controllers[port];

        if (first_task) printf("[n64_host] task: polling port %d\n", port);

        // Poll the controller
        n64_report_t report;
        bool success = N64Controller_Poll(controller, &report, rumble_state[port]);

        if (first_task) printf("[n64_host] task: poll returned %d\n", success);
        first_task = false;

        if (!success) {
            // Controller not responding, skip
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

        // N64 L/R as analog triggers (full press = 255)
        uint8_t lt = report.l ? 255 : 0;
        uint8_t rt = report.r ? 255 : 0;

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
    if (port < N64_MAX_PORTS) {
        rumble_state[port] = enabled;
    }
}

// ============================================================================
// HOST INTERFACE
// ============================================================================

static uint8_t n64_host_get_port_count(void)
{
    return N64_MAX_PORTS;
}

static void n64_host_init_pins_generic(const uint8_t* pins, uint8_t pin_count)
{
    if (pin_count >= 1) {
        n64_host_init_pin(pins[0]);
    } else {
        n64_host_init();
    }
}

const HostInterface n64_host_interface = {
    .name = "N64",
    .init = n64_host_init,
    .init_pins = n64_host_init_pins_generic,
    .task = n64_host_task,
    .is_connected = n64_host_is_connected,
    .get_device_type = n64_host_get_device_type,
    .get_port_count = n64_host_get_port_count,
};

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
