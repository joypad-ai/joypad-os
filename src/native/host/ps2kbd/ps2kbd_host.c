// ps2kbd_host.c - Native PS/2 Keyboard Host Driver
//
// PIO captures 11-bit PS/2 frames from the keyboard. Each frame carries one byte of
// scan-code data (Set 2). The decoder tracks E0/F0 prefixes to distinguish extended
// keys and break (release) events, translates Set 2 to USB HID usage IDs, and
// maintains a 6-key-rollover pressed-key set. Whenever that set changes the driver
// rebuilds the gamepad button bitmap and analog-stick assignments (WASD + arrows)
// and submits a router input event.

#include "ps2kbd_host.h"
#include "ps2kbd.pio.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include "tusb.h"

#include "hardware/pio.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// CONFIG
// ============================================================================

// Native device address reserved for this host (kept clear of USB 1-127 and other
// native hosts like SNES/LodgeNet which use 0xF0+).
#define PS2KBD_DEV_ADDR   0xE0

// 6-key-rollover matches USB HID keyboard reports.
#define PS2KBD_MAX_KEYS   6

// Analog stick intensity when WASD / arrow keys are used as a stick.
#define PS2KBD_STICK_FULL 255

// ============================================================================
// SCAN CODE TRANSLATION TABLES (PS/2 Set 2 -> USB HID usage ID)
// ============================================================================
//
// Non-zero entries are valid translations; zero means "no mapping" (key ignored).
// Modifier keys translate to the special sentinel values below so the decoder can
// fold them into the modifier byte instead of the keycode slots.

#define PS2_MOD_SENTINEL  0x80  // top bit: "this HID value is actually a modifier mask"
#define MOD_LCTRL         (PS2_MOD_SENTINEL | 0x01)
#define MOD_LSHIFT        (PS2_MOD_SENTINEL | 0x02)
#define MOD_LALT          (PS2_MOD_SENTINEL | 0x04)
#define MOD_LGUI          (PS2_MOD_SENTINEL | 0x08)
#define MOD_RCTRL         (PS2_MOD_SENTINEL | 0x10)
#define MOD_RSHIFT        (PS2_MOD_SENTINEL | 0x20)
#define MOD_RALT          (PS2_MOD_SENTINEL | 0x40)
#define MOD_RGUI          (PS2_MOD_SENTINEL | 0x80)

static const uint8_t ps2_set2_to_hid[256] = {
    // Row 0x00
    [0x01] = HID_KEY_F9,
    [0x03] = HID_KEY_F5,
    [0x04] = HID_KEY_F3,
    [0x05] = HID_KEY_F1,
    [0x06] = HID_KEY_F2,
    [0x07] = HID_KEY_F12,
    [0x09] = HID_KEY_F10,
    [0x0A] = HID_KEY_F8,
    [0x0B] = HID_KEY_F6,
    [0x0C] = HID_KEY_F4,
    [0x0D] = HID_KEY_TAB,
    [0x0E] = HID_KEY_GRAVE,

    [0x11] = MOD_LALT,
    [0x12] = MOD_LSHIFT,
    [0x14] = MOD_LCTRL,
    [0x15] = HID_KEY_Q,
    [0x16] = HID_KEY_1,

    [0x1A] = HID_KEY_Z,
    [0x1B] = HID_KEY_S,
    [0x1C] = HID_KEY_A,
    [0x1D] = HID_KEY_W,
    [0x1E] = HID_KEY_2,

    [0x21] = HID_KEY_C,
    [0x22] = HID_KEY_X,
    [0x23] = HID_KEY_D,
    [0x24] = HID_KEY_E,
    [0x25] = HID_KEY_4,
    [0x26] = HID_KEY_3,
    [0x29] = HID_KEY_SPACE,
    [0x2A] = HID_KEY_V,
    [0x2B] = HID_KEY_F,
    [0x2C] = HID_KEY_T,
    [0x2D] = HID_KEY_R,
    [0x2E] = HID_KEY_5,

    [0x31] = HID_KEY_N,
    [0x32] = HID_KEY_B,
    [0x33] = HID_KEY_H,
    [0x34] = HID_KEY_G,
    [0x35] = HID_KEY_Y,
    [0x36] = HID_KEY_6,

    [0x3A] = HID_KEY_M,
    [0x3B] = HID_KEY_J,
    [0x3C] = HID_KEY_U,
    [0x3D] = HID_KEY_7,
    [0x3E] = HID_KEY_8,

    [0x41] = HID_KEY_COMMA,
    [0x42] = HID_KEY_K,
    [0x43] = HID_KEY_I,
    [0x44] = HID_KEY_O,
    [0x45] = HID_KEY_0,
    [0x46] = HID_KEY_9,
    [0x49] = HID_KEY_PERIOD,
    [0x4A] = HID_KEY_SLASH,
    [0x4B] = HID_KEY_L,
    [0x4C] = HID_KEY_SEMICOLON,
    [0x4D] = HID_KEY_P,
    [0x4E] = HID_KEY_MINUS,

    [0x52] = HID_KEY_APOSTROPHE,
    [0x54] = HID_KEY_BRACKET_LEFT,
    [0x55] = HID_KEY_EQUAL,
    [0x58] = HID_KEY_CAPS_LOCK,
    [0x59] = MOD_RSHIFT,
    [0x5A] = HID_KEY_ENTER,
    [0x5B] = HID_KEY_BRACKET_RIGHT,
    [0x5D] = HID_KEY_BACKSLASH,

    [0x66] = HID_KEY_BACKSPACE,
    [0x76] = HID_KEY_ESCAPE,
    [0x78] = HID_KEY_F11,
    [0x83] = HID_KEY_F7,
};

// Extended scan codes follow the 0xE0 prefix. Only entries we actually care about;
// anything else is ignored.
static const uint8_t ps2_set2_e0_to_hid[256] = {
    [0x11] = MOD_RALT,
    [0x14] = MOD_RCTRL,
    [0x1F] = MOD_LGUI,
    [0x27] = MOD_RGUI,
    [0x69] = HID_KEY_END,
    [0x6B] = HID_KEY_ARROW_LEFT,
    [0x6C] = HID_KEY_HOME,
    [0x70] = HID_KEY_INSERT,
    [0x71] = HID_KEY_DELETE,
    [0x72] = HID_KEY_ARROW_DOWN,
    [0x74] = HID_KEY_ARROW_RIGHT,
    [0x75] = HID_KEY_ARROW_UP,
    [0x7A] = HID_KEY_PAGE_DOWN,
    [0x7D] = HID_KEY_PAGE_UP,
};

// ============================================================================
// INTERNAL STATE
// ============================================================================

// PIO resources
static PIO pio_hw;
static uint pio_sm;
static uint pio_offset;
static bool initialized = false;

// Pressed-key state (USB HID-compatible)
static uint8_t keys_down[PS2KBD_MAX_KEYS];
static uint8_t modifier;

// Decoder prefix state (only one of these is true between frames)
static bool pending_extended = false;   // last byte was 0xE0
static bool pending_break    = false;   // last byte was 0xF0 (or E0 F0)

// Keyboard presence — set true once the self-test pass byte (0xAA) is seen.
static bool keyboard_present = false;

// Cached router output so we only submit events when state changes.
static uint32_t last_buttons      = 0;
static uint8_t  last_analog_lx    = 128;
static uint8_t  last_analog_ly    = 128;
static uint8_t  last_analog_rx    = 128;
static uint8_t  last_analog_ry    = 128;
static bool     last_submitted    = false;

// ============================================================================
// KEY TABLE HELPERS
// ============================================================================

static int find_key(uint8_t hid_code) {
    for (int i = 0; i < PS2KBD_MAX_KEYS; i++) {
        if (keys_down[i] == hid_code) return i;
    }
    return -1;
}

static void add_key(uint8_t hid_code) {
    if (find_key(hid_code) >= 0) return;  // already down
    for (int i = 0; i < PS2KBD_MAX_KEYS; i++) {
        if (keys_down[i] == 0) {
            keys_down[i] = hid_code;
            return;
        }
    }
    // rollover — ignore further keys until something releases
}

static void remove_key(uint8_t hid_code) {
    int idx = find_key(hid_code);
    if (idx < 0) return;
    keys_down[idx] = 0;
}

static bool key_held(uint8_t hid_code) {
    return find_key(hid_code) >= 0;
}

// ============================================================================
// FRAME DECODE
// ============================================================================

// Process one data byte (after PIO has stripped start/parity/stop bits).
// Returns true if the pressed-key set changed and the caller should rebuild
// and submit an event.
static bool process_scan_byte(uint8_t byte) {
    // Special bytes from keyboard
    if (byte == 0xAA) {
        // Self-test passed after reset. Clear all state.
        memset(keys_down, 0, sizeof(keys_down));
        modifier = 0;
        pending_extended = false;
        pending_break = false;
        keyboard_present = true;
        return true;
    }

    if (byte == 0xE0) {
        pending_extended = true;
        return false;
    }

    if (byte == 0xF0) {
        pending_break = true;
        return false;
    }

    // Regular make/break code — consume prefix state
    bool is_break = pending_break;
    bool is_ext   = pending_extended;
    pending_break = false;
    pending_extended = false;

    uint8_t hid = is_ext ? ps2_set2_e0_to_hid[byte] : ps2_set2_to_hid[byte];
    if (hid == 0) return false;  // unmapped — ignore

    if (hid & PS2_MOD_SENTINEL) {
        uint8_t mask = hid & 0x7F;
        if (is_break) modifier &= ~mask;
        else          modifier |= mask;
        return true;
    }

    if (is_break) remove_key(hid);
    else          add_key(hid);
    return true;
}

// ============================================================================
// KEY-TO-GAMEPAD MAPPING
// ============================================================================
//
// Canonical mapping — mirrors USB HID keyboard behavior (see hid_keyboard.c):
//   Esc / =       -> Start   (S2)
//   P  / -        -> Select  (S1)
//   J  / Enter    -> B1
//   K  / Backspace-> B2
//   ;             -> B3
//   L             -> B4
//   U  / PgUp     -> L2
//   I  / PgDn     -> R2
//   [             -> L1
//   ]             -> R1
//   V             -> L3
//   N             -> R3
//   Ctrl+Alt+Del  -> A1 (Home/Guide)
//   Arrows        -> D-pad
//   WASD          -> Left stick (4-way)
//   M , . /       -> Right stick (4-way)
//
// Simpler than USB HID keyboard's 8-way stroke logic — only 4 cardinal directions
// are emitted. If both opposite keys are held, neither wins (centered).

static void rebuild_and_submit(void) {
    uint32_t buttons = 0;
    uint8_t lx = 128, ly = 128;
    uint8_t rx = 128, ry = 128;

    // Face / shoulder / system buttons
    if (key_held(HID_KEY_ESCAPE) || key_held(HID_KEY_EQUAL))      buttons |= JP_BUTTON_S2;
    if (key_held(HID_KEY_P)      || key_held(HID_KEY_MINUS))      buttons |= JP_BUTTON_S1;
    if (key_held(HID_KEY_J)      || key_held(HID_KEY_ENTER))      buttons |= JP_BUTTON_B1;
    if (key_held(HID_KEY_K)      || key_held(HID_KEY_BACKSPACE))  buttons |= JP_BUTTON_B2;
    if (key_held(HID_KEY_SEMICOLON))                              buttons |= JP_BUTTON_B3;
    if (key_held(HID_KEY_L))                                      buttons |= JP_BUTTON_B4;
    if (key_held(HID_KEY_U)      || key_held(HID_KEY_PAGE_UP))    buttons |= JP_BUTTON_L2;
    if (key_held(HID_KEY_I)      || key_held(HID_KEY_PAGE_DOWN))  buttons |= JP_BUTTON_R2;
    if (key_held(HID_KEY_BRACKET_LEFT))                           buttons |= JP_BUTTON_L1;
    if (key_held(HID_KEY_BRACKET_RIGHT))                          buttons |= JP_BUTTON_R1;
    if (key_held(HID_KEY_V))                                      buttons |= JP_BUTTON_L3;
    if (key_held(HID_KEY_N))                                      buttons |= JP_BUTTON_R3;

    // D-pad from arrow keys
    if (key_held(HID_KEY_ARROW_UP))    buttons |= JP_BUTTON_DU;
    if (key_held(HID_KEY_ARROW_DOWN))  buttons |= JP_BUTTON_DD;
    if (key_held(HID_KEY_ARROW_LEFT))  buttons |= JP_BUTTON_DL;
    if (key_held(HID_KEY_ARROW_RIGHT)) buttons |= JP_BUTTON_DR;

    // Ctrl+Alt+Delete -> Home/Guide (modifier byte uses standard USB HID bit positions)
    bool ctrl = modifier & 0x11;  // bit 0 (LCtrl) | bit 4 (RCtrl)
    bool alt  = modifier & 0x44;  // bit 2 (LAlt)  | bit 6 (RAlt)
    if (ctrl && alt && key_held(HID_KEY_DELETE)) buttons |= JP_BUTTON_A1;

    // Left stick from WASD (cardinal only — opposite keys cancel)
    bool w = key_held(HID_KEY_W);
    bool s = key_held(HID_KEY_S);
    bool a = key_held(HID_KEY_A);
    bool d = key_held(HID_KEY_D);
    if (w && !s) ly = 0;
    else if (s && !w) ly = PS2KBD_STICK_FULL;
    if (a && !d) lx = 0;
    else if (d && !a) lx = PS2KBD_STICK_FULL;

    // Right stick from M/,/./ (mirrors hid_keyboard.c mapping)
    bool m_up    = key_held(HID_KEY_M);
    bool m_down  = key_held(HID_KEY_PERIOD);
    bool m_left  = key_held(HID_KEY_COMMA);
    bool m_right = key_held(HID_KEY_SLASH);
    if (m_up && !m_down)   ry = 0;
    else if (m_down && !m_up) ry = PS2KBD_STICK_FULL;
    if (m_left && !m_right)  rx = 0;
    else if (m_right && !m_left) rx = PS2KBD_STICK_FULL;

    // Suppress duplicate submissions
    if (last_submitted &&
        buttons == last_buttons &&
        lx == last_analog_lx && ly == last_analog_ly &&
        rx == last_analog_rx && ry == last_analog_ry) {
        return;
    }
    last_buttons   = buttons;
    last_analog_lx = lx;
    last_analog_ly = ly;
    last_analog_rx = rx;
    last_analog_ry = ry;
    last_submitted = true;

    // Pack HID keycodes into 'keys' field (up to 4 bytes). Useful for outputs that
    // want raw key events (e.g. USB HID keyboard passthrough) rather than the
    // gamepad bitmap.
    uint32_t keys = 0;
    for (int i = 0; i < PS2KBD_MAX_KEYS && i < 4; i++) {
        if (keys_down[i]) keys |= ((uint32_t)keys_down[i]) << (i * 8);
    }

    input_event_t event;
    init_input_event(&event);
    event.dev_addr  = PS2KBD_DEV_ADDR;
    event.instance  = 0;
    event.type      = INPUT_TYPE_KEYBOARD;
    event.transport = INPUT_TRANSPORT_NATIVE;
    event.layout    = LAYOUT_UNKNOWN;
    event.buttons   = buttons;
    event.keys      = keys;
    event.analog[ANALOG_LX] = lx;
    event.analog[ANALOG_LY] = ly;
    event.analog[ANALOG_RX] = rx;
    event.analog[ANALOG_RY] = ry;

    router_submit_input(&event);
}

// ============================================================================
// PIO SETUP
// ============================================================================

static void ps2kbd_pio_init(uint8_t pin_data) {
    // Prefer PIO1 to stay out of PIO0 where NeoPixel / PIO-USB live on some boards.
    int sm = pio_claim_unused_sm(pio1, false);
    if (sm >= 0) {
        pio_hw = pio1;
        pio_sm = (uint)sm;
    } else {
        pio_hw = pio0;
        pio_sm = pio_claim_unused_sm(pio_hw, true);
    }

    pio_offset = pio_add_program(pio_hw, &ps2kbd_program);
    ps2kbd_program_init(pio_hw, pio_sm, pio_offset, pin_data);
}

// ============================================================================
// PUBLIC API
// ============================================================================

void ps2kbd_host_init(void) {
    ps2kbd_host_init_pin(PS2KBD_PIN_DATA);
}

void ps2kbd_host_init_pin(uint8_t pin_data) {
    if (initialized) return;

    printf("[ps2kbd] Initialising PS/2 keyboard host DATA=GP%u CLOCK=GP%u\n",
           pin_data, pin_data + 1);

    memset(keys_down, 0, sizeof(keys_down));
    modifier = 0;
    pending_extended = false;
    pending_break = false;
    keyboard_present = false;
    last_submitted = false;

    ps2kbd_pio_init(pin_data);
    initialized = true;

    printf("[ps2kbd] ready PIO%d SM%d\n", pio_get_index(pio_hw), pio_sm);
}

void ps2kbd_host_task(void) {
    if (!initialized) return;

    bool state_changed = false;

    // Drain any complete frames from the FIFO.
    while (!pio_sm_is_rx_fifo_empty(pio_hw, pio_sm)) {
        uint32_t frame = pio_sm_get(pio_hw, pio_sm);

        // Auto-push with 11-bit threshold and shift-right places the frame in the
        // upper bits of the 32-bit FIFO word. Slide it down to expose the 11-bit
        // payload.
        frame >>= (32 - 11);

        // Frame layout (LSB-first from keyboard):
        //   bit  0      : start (always 0)
        //   bits 1..8   : data bits (LSB first)
        //   bit  9      : odd parity
        //   bit  10     : stop (always 1)
        uint8_t data  = (frame >> 1) & 0xFF;
        uint8_t start = frame & 0x01;
        uint8_t stop  = (frame >> 10) & 0x01;
        uint8_t par   = (frame >> 9) & 0x01;

        // Verify framing. Odd parity: data bits XOR'd with parity bit must be odd.
        uint8_t bits = data;
        uint8_t ones = 0;
        while (bits) { ones += bits & 1; bits >>= 1; }
        ones += par;
        if (start != 0 || stop != 1 || (ones & 1) == 0) {
            // Framing error — discard. A real-world keyboard recovers on the next
            // frame so there's no need to reset.
            continue;
        }

        if (process_scan_byte(data)) state_changed = true;
    }

    if (state_changed) rebuild_and_submit();
}

bool ps2kbd_host_is_connected(void) {
    return keyboard_present;
}

// ============================================================================
// INPUT INTERFACE
// ============================================================================

static uint8_t ps2kbd_get_device_count(void) {
    return keyboard_present ? 1 : 0;
}

const InputInterface ps2kbd_input_interface = {
    .name = "PS/2 Keyboard",
    .source = INPUT_SOURCE_NATIVE_PS2KBD,
    .init = ps2kbd_host_init,
    .task = ps2kbd_host_task,
    .is_connected = ps2kbd_host_is_connected,
    .get_device_count = ps2kbd_get_device_count,
};
