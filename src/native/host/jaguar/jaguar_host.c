// jaguar_host.c - Native Atari Jaguar Controller Host Driver
//
// See jaguar_host.h for the protocol summary. This driver bit-bangs the
// column selects directly (the pad is a passive matrix with no clock to
// track, so PIO is unnecessary) and polls at ~60 Hz from jaguar_host_task().
//
// Every poll scans all four rows in console order (0xE, 0xD, 0xB, 0x7) and
// returns the selects to idle. Always completing the 4-row cycle matters
// for future 6D-controller support: its bank counter advances per finished
// cycle, so partial scans would desync it.

#include "jaguar_host.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include <stdio.h>

#define JAG_POLL_INTERVAL_US 16666   // ~60 Hz
#define JAG_SETTLE_US        6       // select + cable + diode settling

#define JAG_DEV_ADDR 0xE8            // virtual device address (native range)

// Hold Pause+Option this long to toggle Pro Controller mode.
#define JAG_PRO_TOGGLE_HOLD_US 2000000

// Return-line bit positions within a sampled row (active-low raw).
#define JAG_BIT_B0   0
#define JAG_BIT_B1   1
#define JAG_BIT_J8   2
#define JAG_BIT_J9   3
#define JAG_BIT_J10  4
#define JAG_BIT_J11  5
#define JAG_ROW_IDLE 0x3F            // all six lines released (pull-ups)

// HID Keyboard/Keypad page (0x07) usages for the 12-key pad.
#define HID_KEY_KEYPAD_ASTERISK 0x55
#define HID_KEY_KEYPAD_ENTER    0x58  // stands in for '#' (real Keypad # 0xCC
                                      // has poor OS support)
#define HID_KEY_KEYPAD_1        0x59  // ..0x61 = Keypad 9
#define HID_KEY_KEYPAD_0        0x62

static const uint8_t jag_select_pins[4] = {
    JAG_PIN_J0, JAG_PIN_J1, JAG_PIN_J2, JAG_PIN_J3
};

static struct {
    bool     initialized;
    uint64_t next_poll_us;

    uint8_t  rows[4];           // raw active-low samples, bit order per JAG_BIT_*
    bool     connected;         // latched on first press (idle == empty port)
    bool     pro_mode;          // keypad 7/8/9/4/6 → X/Y/Z/L/R gamepad buttons
    bool     foreign_device;    // C2 asserted: 6D/rotary — hold neutral

    uint64_t combo_hold_start_us;   // Pause+Option pro-mode toggle tracking
    bool     combo_latched;         // toggle fired; wait for release
} s_jag;

// Sample one matrix key from the captured rows (active-low → pressed=true).
static inline bool jag_key(int row, int bit)
{
    return !(s_jag.rows[row] & (1u << bit));
}

void jaguar_host_init(void)
{
    printf("[jaguar_host] Initializing Jaguar host\n");

    // Selects idle HIGH (nothing addressed).
    for (int i = 0; i < 4; i++) {
        gpio_init(jag_select_pins[i]);
        gpio_set_dir(jag_select_pins[i], GPIO_OUT);
        gpio_put(jag_select_pins[i], 1);
    }

    // Returns: inputs with pull-ups. The passive matrix pulls a line LOW
    // through a steering diode when its key is pressed on the selected row.
    const uint8_t input_pins[6] = {
        JAG_PIN_B0, JAG_PIN_B1, JAG_PIN_J8, JAG_PIN_J9, JAG_PIN_J10, JAG_PIN_J11
    };
    for (int i = 0; i < 6; i++) {
        gpio_init(input_pins[i]);
        gpio_set_dir(input_pins[i], GPIO_IN);
        gpio_pull_up(input_pins[i]);
    }

    s_jag.initialized = true;
    s_jag.next_poll_us = time_us_64();
    for (int r = 0; r < 4; r++) s_jag.rows[r] = JAG_ROW_IDLE;

    printf("[jaguar_host] Jaguar host ready (J0..J3=%d,%d,%d,%d B0=%d B1=%d J8..J11=%d,%d,%d,%d)\n",
           JAG_PIN_J0, JAG_PIN_J1, JAG_PIN_J2, JAG_PIN_J3,
           JAG_PIN_B0, JAG_PIN_B1,
           JAG_PIN_J8, JAG_PIN_J9, JAG_PIN_J10, JAG_PIN_J11);
}

// Sample the six return lines into a raw active-low byte.
static inline uint8_t jag_read_returns(void)
{
    uint32_t all = gpio_get_all();
    return (uint8_t)((((all >> JAG_PIN_B0)  & 1u) << JAG_BIT_B0)  |
                     (((all >> JAG_PIN_B1)  & 1u) << JAG_BIT_B1)  |
                     (((all >> JAG_PIN_J8)  & 1u) << JAG_BIT_J8)  |
                     (((all >> JAG_PIN_J9)  & 1u) << JAG_BIT_J9)  |
                     (((all >> JAG_PIN_J10) & 1u) << JAG_BIT_J10) |
                     (((all >> JAG_PIN_J11) & 1u) << JAG_BIT_J11));
}

// One full scan: rows 0..3 in console order, selects back to idle after.
static void jag_scan(void)
{
    for (int r = 0; r < 4; r++) {
        gpio_put(jag_select_pins[r], 0);
        busy_wait_us_32(JAG_SETTLE_US);
        s_jag.rows[r] = jag_read_returns();
        gpio_put(jag_select_pins[r], 1);
    }
}

// Decode face buttons + d-pad (positions shared by standard and Pro pads).
static uint32_t jag_decode_buttons(void)
{
    uint32_t buttons = 0;

    if (jag_key(0, JAG_BIT_J8))  buttons |= JP_BUTTON_DU;
    if (jag_key(0, JAG_BIT_J9))  buttons |= JP_BUTTON_DD;
    if (jag_key(0, JAG_BIT_J10)) buttons |= JP_BUTTON_DL;
    if (jag_key(0, JAG_BIT_J11)) buttons |= JP_BUTTON_DR;
    if (jag_key(0, JAG_BIT_B1))  buttons |= JP_BUTTON_B1;  // A
    if (jag_key(1, JAG_BIT_B1))  buttons |= JP_BUTTON_B2;  // B
    if (jag_key(2, JAG_BIT_B1))  buttons |= JP_BUTTON_R1;  // C (M30/Sega convention)
    if (jag_key(3, JAG_BIT_B1))  buttons |= JP_BUTTON_S1;  // Option
    if (jag_key(0, JAG_BIT_B0))  buttons |= JP_BUTTON_S2;  // Pause

    if (s_jag.pro_mode) {
        // Pro Controller: keypad 7/8/9 = X/Y/Z, 4/6 = L/R (shared matrix keys).
        if (jag_key(1, JAG_BIT_J9))  buttons |= JP_BUTTON_B3;  // X (kp7)
        if (jag_key(2, JAG_BIT_J9))  buttons |= JP_BUTTON_B4;  // Y (kp8)
        if (jag_key(3, JAG_BIT_J9))  buttons |= JP_BUTTON_L1;  // Z (kp9)
        if (jag_key(1, JAG_BIT_J10)) buttons |= JP_BUTTON_L2;  // L (kp4)
        if (jag_key(3, JAG_BIT_J10)) buttons |= JP_BUTTON_R2;  // R (kp6)
    }

    return buttons;
}

// Emit pressed keypad keys as numpad usages into kb_keys[] (6-key rollover).
// In Pro mode the five positions consumed by X/Y/Z/L/R are skipped.
static void jag_decode_keypad(input_event_t* event)
{
    // {row, bit, usage} for the 12 keys, digits first so rollover overflow
    // drops * / # last.
    static const struct { uint8_t row, bit, usage; } keys[12] = {
        {1, JAG_BIT_J11, HID_KEY_KEYPAD_1},
        {2, JAG_BIT_J11, HID_KEY_KEYPAD_1 + 1},   // 2
        {3, JAG_BIT_J11, HID_KEY_KEYPAD_1 + 2},   // 3
        {1, JAG_BIT_J10, HID_KEY_KEYPAD_1 + 3},   // 4
        {2, JAG_BIT_J10, HID_KEY_KEYPAD_1 + 4},   // 5
        {3, JAG_BIT_J10, HID_KEY_KEYPAD_1 + 5},   // 6
        {1, JAG_BIT_J9,  HID_KEY_KEYPAD_1 + 6},   // 7
        {2, JAG_BIT_J9,  HID_KEY_KEYPAD_1 + 7},   // 8
        {3, JAG_BIT_J9,  HID_KEY_KEYPAD_1 + 8},   // 9
        {2, JAG_BIT_J8,  HID_KEY_KEYPAD_0},
        {1, JAG_BIT_J8,  HID_KEY_KEYPAD_ASTERISK},
        {3, JAG_BIT_J8,  HID_KEY_KEYPAD_ENTER},   // '#'
    };

    int n = 0;
    for (int i = 0; i < 12 && n < 6; i++) {
        bool pro_button = (keys[i].bit == JAG_BIT_J9) ||                      // 7/8/9
                          (keys[i].bit == JAG_BIT_J10 && keys[i].row != 2);   // 4/6
        if (s_jag.pro_mode && pro_button) continue;   // emitted as X/Y/Z/L/R instead
        if (jag_key(keys[i].row, keys[i].bit)) {
            event->kb_keys[n++] = keys[i].usage;
        }
    }
}

// Hold Pause+Option 2s to toggle Pro Controller mode (not persisted).
static void jag_pro_toggle_task(uint64_t now, uint32_t buttons)
{
    bool combo = (buttons & JP_BUTTON_S2) && (buttons & JP_BUTTON_S1);

    if (!combo) {
        s_jag.combo_hold_start_us = 0;
        s_jag.combo_latched = false;
        return;
    }
    if (s_jag.combo_latched) return;

    if (s_jag.combo_hold_start_us == 0) {
        s_jag.combo_hold_start_us = now;
    } else if ((uint64_t)(now - s_jag.combo_hold_start_us) >= JAG_PRO_TOGGLE_HOLD_US) {
        s_jag.pro_mode = !s_jag.pro_mode;
        s_jag.combo_latched = true;
        printf("[jaguar_host] Pro Controller mode %s\n",
               s_jag.pro_mode ? "ON (kp 7/8/9/4/6 = X/Y/Z/L/R)" : "OFF (full numpad)");
    }
}

void jaguar_host_task(void)
{
    if (!s_jag.initialized) return;

    uint64_t now = time_us_64();
    if ((int64_t)(now - s_jag.next_poll_us) < 0) return;
    s_jag.next_poll_us = now + JAG_POLL_INTERVAL_US;

    jag_scan();

    // C2 (row2/B0) asserted = 6D/rotary controller identifying itself — not
    // decodable as a standard matrix, so hold neutral instead of garbage.
    bool foreign = jag_key(2, JAG_BIT_B0);
    if (foreign != s_jag.foreign_device) {
        s_jag.foreign_device = foreign;
        if (foreign) printf("[jaguar_host] Non-standard controller (C2 low, 6D/rotary?) — ignoring\n");
    }
    if (foreign) return;

    // Presence: pull-ups make an idle pad identical to an empty port, so we
    // latch "connected" on the first press and never disconnect (an unplug
    // just reads as idle/neutral forever, which is harmless).
    bool any_press = false;
    for (int r = 0; r < 4; r++) {
        if (s_jag.rows[r] != JAG_ROW_IDLE) { any_press = true; break; }
    }
    if (!s_jag.connected) {
        if (!any_press) return;
        s_jag.connected = true;
        printf("[jaguar_host] Controller connected\n");
    }

    uint32_t buttons = jag_decode_buttons();
    jag_pro_toggle_task(now, buttons);

    input_event_t event;
    init_input_event(&event);
    event.dev_addr = JAG_DEV_ADDR;
    event.instance = 0;
    event.type = INPUT_TYPE_GAMEPAD;
    event.transport = INPUT_TRANSPORT_NATIVE;
    event.layout = s_jag.pro_mode ? LAYOUT_SEGA_6BUTTON : LAYOUT_UNKNOWN;
    event.button_count = s_jag.pro_mode ? 6 : 3;
    event.buttons = buttons;
    jag_decode_keypad(&event);
    router_submit_input(&event);
}

bool jaguar_host_is_connected(void)
{
    return s_jag.connected;
}

bool jaguar_host_get_pro_mode(void)
{
    return s_jag.pro_mode;
}

void jaguar_host_set_pro_mode(bool enabled)
{
    s_jag.pro_mode = enabled;
}

// ============================================================================
// INPUT INTERFACE (for app declaration)
// ============================================================================

static uint8_t jag_get_device_count(void)
{
    return s_jag.connected ? 1 : 0;
}

const InputInterface jaguar_input_interface = {
    .name = "Jaguar",
    .source = INPUT_SOURCE_NATIVE_JAGUAR,
    .init = jaguar_host_init,
    .task = jaguar_host_task,
    .is_connected = jaguar_host_is_connected,
    .get_device_count = jag_get_device_count,
};
