// gc_host.c - Native GameCube Controller Host Driver
//
// Polls native GameCube controllers via the joybus-pio library and submits
// input events to the router.

#include "gc_host.h"
#include "GamecubeController.h"
#include "gamecube_definitions.h"
#include "joybus.h"
#include "gba_multiboot.h"
#include "usb/usbd/usbd.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include "core/services/players/feedback.h"
#include "platform/platform.h"
#include <hardware/pio.h>
#include <pico/time.h>
#include <stdio.h>


// ============================================================================
// INTERNAL STATE
// ============================================================================

static GamecubeController gc_controllers[GC_MAX_PORTS];
static bool initialized = false;
static bool rumble_state[GC_MAX_PORTS] = {false};
static uint8_t disconnect_debounce[GC_MAX_PORTS] = {0};  // Debounce brief disconnects
static bool was_connected[GC_MAX_PORTS] = {false};  // Track connection state
static bool gba_boot_attempted[GC_MAX_PORTS] = {false};  // One multiboot attempt per disconnect cycle
// Consecutive gba_input_read failures since the last successful read.
// Used to detect a GBA power-cycle or unplug after multiboot succeeded:
// once this exceeds GBA_READ_FAIL_RESET_THRESHOLD we clear
// gba_boot_attempted and let the probe re-run from scratch.
#define GBA_READ_FAIL_RESET_THRESHOLD 50
static uint16_t gba_read_fail_streak[GC_MAX_PORTS] = {0};
static bool gba_bridge_owned[GC_MAX_PORTS] = {false};    // True when gba_bridge.c owns the joybus port
static uint32_t gba_probe_next_ms[GC_MAX_PORTS] = {0};  // Rate-limit GBA probes (500ms)

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

// Trigger analog rest bias: GC L/R analog values are not 0 at rest. Per-unit
// mechanical/calibration variance puts the resting value anywhere from 1 to
// ~40, and once released the noise floor still drifts a few units above the
// absolute minimum. Track the lowest value seen per session, then suppress
// anything within GC_TRIGGER_DEADZONE above it as 0. Pressed values rescale
// to 0..255 from the dead-zone ceiling.
#define GC_TRIGGER_INIT_REST 40   // Conservative initial rest cap (narrows on use)
#define GC_TRIGGER_DEADZONE  10   // Idle drift suppression above tracked rest

static uint8_t trigger_rest[GC_MAX_PORTS][2];  // [port][0=L, 1=R]
static bool trigger_rest_init = false;

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

static void trigger_rest_reset(void)
{
    for (int p = 0; p < GC_MAX_PORTS; p++) {
        trigger_rest[p][0] = GC_TRIGGER_INIT_REST;
        trigger_rest[p][1] = GC_TRIGGER_INIT_REST;
    }
    trigger_rest_init = true;
}

// Subtract tracked rest bias + dead-zone, rescale press range to 0..255.
// Auto-calibrates: any value below the current rest becomes the new rest.
static uint8_t trigger_scale(uint8_t raw, uint8_t port, uint8_t which)
{
    uint8_t rest = trigger_rest[port][which];
    if (raw < rest) {
        trigger_rest[port][which] = raw;
        rest = raw;
    }
    uint16_t floor = (uint16_t)rest + GC_TRIGGER_DEADZONE;
    if (raw <= floor) return 0;
    uint16_t span = 255 - floor;
    if (span == 0) return 255;
    return (uint8_t)(((uint16_t)(raw - floor) * 255) / span);
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
    // Bump output drive strength to 12mA (default is 4mA) for cleaner edges
    // — joybus needs sharp transitions in <100ns to be reliable on long
    // jumper-wire harnesses. Default drive is too soft.
    gpio_set_drive_strength(data_pin, GPIO_DRIVE_STRENGTH_12MA);
    // Enable fast slew rate for the same reason.
    gpio_set_slew_rate(data_pin, GPIO_SLEW_RATE_FAST);
    printf("[gc_host]   GPIO%d pull-up enabled, drive=12mA fast-slew, state=%d\n",
           data_pin, gpio_get(data_pin));

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
        gba_boot_attempted[i] = false;
    }

    initialized = true;
    if (gba_payload_len > 0) {
        printf("[gc_host] GBA-as-controller bridge: payload %lu bytes\n",
               (unsigned long)gba_payload_len);
    } else {
        printf("[gc_host] GBA-as-controller bridge: no payload linked (skip)\n");
    }
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

        // Bridge owns the joybus PIO for this port — skip ALL gc_host
        // activity (autopoll AND multiboot). The previous skip below
        // only fired *after* gba_boot_attempted was set, so a fresh
        // bridge-mode boot (where gc_host hasn't autobooted yet) would
        // still race with our bridge's joybus_bridge_xfer on the same
        // PIO state machine, causing every transfer to time out.
        if (gba_bridge_owned[port]) {
            continue;
        }

        // GBA-as-controller path: after multiboot, the GBA-side payload uses
        // joybus mode (0x14 READ → 4 bytes of input state). The standard GC
        // controller protocol (probe/origin/poll) does NOT match — we must
        // bypass GamecubeController_Poll entirely.
        if (gba_boot_attempted[port]) {
            // If gba_bridge owns the bus, skip autopoll — its CDC-driven
            // traffic would race with our reads otherwise.
            if (gba_bridge_owned[port]) {
                continue;
            }
            uint8_t gba_keys[4];
            if (gba_input_read(&controller->_port, gba_keys) < 0) {
                // GBA may have been power-cycled (back into BIOS) or
                // physically disconnected. Tolerate brief transients,
                // but after a streak of failures clear boot_attempted
                // so the next probe cycle re-runs detection and re-
                // multiboots if needed.
                if (++gba_read_fail_streak[port] >= GBA_READ_FAIL_RESET_THRESHOLD) {
                    printf("[gc_host] Port %d: GBA gone silent for %d reads "
                           "→ resetting boot_attempted, will re-probe in 2s\n",
                           port, gba_read_fail_streak[port]);
                    gba_boot_attempted[port]  = false;
                    gba_read_fail_streak[port] = 0;
                    // Give the GBA's BIOS time to finish its power-on
                    // init before bombing the bus again. If it's mid-
                    // boot when we probe, our STATUS commands fight
                    // with the BIOS's own SIO init and the GBA never
                    // reaches multiboot-wait state.
                    gba_probe_next_ms[port] = to_ms_since_boot(get_absolute_time()) + 2000;
                }
                continue;  // transient bus failure
            }
            gba_read_fail_streak[port] = 0;
            // Stale-read filter: cable's level-shifter MCU returns 0 when
            // JSTAT.SEND is clear (= GBA hasn't refilled JOY_TRANS since
            // our last read). Real GBA writes lower 16 bits of JOYTR with
            // KEYINPUT (idle = 0x03FF, never zero). So all-zeros = stale,
            // skip — keep current button state, don't pretend all 10
            // buttons just got pressed.
            if (gba_keys[0] == 0 && gba_keys[1] == 0 &&
                gba_keys[2] == 0 && gba_keys[3] == 0) {
                continue;
            }
            // GBA KEYINPUT layout (0=pressed):
            //   data[0] bit 0: A, 1: B, 2: Select, 3: Start,
            //                4: Right, 5: Left, 6: Up, 7: Down
            //   data[1] bit 0: R, 1: L
            // GBA "A" is the bottom-right face button (positionally Nintendo-B
            // / Sony-Cross), so map it to JP_BUTTON_B2. "B" maps to B1.
            uint16_t k = (uint16_t)gba_keys[0] | ((uint16_t)gba_keys[1] << 8);
            uint32_t buttons = 0;
            if (!(k & (1 << 0))) buttons |= JP_BUTTON_B2;   // A → B2 (Cross/Nintendo-B)
            if (!(k & (1 << 1))) buttons |= JP_BUTTON_B1;   // B → B1 (Square/Nintendo-Y? actually B/A swap)
            if (!(k & (1 << 2))) buttons |= JP_BUTTON_S1;   // Select
            if (!(k & (1 << 3))) buttons |= JP_BUTTON_S2;   // Start
            if (!(k & (1 << 4))) buttons |= JP_BUTTON_DR;
            if (!(k & (1 << 5))) buttons |= JP_BUTTON_DL;
            if (!(k & (1 << 6))) buttons |= JP_BUTTON_DU;
            if (!(k & (1 << 7))) buttons |= JP_BUTTON_DD;
            if (!(k & (1 << 8))) buttons |= JP_BUTTON_R1;
            if (!(k & (1 << 9))) buttons |= JP_BUTTON_L1;

            // First successful read: announce as connected
            if (!was_connected[port]) {
                was_connected[port] = true;
                printf("[gc_host] Port %d: GBA controller connected (k=%04x)\n",
                       port, k);
            }

            if (buttons != prev_buttons[port]) {
                prev_buttons[port] = buttons;
                input_event_t event;
                init_input_event(&event);
                event.dev_addr = 0xD0 + port;
                event.instance = 0;
                event.type = INPUT_TYPE_GAMEPAD;
                event.layout = LAYOUT_NINTENDO_4FACE;
                event.buttons = buttons;
                event.analog[ANALOG_LX] = 128;
                event.analog[ANALOG_LY] = 128;
                event.analog[ANALOG_RX] = 128;
                event.analog[ANALOG_RY] = 128;
                router_submit_input(&event);
            }
            continue;  // skip GC poll path entirely
        }

        // Poll the controller (rumble state passed in poll command)
        gc_report_t report;
        bool success = GamecubeController_Poll(controller, &report, rumble_state[port]);

        // GBA-as-controller bridge: a cartless GBA boots into the BIOS
        // multiboot wait state in SIO normal mode — its 0x00 probe response
        // is silent until the joybus hardware activity flips it into JOY
        // mode. So we can't rely on _status.device == 0x0400 to gate this:
        // we have to try multiboot periodically while no controller is
        // detected. The first attempt usually fails on State 0 (PROBE) and
        // simultaneously kicks the GBA into JOY mode; the next attempt
        // succeeds. We throttle to once every 2s so this doesn't dominate
        // joybus traffic when nothing is plugged in.
        if (!success
            && !GamecubeController_IsInitialized(controller)
            && !gba_boot_attempted[port]
            && !gba_bridge_owned[port]   // daemon owns the bus; don't race
            && gba_payload_len > 0) {
            uint32_t now_ms = to_ms_since_boot(get_absolute_time());
            if (now_ms >= gba_probe_next_ms[port]) {
                gba_probe_next_ms[port] = now_ms + 2000;

                // Firmware-restart shortcut: if a payload is already
                // running on the GBA (e.g. we just rebooted the kb2040
                // but the GBA stayed powered), skip the ~3 s multiboot
                // upload and resume polling immediately. This is what
                // distinguishes "GBA in BIOS multiboot wait, PSF0 set"
                // from "GBA running a payload in JOY mode, PSF0 clear,
                // READ responds with valid jstat".
                if (gba_mb_payload_already_running(&controller->_port)) {
                    printf("[gc_host] Port %d: GBA payload already running "
                           "→ skipping multiboot, resuming poll\n", port);
                    gba_boot_attempted[port] = true;
                    // Tell the running payload to flash its splash for the
                    // current USB output mode — without this the user sees
                    // no visual indication after a firmware reboot.
                    uint8_t mode_id = (uint8_t)usbd_get_mode();
                    gba_send_splash_cmd(&controller->_port, mode_id);
                    continue;
                }

                extern void gba_mb_detect_log_printf(const char* fmt, ...);
                gba_mb_detect_log_printf("→ calling multiboot (len=%lu)\n",
                                         (unsigned long)gba_payload_len);
                printf("[gc_host] Port %d: attempting GBA multiboot "
                       "(payload %lu bytes)\n",
                       port, (unsigned long)gba_payload_len);
                gba_mb_result_t r = gba_mb_upload(&controller->_port,
                                                  gba_payload, gba_payload_len,
                                                  /*palette*/3, /*speed*/0,
                                                  /*channel*/port);
                gba_mb_detect_log_printf("→ multiboot returned %d\n", (int)r);
                if (r == GBA_MB_OK) {
                    printf("[gc_host] Port %d: GBA boot OK, payload running\n", port);
                    gba_boot_attempted[port] = true;
                    sleep_ms(200);  // let the payload init SIO and halt before we poll
                } else {
                    printf("[gc_host] Port %d: GBA multiboot failed (%d), retrying in 2s\n",
                           port, (int)r);
                    // Don't set gba_boot_attempted — keep retrying until success
                }
            }
        }

        // Check connection state
        bool is_connected = GamecubeController_IsInitialized(controller);

        if (!is_connected) {
            // Debounce: require 30 consecutive disconnects before reporting
            if (was_connected[port]) {
                disconnect_debounce[port]++;
                if (disconnect_debounce[port] >= 30) {
                    was_connected[port] = false;
                    disconnect_debounce[port] = 0;
                    // INTENTIONALLY NOT resetting gba_boot_attempted — once
                    // a GBA has multibooted, don't re-upload on transient
                    // disconnects. Users would see Nintendo logo flashing
                    // every few seconds. If a GBA reset is truly desired,
                    // power-cycle the GBA and the gc_host_init re-runs.
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
                // Reset trigger rest bias for this port on fresh connect
                trigger_rest[port][0] = GC_TRIGGER_INIT_REST;
                trigger_rest[port][1] = GC_TRIGGER_INIT_REST;
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

        // Analog triggers: subtract auto-calibrated rest bias so idle = 0
        if (!trigger_rest_init) trigger_rest_reset();
        uint8_t l_analog = trigger_scale(report.l_analog, port, 0);
        uint8_t r_analog = trigger_scale(report.r_analog, port, 1);

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
        event.layout = LAYOUT_GAMECUBE;  // AXBY face style + gates GC hotkeys
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

// ============================================================================
// GBA BRIDGE COORDINATION (called from gba_bridge.c)
// ============================================================================

joybus_port_t* gc_host_get_gba_port(void)
{
    if (!initialized) return NULL;
    return &gc_controllers[0]._port;
}

bool gc_host_gba_boot_attempted(uint8_t port)
{
    if (port >= GC_MAX_PORTS) return false;
    return gba_boot_attempted[port];
}

uint16_t gc_host_gba_read_fail_streak(uint8_t port)
{
    if (port >= GC_MAX_PORTS) return 0;
    return gba_read_fail_streak[port];
}

void gc_host_gba_reset_boot_attempted(uint8_t port)
{
    if (port >= GC_MAX_PORTS) return;
    gba_boot_attempted[port] = false;
    gba_probe_next_ms[port]  = 0;  // probe immediately on next task
}

bool gc_host_gba_acquire_for_bridge(void)
{
    if (!initialized) return false;
    // Allow acquire BEFORE autoboot has fired — daemon may want to
    // upload its own ROM instead of letting gc_host's autoboot install
    // the embedded joypad payload. Once owned, gc_host_task skips both
    // its autopoll AND its autoboot path until released.
    gba_bridge_owned[0] = true;
    return true;
}

void gc_host_gba_release_from_bridge(void)
{
    gba_bridge_owned[0] = false;
}
