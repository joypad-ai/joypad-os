// switch_pro_bt.c - Nintendo Switch Pro Controller Bluetooth Driver
// Handles Switch Pro and Joy-Con controllers over Bluetooth
//
// Reference: https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering

#include "switch_pro_bt.h"
#include "bt/bthid/bthid.h"
#include "bt/transport/bt_transport.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "platform/platform.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// SWITCH PRO CONSTANTS
// ============================================================================

// Report IDs
#define SWITCH_REPORT_INPUT_STANDARD    0x30    // Standard full input report
#define SWITCH_REPORT_INPUT_SIMPLE      0x3F    // Simple HID mode
#define SWITCH_REPORT_OUTPUT            0x01    // Output report with subcommand
#define SWITCH_REPORT_RUMBLE_ONLY       0x10    // Rumble only (no subcommand)

// Subcommands
#define SWITCH_SUBCMD_SET_INPUT_MODE    0x03
#define SWITCH_SUBCMD_SET_PLAYER_LED    0x30
#define SWITCH_SUBCMD_SET_HOME_LED      0x38
#define SWITCH_SUBCMD_ENABLE_IMU        0x40
#define SWITCH_SUBCMD_ENABLE_VIBRATION  0x48

// Input modes
#define SWITCH_INPUT_MODE_FULL          0x30

// Init delay between subcommands (ms)
#define SWITCH_INIT_DELAY_MS            200

// ============================================================================
// SWITCH PRO REPORT STRUCTURE
// ============================================================================

typedef struct __attribute__((packed)) {
    uint8_t report_id;      // 0x30 or 0x3F
    uint8_t timer;          // Increments by 1 per report

    uint8_t battery_conn;   // Battery level + connection info

    // Button byte 1 (right side buttons)
    struct {
        uint8_t y    : 1;
        uint8_t x    : 1;
        uint8_t b    : 1;
        uint8_t a    : 1;
        uint8_t sr_r : 1;
        uint8_t sl_r : 1;
        uint8_t r    : 1;
        uint8_t zr   : 1;
    };

    // Button byte 2 (system buttons)
    struct {
        uint8_t minus  : 1;
        uint8_t plus   : 1;
        uint8_t rstick : 1;
        uint8_t lstick : 1;
        uint8_t home   : 1;
        uint8_t capture: 1;
        uint8_t pad1   : 2;
    };

    // Button byte 3 (left side buttons + dpad)
    struct {
        uint8_t down  : 1;
        uint8_t up    : 1;
        uint8_t right : 1;
        uint8_t left  : 1;
        uint8_t sr_l  : 1;
        uint8_t sl_l  : 1;
        uint8_t l     : 1;
        uint8_t zl    : 1;
    };

    // Analog sticks (12-bit packed, 3 bytes each)
    uint8_t left_stick[3];
    uint8_t right_stick[3];

    // Vibration ack and subcommand data follow...
} switch_input_report_t;

// Simple HID report (0x3F) - used before handshake
typedef struct __attribute__((packed)) {
    uint8_t report_id;      // 0x3F

    struct {
        uint8_t b      : 1;
        uint8_t a      : 1;
        uint8_t y      : 1;
        uint8_t x      : 1;
        uint8_t l      : 1;
        uint8_t r      : 1;
        uint8_t zl     : 1;
        uint8_t zr     : 1;
    };

    struct {
        uint8_t minus  : 1;
        uint8_t plus   : 1;
        uint8_t lstick : 1;
        uint8_t rstick : 1;
        uint8_t home   : 1;
        uint8_t capture: 1;
        uint8_t pad    : 2;
    };

    uint8_t hat;            // D-pad as hat (0-7, 8=center)
    uint16_t lx, ly;        // Left stick (0-65535, center ~32768)
    uint16_t rx, ry;        // Right stick (0-65535, center ~32768)
} switch_simple_report_t;

// ============================================================================
// INIT STATE MACHINE
// ============================================================================

typedef enum {
    SWITCH_STATE_WAIT_READY,        // Wait before sending first subcommand
    SWITCH_STATE_SET_INPUT_MODE,    // Send set input mode (0x03 → 0x30)
    SWITCH_STATE_ENABLE_VIBRATION,  // Send enable vibration (0x48 → 0x01)
    SWITCH_STATE_SET_PLAYER_LED,    // Send player LED (0x30)
    SWITCH_STATE_ACTIVE,            // Init complete, monitor feedback
} switch_init_state_t;

// ============================================================================
// CONTROLLER MODEL
// ============================================================================

// The NSO retro pads (SNES / NES / N64 / Genesis) speak the same Switch Pro
// protocol and need the same 0x30 handshake, but they place their physical
// buttons on different bits of the report. They are identified by name — over
// classic BT the VID/PID often is not available when a driver is picked.
typedef enum {
    SWITCH_MODEL_PRO = 0,   // Pro Controller / Joy-Con — standard layout
    SWITCH_MODEL_NSO_SNES,  // "SNES Controller"
    SWITCH_MODEL_NSO_NES,   // "NES Controller" / "HVC Controller"
    SWITCH_MODEL_NSO_N64,   // "N64 Controller"
    SWITCH_MODEL_NSO_MD,    // "MD/Gen Control Pad"
} switch_model_t;

// SNES/NES/Genesis pads have no analog sticks; the N64 pad has a left stick only.
static inline bool switch_model_has_sticks(switch_model_t model)
{
    return model == SWITCH_MODEL_PRO || model == SWITCH_MODEL_NSO_N64;
}

static switch_model_t switch_model_from_name(const char* name)
{
    if (!name) return SWITCH_MODEL_PRO;

    // Order matters: "SNES Controller" contains "NES Controller", so the SNES
    // test has to come first.
    if (strstr(name, "SNES Controller") != NULL) return SWITCH_MODEL_NSO_SNES;
    if (strstr(name, "N64 Controller") != NULL)  return SWITCH_MODEL_NSO_N64;
    if (strstr(name, "MD/Gen") != NULL)          return SWITCH_MODEL_NSO_MD;
    // "HVC Controller" is the Japanese Famicom variant of the NES pad.
    if (strstr(name, "NES Controller") != NULL ||
        strstr(name, "HVC Controller") != NULL)  return SWITCH_MODEL_NSO_NES;

    return SWITCH_MODEL_PRO;
}

static const char* switch_model_str(switch_model_t model)
{
    switch (model) {
        case SWITCH_MODEL_NSO_SNES: return "NSO SNES";
        case SWITCH_MODEL_NSO_NES:  return "NSO NES";
        case SWITCH_MODEL_NSO_N64:  return "NSO N64";
        case SWITCH_MODEL_NSO_MD:   return "NSO MD/Genesis";
        default:                    return "Pro/Joy-Con";
    }
}

// ============================================================================
// DRIVER DATA
// ============================================================================

typedef struct {
    input_event_t event;
    bool initialized;
    bool full_report_mode;
    uint8_t output_seq;     // Sequence counter for output reports
    switch_init_state_t init_state;
    uint32_t init_time;     // Timestamp for init delays
    uint8_t rumble_left;    // Cached rumble state
    uint8_t rumble_right;
    switch_model_t model;   // Physical pad — selects the button decode
} switch_bt_data_t;

static switch_bt_data_t switch_data[BTHID_MAX_DEVICES];

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Unpack 12-bit analog value from 3-byte packed format
static uint16_t unpack_stick_12bit(const uint8_t* data, bool high)
{
    if (high) {
        // High nibble of byte 1 + all of byte 2
        return ((data[1] & 0xF0) >> 4) | (data[2] << 4);
    } else {
        // All of byte 0 + low nibble of byte 1
        return data[0] | ((data[1] & 0x0F) << 8);
    }
}

// Scale 12-bit to 8-bit
static uint8_t scale_12bit_to_8bit(uint16_t val)
{
    if (val == 0) return 1;
    return 1 + ((val * 254) / 4095);
}

// Encode rumble intensity to Switch rumble format (from USB Switch Pro driver)
// Each motor uses 4 bytes: [amplitude, HF_freq, amplitude/2, LF_freq]
// Neutral state: [00 01 40 40]
static void encode_rumble(uint8_t intensity, uint8_t* out)
{
    if (intensity == 0) {
        out[0] = 0x00;
        out[1] = 0x01;
        out[2] = 0x40;
        out[3] = 0x40;
        return;
    }
    uint16_t scaled = ((uint16_t)intensity * 102) / 255 + 64;
    uint8_t amplitude = (uint8_t)(scaled + 64);
    out[0] = amplitude;
    out[1] = 0x88;
    out[2] = amplitude / 2;
    out[3] = 0x61;
}

static void switch_send_subcommand(bthid_device_t* device, uint8_t subcmd,
                                    const uint8_t* data, uint8_t len)
{
    switch_bt_data_t* sw = (switch_bt_data_t*)device->driver_data;
    if (!sw) return;

    uint8_t buf[48];
    memset(buf, 0, sizeof(buf));

    buf[0] = sw->output_seq++ & 0x0F;

    // Neutral rumble data (8 bytes)
    buf[1] = 0x00; buf[2] = 0x01; buf[3] = 0x40; buf[4] = 0x40;
    buf[5] = 0x00; buf[6] = 0x01; buf[7] = 0x40; buf[8] = 0x40;

    buf[9] = subcmd;
    if (data && len > 0 && len < 38) {
        memcpy(&buf[10], data, len);
    }

    bthid_send_output_report(device->conn_index, SWITCH_REPORT_OUTPUT, buf, 10 + len);
}

static void switch_send_rumble(bthid_device_t* device, uint8_t left, uint8_t right)
{
    switch_bt_data_t* sw = (switch_bt_data_t*)device->driver_data;
    if (!sw) return;

    uint8_t buf[9];
    buf[0] = sw->output_seq++ & 0x0F;
    encode_rumble(left, &buf[1]);
    encode_rumble(right, &buf[5]);

    bthid_send_output_report(device->conn_index, SWITCH_REPORT_RUMBLE_ONLY, buf, 9);
}

// ============================================================================
// DRIVER IMPLEMENTATION
// ============================================================================

static bool switch_match(const char* device_name, const uint8_t* class_of_device,
                         uint16_t vendor_id, uint16_t product_id, bool is_ble)
{
    (void)class_of_device;
    (void)is_ble;

    // Match Switch 1 controllers by VID/PID
    // Nintendo VID = 0x057E
    // Switch 1 PIDs: Joy-Con L = 0x2006, Joy-Con R = 0x2007, Pro Controller = 0x2009
    // Do NOT match Switch 2 PIDs (0x2066, 0x2067, 0x2069, 0x2073) - handled by switch2_ble
    if (vendor_id == 0x057E) {
        switch (product_id) {
            case 0x2006:  // Joy-Con L
            case 0x2007:  // Joy-Con R
            case 0x2009:  // Pro Controller
            case 0x2017:  // SNES Controller (NSO) — same PID the USB host side uses
                return true;
        }
        // Don't return true for unknown Nintendo PIDs
        // Let specific drivers handle them
    }

    // Name-based match (fallback for classic BT where VID/PID may be unavailable).
    // The NSO retro pads only ever advertise a name on the classic-BT path, so
    // this is the match that actually fires for them in practice.
    if (device_name) {
        if (strstr(device_name, "Pro Controller") != NULL) {
            return true;
        }
        if (strstr(device_name, "Joy-Con") != NULL) {
            return true;
        }
        if (switch_model_from_name(device_name) != SWITCH_MODEL_PRO) {
            return true;
        }
    }

    return false;
}

static bool switch_init(bthid_device_t* device)
{
    switch_model_t model = switch_model_from_name(device->name);

    printf("[SWITCH_BT] Init for device: %s (model: %s)\n",
           device->name, switch_model_str(model));

    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (!switch_data[i].initialized) {
            init_input_event(&switch_data[i].event);
            switch_data[i].initialized = true;
            switch_data[i].model = model;
            switch_data[i].full_report_mode = false;
            switch_data[i].output_seq = 0;
            switch_data[i].rumble_left = 0;
            switch_data[i].rumble_right = 0;

            // Start init state machine — commands sent from task()
            switch_data[i].init_state = SWITCH_STATE_WAIT_READY;
            switch_data[i].init_time = platform_time_ms();

            switch_data[i].event.type = INPUT_TYPE_GAMEPAD;
            switch_data[i].event.transport = INPUT_TRANSPORT_BT_CLASSIC;
            switch_data[i].event.dev_addr = device->conn_index;
            switch_data[i].event.instance = 0;
            switch_data[i].event.button_count = 10;

            device->driver_data = &switch_data[i];
            return true;
        }
    }

    return false;
}

// Report bits normalised across the 0x30 and 0x3F layouts. The two reports use
// different bit orders, but once unpacked into named fields the NSO pads land on
// the same fields in both — so one mapping table covers both paths.
typedef struct {
    uint8_t b, a, y, x;
    uint8_t l, r, zl, zr;
    uint8_t minus, plus, lstick, rstick, home, capture;
} switch_raw_btns_t;

// Map an NSO retro pad's raw report bits onto JoypadOS buttons.
//
// The NSO pads reuse the Switch Pro report format but put their physical buttons
// on unrelated bits. Bit assignments cross-checked against BlueRetro's Switch
// tables (main/adapter/wireless/sw.c), which are validated against real hardware.
//
// Returns the button mask; for the N64 pad the C-button cluster is synthesised
// onto the right stick via out_rx/out_ry, since it sits where the right stick
// would be and every output path already knows how to read a stick.
static uint32_t switch_map_nso_buttons(switch_model_t model, const switch_raw_btns_t* n,
                                       uint8_t* out_rx, uint8_t* out_ry)
{
    uint32_t buttons = 0;

    // Common across every NSO pad
    if (n->plus)    buttons |= JP_BUTTON_S2;   // Start
    if (n->home)    buttons |= JP_BUTTON_A1;
    if (n->capture) buttons |= JP_BUTTON_A2;

    switch (model) {
        case SWITCH_MODEL_NSO_N64:
            if (n->a)      buttons |= JP_BUTTON_B1;  // N64 A (bottom)
            if (n->b)      buttons |= JP_BUTTON_B3;  // N64 B (left of A)
            if (n->l)      buttons |= JP_BUTTON_L1;  // N64 L
            if (n->r)      buttons |= JP_BUTTON_R1;  // N64 R
            if (n->zl)     buttons |= JP_BUTTON_L2;  // N64 Z (underside trigger)
            if (n->lstick) buttons |= JP_BUTTON_R2;  // NSO-only right-grip ZR

            // C-button cluster → right stick. HID convention: up = low.
            if (n->x)     *out_rx = 0;    // C-Left
            if (n->minus) *out_rx = 255;  // C-Right
            if (n->y)     *out_ry = 0;    // C-Up
            if (n->zr)    *out_ry = 255;  // C-Down
            break;

        case SWITCH_MODEL_NSO_MD:
            // Genesis 6-button face: A B C on the bottom row, X Y Z on the top.
            if (n->b) buttons |= JP_BUTTON_B1;  // B (bottom)
            if (n->r) buttons |= JP_BUTTON_B2;  // C (right)
            if (n->a) buttons |= JP_BUTTON_B3;  // A (left)
            if (n->y) buttons |= JP_BUTTON_B4;  // Y (top)
            // X reports on the X bit in the 0x30 report and on the ZL bit in
            // 0x3F. Neither bit carries anything else on this pad, so OR them
            // rather than branching on report type.
            if (n->x || n->zl) buttons |= JP_BUTTON_L1;  // X
            if (n->l)          buttons |= JP_BUTTON_R1;  // Z
            if (n->zr)         buttons |= JP_BUTTON_S1;  // Mode
            break;

        case SWITCH_MODEL_NSO_SNES:
        case SWITCH_MODEL_NSO_NES:
        default:
            // SNES and NES pads keep the standard Nintendo face/shoulder bits —
            // the standard decode is already correct for them.
            if (n->b)     buttons |= JP_BUTTON_B1;
            if (n->a)     buttons |= JP_BUTTON_B2;
            if (n->y)     buttons |= JP_BUTTON_B3;
            if (n->x)     buttons |= JP_BUTTON_B4;
            if (n->l)     buttons |= JP_BUTTON_L1;
            if (n->r)     buttons |= JP_BUTTON_R1;
            if (n->zl)    buttons |= JP_BUTTON_L2;
            if (n->zr)    buttons |= JP_BUTTON_R2;
            if (n->minus) buttons |= JP_BUTTON_S1;
            break;
    }

    return buttons;
}

static void switch_process_report(bthid_device_t* device, const uint8_t* data, uint16_t len)
{
    switch_bt_data_t* sw = (switch_bt_data_t*)device->driver_data;
    if (!sw || len < 1) return;

    uint8_t report_id = data[0];

    if (report_id == SWITCH_REPORT_INPUT_STANDARD && len >= 13) {
        // Full input report (0x30)
        const switch_input_report_t* rpt = (const switch_input_report_t*)data;

        sw->full_report_mode = true;

        // Build button state
        uint32_t buttons = 0x00000000;
        uint8_t nso_rx = 128, nso_ry = 128;

        if (sw->model != SWITCH_MODEL_PRO) {
            const switch_raw_btns_t n = {
                .b = rpt->b, .a = rpt->a, .y = rpt->y, .x = rpt->x,
                .l = rpt->l, .r = rpt->r, .zl = rpt->zl, .zr = rpt->zr,
                .minus = rpt->minus, .plus = rpt->plus,
                .lstick = rpt->lstick, .rstick = rpt->rstick,
                .home = rpt->home, .capture = rpt->capture,
            };
            buttons = switch_map_nso_buttons(sw->model, &n, &nso_rx, &nso_ry);
        } else {
            // Face buttons (map by position, not label — Nintendo layout is rotated)
            if (rpt->b)      buttons |= JP_BUTTON_B1;  // B = bottom
            if (rpt->a)      buttons |= JP_BUTTON_B2;  // A = right
            if (rpt->y)      buttons |= JP_BUTTON_B3;  // Y = left
            if (rpt->x)      buttons |= JP_BUTTON_B4;  // X = top

            // Shoulder buttons
            if (rpt->l)      buttons |= JP_BUTTON_L1;
            if (rpt->r)      buttons |= JP_BUTTON_R1;
            if (rpt->zl)     buttons |= JP_BUTTON_L2;
            if (rpt->zr)     buttons |= JP_BUTTON_R2;

            // System buttons
            if (rpt->minus)  buttons |= JP_BUTTON_S1;
            if (rpt->plus)   buttons |= JP_BUTTON_S2;
            if (rpt->lstick) buttons |= JP_BUTTON_L3;
            if (rpt->rstick) buttons |= JP_BUTTON_R3;
            if (rpt->home)   buttons |= JP_BUTTON_A1;
            if (rpt->capture) buttons |= JP_BUTTON_A2;
        }

        // D-pad — same bits on every model
        if (rpt->up)     buttons |= JP_BUTTON_DU;
        if (rpt->down)   buttons |= JP_BUTTON_DD;
        if (rpt->left)   buttons |= JP_BUTTON_DL;
        if (rpt->right)  buttons |= JP_BUTTON_DR;

        sw->event.buttons = buttons;

        if (switch_model_has_sticks(sw->model)) {
            // Unpack 12-bit sticks
            uint16_t lx = unpack_stick_12bit(rpt->left_stick, false);
            uint16_t ly = unpack_stick_12bit(rpt->left_stick, true);

            // Scale to 8-bit and invert Y (Nintendo: up=high, HID: up=low)
            sw->event.analog[ANALOG_LX] = scale_12bit_to_8bit(lx);
            sw->event.analog[ANALOG_LY] = 255 - scale_12bit_to_8bit(ly);
        } else {
            // Stickless pad — the stick bytes are not meaningful, so centre them
            // rather than letting them read as a permanently deflected stick.
            sw->event.analog[ANALOG_LX] = 128;
            sw->event.analog[ANALOG_LY] = 128;
        }

        if (sw->model == SWITCH_MODEL_PRO) {
            uint16_t rx = unpack_stick_12bit(rpt->right_stick, false);
            uint16_t ry = unpack_stick_12bit(rpt->right_stick, true);
            sw->event.analog[ANALOG_RX] = scale_12bit_to_8bit(rx);
            sw->event.analog[ANALOG_RY] = 255 - scale_12bit_to_8bit(ry);
        } else {
            // N64 C-cluster, or centred for the stickless pads.
            sw->event.analog[ANALOG_RX] = nso_rx;
            sw->event.analog[ANALOG_RY] = nso_ry;
        }

        // Battery: bits 7-4 = level (0/2/4/6/8), bit 3 = charging
        uint8_t bat_raw = rpt->battery_conn >> 4;
        sw->event.battery_level = (bat_raw > 8) ? 100 : bat_raw * 12 + 5;
        sw->event.battery_charging = (rpt->battery_conn & 0x08) != 0;

        router_submit_input(&sw->event);

    } else if (report_id == SWITCH_REPORT_INPUT_SIMPLE && len >= 12) {
        // Simple HID report (0x3F) - used before full mode enabled
        const switch_simple_report_t* rpt = (const switch_simple_report_t*)data;

        uint32_t buttons = 0x00000000;
        uint8_t nso_rx = 128, nso_ry = 128;

        if (sw->model != SWITCH_MODEL_PRO) {
            const switch_raw_btns_t n = {
                .b = rpt->b, .a = rpt->a, .y = rpt->y, .x = rpt->x,
                .l = rpt->l, .r = rpt->r, .zl = rpt->zl, .zr = rpt->zr,
                .minus = rpt->minus, .plus = rpt->plus,
                .lstick = rpt->lstick, .rstick = rpt->rstick,
                .home = rpt->home, .capture = rpt->capture,
            };
            buttons = switch_map_nso_buttons(sw->model, &n, &nso_rx, &nso_ry);
        } else {
            if (rpt->b)      buttons |= JP_BUTTON_B1;  // B = bottom
            if (rpt->a)      buttons |= JP_BUTTON_B2;  // A = right
            if (rpt->y)      buttons |= JP_BUTTON_B3;  // Y = left
            if (rpt->x)      buttons |= JP_BUTTON_B4;  // X = top
            if (rpt->l)      buttons |= JP_BUTTON_L1;
            if (rpt->r)      buttons |= JP_BUTTON_R1;
            if (rpt->zl)     buttons |= JP_BUTTON_L2;
            if (rpt->zr)     buttons |= JP_BUTTON_R2;
            if (rpt->minus)  buttons |= JP_BUTTON_S1;
            if (rpt->plus)   buttons |= JP_BUTTON_S2;
            if (rpt->lstick) buttons |= JP_BUTTON_L3;
            if (rpt->rstick) buttons |= JP_BUTTON_R3;
            if (rpt->home)   buttons |= JP_BUTTON_A1;
            if (rpt->capture) buttons |= JP_BUTTON_A2;
        }

        // Hat to D-pad
        if (rpt->hat == 0 || rpt->hat == 1 || rpt->hat == 7) buttons |= JP_BUTTON_DU;
        if (rpt->hat >= 1 && rpt->hat <= 3) buttons |= JP_BUTTON_DR;
        if (rpt->hat >= 3 && rpt->hat <= 5) buttons |= JP_BUTTON_DD;
        if (rpt->hat >= 5 && rpt->hat <= 7) buttons |= JP_BUTTON_DL;

        sw->event.buttons = buttons;

        if (switch_model_has_sticks(sw->model)) {
            // 16-bit sticks scaled to 8-bit (0-65535 → 0-255)
            sw->event.analog[ANALOG_LX] = rpt->lx >> 8;
            sw->event.analog[ANALOG_LY] = 255 - (rpt->ly >> 8);  // Invert Y (Nintendo: up=high, HID: up=low)
        } else {
            sw->event.analog[ANALOG_LX] = 128;
            sw->event.analog[ANALOG_LY] = 128;
        }

        if (sw->model == SWITCH_MODEL_PRO) {
            sw->event.analog[ANALOG_RX] = rpt->rx >> 8;
            sw->event.analog[ANALOG_RY] = 255 - (rpt->ry >> 8);  // Invert Y (Nintendo: up=high, HID: up=low)
        } else {
            sw->event.analog[ANALOG_RX] = nso_rx;
            sw->event.analog[ANALOG_RY] = nso_ry;
        }

        router_submit_input(&sw->event);
    }
}

static void switch_task(bthid_device_t* device)
{
    switch_bt_data_t* sw = (switch_bt_data_t*)device->driver_data;
    if (!sw) return;

    uint32_t now = platform_time_ms();

    switch (sw->init_state) {
        case SWITCH_STATE_WAIT_READY:
            // Wait before sending first subcommand
            if (now - sw->init_time >= SWITCH_INIT_DELAY_MS) {
                printf("[SWITCH_BT] Sending set input mode (0x30)\n");
                uint8_t mode = SWITCH_INPUT_MODE_FULL;
                switch_send_subcommand(device, SWITCH_SUBCMD_SET_INPUT_MODE, &mode, 1);
                sw->init_state = SWITCH_STATE_SET_INPUT_MODE;
                sw->init_time = now;
            }
            break;

        case SWITCH_STATE_SET_INPUT_MODE:
            if (now - sw->init_time >= SWITCH_INIT_DELAY_MS) {
                printf("[SWITCH_BT] Sending enable vibration\n");
                uint8_t enable = 0x01;
                switch_send_subcommand(device, SWITCH_SUBCMD_ENABLE_VIBRATION, &enable, 1);
                sw->init_state = SWITCH_STATE_ENABLE_VIBRATION;
                sw->init_time = now;
            }
            break;

        case SWITCH_STATE_ENABLE_VIBRATION:
            if (now - sw->init_time >= SWITCH_INIT_DELAY_MS) {
                // Set player LED based on player index
                int player_idx = find_player_index(sw->event.dev_addr, sw->event.instance);
                uint8_t player_num = (player_idx >= 0) ? player_idx + 1 : 1;
                uint8_t pattern = 0;
                if (player_num >= 1 && player_num <= 4) {
                    pattern = (1 << player_num) - 1;
                }
                printf("[SWITCH_BT] Sending player LED (player %d, pattern 0x%02X)\n",
                       player_num, pattern);
                switch_send_subcommand(device, SWITCH_SUBCMD_SET_PLAYER_LED, &pattern, 1);
                sw->init_state = SWITCH_STATE_SET_PLAYER_LED;
                sw->init_time = now;
            }
            break;

        case SWITCH_STATE_SET_PLAYER_LED:
            if (now - sw->init_time >= SWITCH_INIT_DELAY_MS) {
                printf("[SWITCH_BT] Init complete\n");
                sw->init_state = SWITCH_STATE_ACTIVE;
            }
            break;

        case SWITCH_STATE_ACTIVE: {
            // Monitor feedback system for rumble/LED updates
            int player_idx = find_player_index(sw->event.dev_addr, sw->event.instance);
            if (player_idx < 0) break;

            feedback_state_t* fb = feedback_get_state(player_idx);
            if (!fb) break;

            // Handle rumble updates
            if (fb->rumble_dirty) {
                uint8_t left = fb->rumble.left;
                uint8_t right = fb->rumble.right;
                if (left != sw->rumble_left || right != sw->rumble_right) {
                    switch_send_rumble(device, left, right);
                    sw->rumble_left = left;
                    sw->rumble_right = right;
                }
            }

            // Handle LED updates
            if (fb->led_dirty) {
                uint8_t pattern = fb->led.pattern;
                if (pattern != 0) {
                    printf("[SWITCH_BT] LED update: pattern=0x%02X\n", pattern);
                    switch_send_subcommand(device, SWITCH_SUBCMD_SET_PLAYER_LED, &pattern, 1);
                }
            }

            if (fb->rumble_dirty || fb->led_dirty) {
                feedback_clear_dirty(player_idx);
            }
            break;
        }
    }
}

static void switch_disconnect(bthid_device_t* device)
{
    printf("[SWITCH_BT] Disconnect: %s\n", device->name);

    switch_bt_data_t* sw = (switch_bt_data_t*)device->driver_data;
    if (sw) {
        // Clear router state first (sends zeroed input report)
        router_device_disconnected(sw->event.dev_addr, sw->event.instance);
        // Remove player assignment
        remove_players_by_address(sw->event.dev_addr, sw->event.instance);

        init_input_event(&sw->event);
        sw->initialized = false;
    }
}

// ============================================================================
// DRIVER STRUCT
// ============================================================================

const bthid_driver_t switch_pro_bt_driver = {
    .name = "Switch Pro",
    .match = switch_match,
    .init = switch_init,
    .process_report = switch_process_report,
    .task = switch_task,
    .disconnect = switch_disconnect,
};

void switch_pro_bt_register(void)
{
    bthid_register_driver(&switch_pro_bt_driver);
}
