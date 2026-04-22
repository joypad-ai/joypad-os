// wiimote_reports.h - Wiimote report format builders
// SPDX-License-Identifier: Apache-2.0
//
// Pure-logic helpers that turn joypad-os input_event_t into Wiimote HID
// input reports, and that interpret output reports from the Wii. No BTstack
// dependency — the caller hands over a buffer and gets back bytes ready to
// pipe into hid_device_send_interrupt_message().
//
// Report IDs implemented so far:
//   0x30 : core buttons only (2 bytes after the ID)
//   0x31 : core buttons + 3-byte accelerometer
//   0x20 : status (battery, ext flag, LEDs)
//
// Output (Wii -> Wiimote) decoding:
//   0x11 : LEDs           -> wiimote_state.leds
//   0x12 : reporting mode -> wiimote_state.reporting_mode
//   0x15 : status request -> trigger a 0x20 reply
//
// The rest of the 0x3x interleaved modes + IR + ext live in wiimote_reports_ext
// (Phase 1c).

#ifndef WIIMOTE_REPORTS_H
#define WIIMOTE_REPORTS_H

#include <stdint.h>
#include <stdbool.h>
#include "core/input_event.h"
#include "core/buttons.h"

// ============================================================================
// CORE BUTTON BITS (per WiiBrew: BB / BB little-endian encoding)
// ============================================================================
// Byte 0 (LSB):
//   bit 0: D-Left
//   bit 1: D-Right
//   bit 2: D-Down
//   bit 3: D-Up
//   bit 4: Plus
//   bit 7: (unknown - also sometimes used for accel low bit 1)
// Byte 1 (MSB):
//   bit 0: Two
//   bit 1: One
//   bit 2: B
//   bit 3: A
//   bit 4: Minus
//   bit 7: Home

#define WM_BTN_LEFT       0x0001
#define WM_BTN_RIGHT      0x0002
#define WM_BTN_DOWN       0x0004
#define WM_BTN_UP         0x0008
#define WM_BTN_PLUS       0x0010
#define WM_BTN_TWO        0x0100
#define WM_BTN_ONE        0x0200
#define WM_BTN_B          0x0400
#define WM_BTN_A          0x0800
#define WM_BTN_MINUS      0x1000
#define WM_BTN_HOME       0x8000

// ============================================================================
// WIIMOTE RUNTIME STATE (owned by bt_output_wiimote.c, passed in by reference)
// ============================================================================

typedef enum {
    WM_REPORT_CORE      = 0x30,
    WM_REPORT_CORE_ACC  = 0x31,
    WM_REPORT_CORE_EXT8 = 0x32,
    WM_REPORT_CORE_ACC_IR12 = 0x33,
    WM_REPORT_CORE_EXT19 = 0x34,
    WM_REPORT_CORE_ACC_EXT16 = 0x35,
    WM_REPORT_CORE_IR10_EXT9 = 0x36,
    WM_REPORT_CORE_ACC_IR10_EXT6 = 0x37,
    WM_REPORT_EXT21     = 0x3d,
    WM_REPORT_INTER_A   = 0x3e,
    WM_REPORT_INTER_B   = 0x3f,
} wm_report_mode_t;

typedef struct {
    uint8_t  leds;              // Player LEDs (bits 4-7 used)
    uint8_t  reporting_mode;    // Last 0x30-0x37 / 0x3d chosen by Wii
    bool     continuous;        // 0x12 byte 0 bit 2
    bool     rumble;            // 0x11/0x13/0x14/0x15 byte 0 bit 0
    uint8_t  battery_level;     // 0..200 (0x20 byte 5)
    bool     extension_attached;// 0x20 byte 2 bit 1
} wiimote_state_t;

static inline void wiimote_state_init(wiimote_state_t* s) {
    s->leds = 0x10;             // default: LED 1 lit
    s->reporting_mode = WM_REPORT_CORE;
    s->continuous = false;
    s->rumble = false;
    s->battery_level = 0xC8;    // ~80% = 0xC8 = 200 on WiiBrew scale
    s->extension_attached = false;
}

// ============================================================================
// BUTTON MAPPING - joypad_os input_event_t -> Wiimote core-button bitmap
// ============================================================================
// Horizontal orientation (NES-classic mapping): d-pad drives dpad, B1/B2
// map to 2/1 buttons, START/SELECT map to Plus/Minus, A1 maps to Home.
// For vertical orientation (Mario Party style) the Wii usually expects a
// Nunchuk — we default to horizontal and let the app layer remap later.

static inline uint16_t wiimote_buttons_from_event(const input_event_t* e) {
    uint32_t b = e->buttons;
    uint16_t wm = 0;
    // Horizontal Wiimote d-pad: physical up/down/left/right rotate 90°.
    // We keep the joypad-os d-pad mapping straight-through for now; a
    // horizontal-rotation transform can be added in Phase 1c.
    if (b & JP_BUTTON_DU) wm |= WM_BTN_UP;
    if (b & JP_BUTTON_DD) wm |= WM_BTN_DOWN;
    if (b & JP_BUTTON_DL) wm |= WM_BTN_LEFT;
    if (b & JP_BUTTON_DR) wm |= WM_BTN_RIGHT;
    if (b & JP_BUTTON_B1) wm |= WM_BTN_A;        // B1 (main action) -> A
    if (b & JP_BUTTON_B2) wm |= WM_BTN_B;        // B2 (secondary) -> B
    if (b & JP_BUTTON_B3) wm |= WM_BTN_ONE;
    if (b & JP_BUTTON_B4) wm |= WM_BTN_TWO;
    if (b & JP_BUTTON_S1) wm |= WM_BTN_MINUS;
    if (b & JP_BUTTON_S2) wm |= WM_BTN_PLUS;
    if (b & JP_BUTTON_A1) wm |= WM_BTN_HOME;
    return wm;
}

// ============================================================================
// REPORT BUILDERS
// ============================================================================
// Each builder writes into `out` (caller-provided buffer) and returns bytes
// written. Buffer must be >= 23 bytes for any report.

// 0x20 Status (7 bytes total: ID + 6 payload)
// Payload: buttons(2), flags(1), unused(2), battery(1)
static inline uint16_t wiimote_build_status(const wiimote_state_t* s,
                                            const input_event_t* e,
                                            uint8_t* out) {
    uint16_t btns = e ? wiimote_buttons_from_event(e) : 0;
    out[0] = 0x20;
    out[1] = btns & 0xff;
    out[2] = (btns >> 8) & 0xff;
    uint8_t flags = 0;
    if (s->leds & 0xF0) flags |= (s->leds & 0xF0);   // LEDs mirror upper nibble
    if (s->extension_attached) flags |= 0x02;
    // bit 0x04 = speaker enabled, 0x08 = IR enabled, 0x10 = battery low
    out[3] = flags;
    out[4] = 0;
    out[5] = 0;
    out[6] = s->battery_level;
    return 7;
}

// 0x30 Core buttons (3 bytes total: ID + 2 payload)
static inline uint16_t wiimote_build_core(const input_event_t* e, uint8_t* out) {
    uint16_t btns = wiimote_buttons_from_event(e);
    out[0] = 0x30;
    out[1] = btns & 0xff;
    out[2] = (btns >> 8) & 0xff;
    return 3;
}

// 0x31 Core + accel (6 bytes total: ID + 2 buttons + 3 accel)
// Accel values: 0-255, 128 = 0g along that axis, 163 ≈ +1g.
// With no real accelerometer we report the "face-up horizontal" default.
static inline uint16_t wiimote_build_core_accel(const input_event_t* e, uint8_t* out) {
    uint16_t btns = wiimote_buttons_from_event(e);
    out[0] = 0x31;
    out[1] = btns & 0xff;
    out[2] = (btns >> 8) & 0xff;
    out[3] = 0x80;  // X = 0g
    out[4] = 0x80;  // Y = 0g
    out[5] = 0xA3;  // Z = +1g (face-up)
    return 6;
}

// Core + 8-byte extension (0x32): 11 bytes total.
// Caller provides up to 6 bytes of extension data; remaining 2 bytes zero-padded.
static inline uint16_t wiimote_build_core_ext8(const input_event_t* e,
                                               const uint8_t* ext, uint16_t ext_len,
                                               uint8_t* out) {
    uint16_t btns = wiimote_buttons_from_event(e);
    out[0] = 0x32;
    out[1] = btns & 0xff;
    out[2] = (btns >> 8) & 0xff;
    for (int i = 0; i < 8; i++) out[3 + i] = (i < ext_len) ? ext[i] : 0x00;
    return 11;
}

// Core + accel + 16-byte extension (0x35): 22 bytes total.
static inline uint16_t wiimote_build_core_acc_ext16(const input_event_t* e,
                                                    const uint8_t* ext, uint16_t ext_len,
                                                    uint8_t* out) {
    uint16_t btns = wiimote_buttons_from_event(e);
    out[0] = 0x35;
    out[1] = btns & 0xff;
    out[2] = (btns >> 8) & 0xff;
    out[3] = 0x80;  // Accel X (0g)
    out[4] = 0x80;  // Accel Y (0g)
    out[5] = 0xA3;  // Accel Z (+1g face-up)
    for (int i = 0; i < 16; i++) out[6 + i] = (i < ext_len) ? ext[i] : 0x00;
    return 22;
}

// Core + 19-byte extension (0x34): 22 bytes total.
static inline uint16_t wiimote_build_core_ext19(const input_event_t* e,
                                                const uint8_t* ext, uint16_t ext_len,
                                                uint8_t* out) {
    uint16_t btns = wiimote_buttons_from_event(e);
    out[0] = 0x34;
    out[1] = btns & 0xff;
    out[2] = (btns >> 8) & 0xff;
    for (int i = 0; i < 19; i++) out[3 + i] = (i < ext_len) ? ext[i] : 0x00;
    return 22;
}

// Build whichever report is currently selected by the Wii. `ext` is the
// caller's current extension payload (NULL if no extension attached); up to
// `ext_len` bytes will be included in extension-mode reports, rest zero-padded.
static inline uint16_t wiimote_build_current(const wiimote_state_t* s,
                                             const input_event_t* e,
                                             const uint8_t* ext, uint16_t ext_len,
                                             uint8_t* out) {
    switch (s->reporting_mode) {
        case WM_REPORT_CORE_ACC:
        case WM_REPORT_CORE_ACC_IR12:
        case WM_REPORT_CORE_ACC_IR10_EXT6:
            return wiimote_build_core_accel(e, out);
        case WM_REPORT_CORE_EXT8:
        case WM_REPORT_CORE_IR10_EXT9:
            return wiimote_build_core_ext8(e, ext, ext_len, out);
        case WM_REPORT_CORE_ACC_EXT16:
            return wiimote_build_core_acc_ext16(e, ext, ext_len, out);
        case WM_REPORT_CORE_EXT19:
        case WM_REPORT_EXT21:
            return wiimote_build_core_ext19(e, ext, ext_len, out);
        case WM_REPORT_CORE:
        default:
            return wiimote_build_core(e, out);
    }
}

// ============================================================================
// OUTPUT REPORT DECODE (Wii -> Wiimote)
// ============================================================================
// Returns true if caller should queue a 0x20 status reply after processing.

static inline bool wiimote_apply_output(wiimote_state_t* s,
                                        const uint8_t* report, uint16_t len) {
    if (len < 2) return false;
    uint8_t id = report[0];
    uint8_t b0 = report[1];
    // Bit 0 of every host-output report drives the rumble motor.
    s->rumble = (b0 & 0x01) != 0;

    switch (id) {
        case 0x11:  // LEDs
            s->leds = b0 & 0xF0;
            return false;
        case 0x12:  // Reporting mode
            if (len < 3) return false;
            s->continuous = (b0 & 0x04) != 0;
            s->reporting_mode = report[2];
            return false;
        case 0x15:  // Status request
            return true;    // caller will send 0x20
        default:
            return false;   // unhandled output — ignore
    }
}

#endif // WIIMOTE_REPORTS_H
