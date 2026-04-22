// wiimote_ir.h - Synthesized Wiimote IR camera data
// SPDX-License-Identifier: Apache-2.0
//
// The real Wiimote's IR camera sees up to 4 IR dots in a 1024x768 field of
// view. The sensor bar (above or below the TV) contains two clusters of IR
// LEDs — when the Wiimote points at the TV, the camera detects two dots.
// The Wii uses the dots' positions to compute the pointer location on the
// screen and the distance to the TV.
//
// We synthesize two fake dots from a pointer position (typically mouse
// delta accumulated into x/y, or a right-stick offset). Two separate
// packing formats need to be supported:
//
//   Basic IR (10 bytes — in 0x36): 4 dots packed as pairs in 5 bytes
//   Extended IR (12 bytes — in 0x33, 0x37): 4 dots × 3 bytes (X lo, Y lo,
//     upper 2 bits of X/Y + 4-bit size)
//
// When no pointer input is active, all four dots report "out of view"
// (0xFF) so the Wii shows "off-screen" cursor.

#ifndef WIIMOTE_IR_H
#define WIIMOTE_IR_H

#include <stdint.h>
#include <stdbool.h>

// Camera coordinates:
//   X: 0-1023 (10-bit)
//   Y: 0-767  (10-bit)
#define WIIMOTE_IR_MAX_X 1023
#define WIIMOTE_IR_MAX_Y 767

// Represents the current virtual-pointer target. x/y are in screen space
// where (0,0) is top-left and (1,1) is bottom-right. Set active=false to
// emit out-of-view dots (Wii cursor off-screen).
typedef struct {
    float x;            // 0.0 - 1.0 (left to right)
    float y;            // 0.0 - 1.0 (top to bottom)
    bool  active;       // false => emit no-dot sentinel bytes
    // Sensor-bar position: Above TV uses y ~= top of camera view,
    // Below TV mirrors to the bottom. Nintendo's default is Above.
    bool  bar_above_tv;
} wiimote_ir_state_t;

// Compute the two synthesized dot centres (in camera coords) from the
// current pointer state. dot1_x < dot2_x (left dot, right dot).
static inline void wiimote_ir_compute_dots(const wiimote_ir_state_t* s,
                                           uint16_t* dot1_x, uint16_t* dot1_y,
                                           uint16_t* dot2_x, uint16_t* dot2_y) {
    if (!s || !s->active) {
        *dot1_x = 0x3FF; *dot1_y = 0x3FF;
        *dot2_x = 0x3FF; *dot2_y = 0x3FF;
        return;
    }

    // Pan factor controls how far the bar moves across the camera for a
    // given pointer excursion. Values under 1.0 keep the bar in view at
    // screen edges.
    const float pan = 0.85f;
    const int   cx = 512;                                   // camera centre X
    const int   cy = s->bar_above_tv ? 200 : 567;           // upper/lower strip

    // Camera view mirrors horizontal (pointer right = bar appears left).
    int bar_cx = cx - (int)((s->x - 0.5f) * 1024.0f * pan);
    int bar_cy = cy + (int)((s->y - 0.5f) * 512.0f  * pan);

    // Clamp to camera bounds.
    if (bar_cx < 0) bar_cx = 0;
    if (bar_cx > WIIMOTE_IR_MAX_X) bar_cx = WIIMOTE_IR_MAX_X;
    if (bar_cy < 0) bar_cy = 0;
    if (bar_cy > WIIMOTE_IR_MAX_Y) bar_cy = WIIMOTE_IR_MAX_Y;

    // Fixed dot separation = mid-distance TV (~2 m). Larger spacing = closer.
    const int half_sep = 128;
    int d1x = bar_cx - half_sep;
    int d2x = bar_cx + half_sep;
    if (d1x < 0) { d2x += -d1x; d1x = 0; }
    if (d2x > WIIMOTE_IR_MAX_X) { d1x -= (d2x - WIIMOTE_IR_MAX_X); d2x = WIIMOTE_IR_MAX_X; }

    *dot1_x = (uint16_t)d1x; *dot1_y = (uint16_t)bar_cy;
    *dot2_x = (uint16_t)d2x; *dot2_y = (uint16_t)bar_cy;
}

// ============================================================================
// BASIC IR (10 bytes, used in report 0x36 + 0x37)
// ============================================================================
// 10 bytes encode 4 dots in 2 groups of 5:
//   byte 0: X1[7:0]
//   byte 1: Y1[7:0]
//   byte 2: Y2[9:8] | X2[9:8] | Y1[9:8] | X1[9:8]  (2 bits each)
//   byte 3: X2[7:0]
//   byte 4: Y2[7:0]
// A dot reports X = 0x3FF to mean "out of view".

static inline void wiimote_ir_pack_basic(const wiimote_ir_state_t* s, uint8_t out[10]) {
    uint16_t d1x, d1y, d2x, d2y;
    wiimote_ir_compute_dots(s, &d1x, &d1y, &d2x, &d2y);

    // First pair (dots 0, 1)
    out[0] = d1x & 0xFF;
    out[1] = d1y & 0xFF;
    out[2] = ((d2y >> 8) & 0x03) << 6
           | ((d2x >> 8) & 0x03) << 4
           | ((d1y >> 8) & 0x03) << 2
           | ((d1x >> 8) & 0x03);
    out[3] = d2x & 0xFF;
    out[4] = d2y & 0xFF;

    // Second pair (dots 2, 3) — always out of view (we emulate only 2 LEDs).
    out[5] = 0xFF;
    out[6] = 0xFF;
    out[7] = 0xFF;
    out[8] = 0xFF;
    out[9] = 0xFF;
}

// ============================================================================
// EXTENDED IR (12 bytes, used in report 0x33)
// ============================================================================
// 12 bytes = 4 dots × 3 bytes each:
//   byte 0: X[7:0]
//   byte 1: Y[7:0]
//   byte 2: Y[9:8] | X[9:8] | size(4 bits)
// Dot reports X = 0x3FF, size = 0xF to mean "out of view".

static inline void wiimote_ir_pack_extended(const wiimote_ir_state_t* s, uint8_t out[12]) {
    uint16_t d1x, d1y, d2x, d2y;
    wiimote_ir_compute_dots(s, &d1x, &d1y, &d2x, &d2y);

    // Dot 1
    out[0] = d1x & 0xFF;
    out[1] = d1y & 0xFF;
    out[2] = ((d1y >> 8) & 0x03) << 6 | ((d1x >> 8) & 0x03) << 4 | 0x03;  // size 3

    // Dot 2
    out[3] = d2x & 0xFF;
    out[4] = d2y & 0xFF;
    out[5] = ((d2y >> 8) & 0x03) << 6 | ((d2x >> 8) & 0x03) << 4 | 0x03;

    // Dots 3 + 4: out of view (0xFF X + 0xFF Y + 0xFF packed)
    out[6] = 0xFF; out[7] = 0xFF; out[8] = 0xFF;
    out[9] = 0xFF; out[10] = 0xFF; out[11] = 0xFF;
}

#endif // WIIMOTE_IR_H
