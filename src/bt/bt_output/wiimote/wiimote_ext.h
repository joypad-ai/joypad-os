// wiimote_ext.h - Wiimote extension byte builders (Nunchuk / Classic Controller)
// SPDX-License-Identifier: Apache-2.0
//
// Pure-logic builders that pack joypad-os input_event_t into the 6-byte
// Nunchuk extension format or the 6-byte / 9-byte Classic Controller
// extension format. Encryption is NOT performed here — the caller (or a
// wrapper) can XOR the bytes with the Wii's extension stream cipher if a
// particular game requires it. Many modern Wiis and homebrew accept
// unencrypted data after writing 0x55/0x00 to register 0xF0.
//
// Register map (answered via 0x21 read-memory responses):
//   0xFA-0xFF : extension identifier (6 bytes)
//     Nunchuk         : 00 00 A4 20 00 00
//     Classic         : 00 00 A4 20 01 01
//     Classic Pro     : 00 00 A4 20 01 01 (distinguished by 0xFE)
//   0x20-0x3F : calibration data
//   0x40-0x4F : Wii writes encryption key here (we respond with ack)
//   0xF0      : encryption enable (0xAA activates, 0x55/0x00 disables)
//   0xFB      : returns 0x00 (sanity)

#ifndef WIIMOTE_EXT_H
#define WIIMOTE_EXT_H

#include <stdint.h>
#include <stdbool.h>
#include "core/input_event.h"
#include "core/buttons.h"

// ============================================================================
// EXTENSION IDENTIFIER BYTES (read from 0xFA-0xFF)
// ============================================================================
//
// WiiBrew: these six bytes are what the Wii reads at extension memory
// 0x00FA to uniquely identify an extension controller.

static const uint8_t WIIMOTE_EXT_ID_NONE[6]     = { 0x00, 0x00, 0xA4, 0x20, 0x00, 0x00 };
static const uint8_t WIIMOTE_EXT_ID_NUNCHUK[6]  = { 0x00, 0x00, 0xA4, 0x20, 0x00, 0x00 };
static const uint8_t WIIMOTE_EXT_ID_CLASSIC[6]  = { 0x00, 0x00, 0xA4, 0x20, 0x01, 0x01 };
// Classic Controller Pro uses the same ID but digital L/R trigger clicks
// only report fully pressed (value 31) — no analog pressure.

// ============================================================================
// EXTENSION KIND (forward-declared — used by calibration + payload builders)
// ============================================================================

typedef enum {
    WM_EXT_NONE     = 0,
    WM_EXT_NUNCHUK,
    WM_EXT_CLASSIC,
    WM_EXT_CLASSIC_PRO,
} wm_ext_kind_t;

// ============================================================================
// CALIBRATION DATA (extension memory 0x20-0x3F — two identical 16-byte blocks)
// ============================================================================
// Read by the Wii after the extension ID to figure out how to interpret
// stick/accel values. Layout and defaults are straight from WiiBrew.
//
// Nunchuk (16 bytes) — two identical 16-byte blocks at 0x20-0x2F and 0x30-0x3F:
//   0x00-0x02 : zero-g X, Y, Z (10-bit high bytes)
//   0x03      : packed LSBs + battery bits (safe to leave 0)
//   0x04-0x06 : one-g X, Y, Z (10-bit high bytes)
//   0x07      : packed LSBs
//   0x08      : joystick X max   (0-255)
//   0x09      : joystick X min
//   0x0A      : joystick X center
//   0x0B      : joystick Y max
//   0x0C      : joystick Y min
//   0x0D      : joystick Y center
//   0x0E-0x0F : 16-bit checksum = (sum(bytes) + 0x55AA) & 0xFFFF stored big-endian
//
// Classic Controller (16 bytes):
//   0x00 : LX max (0-63)      0x01 : LX min      0x02 : LX center
//   0x03 : LY max             0x04 : LY min      0x05 : LY center
//   0x06 : RX max (0-31)      0x07 : RX min      0x08 : RX center
//   0x09 : RY max             0x0A : RY min      0x0B : RY center
//   0x0C : LT max             0x0D : LT min
//   0x0E-0x0F : checksum (same formula as Nunchuk)
//
// For both, we use neutral defaults that the Wii treats as "full range" —
// game-side calibration then adjusts live. Duplicated at 0x20 and 0x30 so
// the Wii's redundant read returns the same data.

static inline void wiimote_ext_build_calibration(wm_ext_kind_t kind, uint8_t out[16]) {
    for (int i = 0; i < 16; i++) out[i] = 0;

    switch (kind) {
        case WM_EXT_NUNCHUK: {
            // Accel cal: zero-g 512 (0x200), one-g 640 (0x280) — 10-bit values,
            // upper byte = value >> 2.
            out[0x00] = 0x80;   // zero-g X hi
            out[0x01] = 0x80;   // zero-g Y hi
            out[0x02] = 0x80;   // zero-g Z hi
            out[0x03] = 0x00;   // LSB packing (zeros OK)
            out[0x04] = 0xA0;   // one-g X hi
            out[0x05] = 0xA0;   // one-g Y hi
            out[0x06] = 0xA0;   // one-g Z hi
            out[0x07] = 0x00;   // LSB packing
            // Stick cal: 8-bit full range.
            out[0x08] = 0xFF;   // X max
            out[0x09] = 0x00;   // X min
            out[0x0A] = 0x80;   // X center
            out[0x0B] = 0xFF;   // Y max
            out[0x0C] = 0x00;   // Y min
            out[0x0D] = 0x80;   // Y center
            break;
        }
        case WM_EXT_CLASSIC:
        case WM_EXT_CLASSIC_PRO: {
            // 6-bit L-stick, 5-bit R-stick, 5-bit triggers.
            out[0x00] = 0x3F; out[0x01] = 0x00; out[0x02] = 0x20;   // LX max/min/ctr
            out[0x03] = 0x3F; out[0x04] = 0x00; out[0x05] = 0x20;   // LY
            out[0x06] = 0x1F; out[0x07] = 0x00; out[0x08] = 0x10;   // RX
            out[0x09] = 0x1F; out[0x0A] = 0x00; out[0x0B] = 0x10;   // RY
            out[0x0C] = 0x1F; out[0x0D] = 0x00;                     // LT max/min
            break;
        }
        case WM_EXT_NONE:
        default:
            return;
    }

    // 16-bit big-endian checksum = (sum(bytes 0..13) + 0x55AA)
    uint16_t sum = 0;
    for (int i = 0; i < 14; i++) sum += out[i];
    sum += 0x55AA;
    out[0x0E] = (sum >> 8) & 0xFF;
    out[0x0F] = sum & 0xFF;
}

// ============================================================================
// NUNCHUK — 6-byte extension payload
// ============================================================================
// Byte layout (per WiiBrew):
//   0: Joystick X (0-255, centered ~128)
//   1: Joystick Y (0-255, centered ~128)
//   2-4: Accelerometer X,Y,Z high bytes (10-bit packed w/ bits 0-1 in byte 5)
//   5: bits 7-6 AZ low, 5-4 AY low, 3-2 AX low, bit 1 C inverted, bit 0 Z inverted
//   (C/Z are active-low: 0 = pressed)

static inline void wiimote_ext_build_nunchuk(const input_event_t* e, uint8_t out[6]) {
    // Sticks: joypad internal LX/LY are 0-255 with 128 center and Y=0=up (HID).
    // Nunchuk stick uses the SAME center but Y=0 = down-physical, so we
    // invert Y.
    uint8_t sx = e ? e->analog[ANALOG_LX] : 128;
    uint8_t sy = e ? (uint8_t)(255 - e->analog[ANALOG_LY]) : 128;
    out[0] = sx;
    out[1] = sy;

    // Accel: default to "face-up horizontal" when we have no motion data.
    // 10-bit Nunchuk accel, zero-g ~= 512 (0x200), 1g ~= 640 (0x280).
    uint16_t ax = 0x200;
    uint16_t ay = 0x200;
    uint16_t az = 0x280;    // Z at 1g for face-up
    out[2] = (ax >> 2) & 0xFF;
    out[3] = (ay >> 2) & 0xFF;
    out[4] = (az >> 2) & 0xFF;

    // Byte 5: packed accel LSBs + buttons.
    // C mapped to joypad B3, Z mapped to joypad L1 (matches PicoGamepadConverter
    // convention — top face btn + left trigger).
    uint8_t b5 = ((az & 0x03) << 6) | ((ay & 0x03) << 4) | ((ax & 0x03) << 2);
    if (e) {
        // Active-low: set bit when NOT pressed (so clear when pressed)
        bool c_pressed = (e->buttons & JP_BUTTON_B3) != 0;
        bool z_pressed = (e->buttons & JP_BUTTON_L1) != 0;
        if (!c_pressed) b5 |= 0x02;
        if (!z_pressed) b5 |= 0x01;
    } else {
        b5 |= 0x03;  // both released
    }
    out[5] = b5;
}

// ============================================================================
// CLASSIC CONTROLLER — 6-byte extension payload
// ============================================================================
// Byte layout (per WiiBrew):
//   0: LX low 6 bits | RX high 2 bits in 6-7
//   1: LY low 6 bits | RX bit 4 in 7, RX bit 3 in 6? (actually: RX bits 1-0 in 6-7, LX in 5-0)
//
// Actually the real layout is intricate — RX is 5 bits split across three
// bytes. Full WiiBrew layout:
//   byte 0: bits 0-5 = LX (6-bit, 0-63), bits 6-7 = RX hi two bits
//   byte 1: bits 0-5 = LY (6-bit),       bits 6-7 = RX middle two bits
//   byte 2: bit 7    = RY hi bit? no...
//
// Referenced layout (standard encoding used by VM/emulators):
//   byte 0 : RX[4:3] << 6 | LX[5:0]
//   byte 1 : RX[2:1] << 6 | LY[5:0]
//   byte 2 : RX[0]   << 7 | LT[4:3] << 5 | RY[4:0]
//   byte 3 : LT[2:0] << 5 | RT[4:0]
//   byte 4 : buttons hi (BDR, BDD, BLT, B-, BH, B+, BRT, 1 (unused bit 0))
//   byte 5 : buttons lo (BZL, BB, BY, BA, BX, BZR, BDL, BDU)
//
// All button bits are active-low (0 = pressed).

static inline void wiimote_ext_build_classic(const input_event_t* e, uint8_t out[6]) {
    // Scale 8-bit sticks/triggers to 6-bit / 5-bit Classic ranges.
    uint8_t lx8 = e ? e->analog[ANALOG_LX] : 128;
    uint8_t ly8 = e ? (uint8_t)(255 - e->analog[ANALOG_LY]) : 128;  // invert
    uint8_t rx8 = e ? e->analog[ANALOG_RX] : 128;
    uint8_t ry8 = e ? (uint8_t)(255 - e->analog[ANALOG_RY]) : 128;
    uint8_t lt8 = e ? e->analog[ANALOG_L2] : 0;
    uint8_t rt8 = e ? e->analog[ANALOG_R2] : 0;

    uint8_t lx = lx8 >> 2;     // 0-63
    uint8_t ly = ly8 >> 2;
    uint8_t rx = rx8 >> 3;     // 0-31
    uint8_t ry = ry8 >> 3;
    uint8_t lt = lt8 >> 3;
    uint8_t rt = rt8 >> 3;

    out[0] = ((rx & 0x18) << 3) | (lx & 0x3F);
    out[1] = ((rx & 0x06) << 5) | (ly & 0x3F);
    out[2] = ((rx & 0x01) << 7) | ((lt & 0x18) << 2) | (ry & 0x1F);
    out[3] = ((lt & 0x07) << 5) | (rt & 0x1F);

    // Buttons — active low. Bit meaning per WiiBrew (byte 4 then byte 5):
    uint32_t b = e ? e->buttons : 0;
    uint8_t hi = 0xFF;    // all released
    uint8_t lo = 0xFF;
    if (b & JP_BUTTON_DR) hi &= ~0x80;   // D-Right
    if (b & JP_BUTTON_DD) hi &= ~0x40;   // D-Down
    if (lt > 28)          hi &= ~0x20;   // LT (digital fallback)
    if (b & JP_BUTTON_S1) hi &= ~0x10;   // Minus
    if (b & JP_BUTTON_A1) hi &= ~0x08;   // Home
    if (b & JP_BUTTON_S2) hi &= ~0x04;   // Plus
    if (rt > 28)          hi &= ~0x02;   // RT
    // bit 0 of byte 4: always 1 (unused)

    if (b & JP_BUTTON_L1) lo &= ~0x80;   // ZL (sic — Classic has L/R + ZL/ZR, mapping: L1=ZL)
    if (b & JP_BUTTON_B2) lo &= ~0x40;   // B
    if (b & JP_BUTTON_B4) lo &= ~0x20;   // Y
    if (b & JP_BUTTON_B1) lo &= ~0x10;   // A
    if (b & JP_BUTTON_B3) lo &= ~0x08;   // X
    if (b & JP_BUTTON_R1) lo &= ~0x04;   // ZR
    if (b & JP_BUTTON_DL) lo &= ~0x02;   // D-Left
    if (b & JP_BUTTON_DU) lo &= ~0x01;   // D-Up

    out[4] = hi;
    out[5] = lo;
}

// ============================================================================
// DISPATCH
// ============================================================================

// Copy the appropriate 6-byte ID for `kind` into `out`. Returns true on success.
static inline bool wiimote_ext_get_id(wm_ext_kind_t kind, uint8_t out[6]) {
    const uint8_t* src = NULL;
    switch (kind) {
        case WM_EXT_NUNCHUK:     src = WIIMOTE_EXT_ID_NUNCHUK; break;
        case WM_EXT_CLASSIC:
        case WM_EXT_CLASSIC_PRO: src = WIIMOTE_EXT_ID_CLASSIC; break;
        case WM_EXT_NONE:
        default:                 return false;
    }
    for (int i = 0; i < 6; i++) out[i] = src[i];
    return true;
}

// Build the per-poll extension payload for the current kind. `out` must be
// at least 6 bytes. Returns the number of bytes written (always 6 for now;
// full 21-byte Classic layout with encryption padding is a future
// extension).
static inline uint16_t wiimote_ext_build_payload(wm_ext_kind_t kind,
                                                 const input_event_t* e,
                                                 uint8_t* out) {
    switch (kind) {
        case WM_EXT_NUNCHUK:
            wiimote_ext_build_nunchuk(e, out);
            return 6;
        case WM_EXT_CLASSIC:
        case WM_EXT_CLASSIC_PRO:
            wiimote_ext_build_classic(e, out);
            return 6;
        case WM_EXT_NONE:
        default:
            return 0;
    }
}

#endif // WIIMOTE_EXT_H
