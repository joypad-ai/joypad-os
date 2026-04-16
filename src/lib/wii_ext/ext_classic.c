// ext_classic.c - Wii Classic / Classic Pro report parser (data mode 1)
//
// Native 6-byte report (default after 0xF0=0x55, 0xFB=0x00 init):
//   [0]  bit 7..6 = RX high bits (bits 4..3)
//        bit 5..0 = LX (6-bit, 0..63)
//   [1]  bit 7..6 = RX high bits (bits 2..1)
//        bit 5..0 = LY (6-bit)
//   [2]  bit 7    = RX bit 0
//        bit 6..5 = LT (2 high bits)
//        bit 4..0 = RY (5-bit)
//   [3]  bit 7..5 = LT (3 low bits)    -> LT is 5-bit total
//        bit 4..0 = RT (5-bit)
//   [4]  button byte 1 (active-low)
//        bit 7 = !BDR, 6 = !BDD, 5 = !LT_digital, 4 = !(-), 3 = !Home,
//        bit 2 = !(+), 1 = !RT_digital, 0 = reserved
//   [5]  button byte 2 (active-low)
//        bit 7 = !ZL, 6 = !B, 5 = !Y, 4 = !A,
//        bit 3 = !X,  2 = !ZR, 1 = !DL, 0 = !DU
//
// We upscale to 10-bit axis space: sticks span 0..1023 (6-bit LX/LY < 4,
// 5-bit RX/RY < 6 ... see masks below). Triggers 0..1023.

#include "wii_ext.h"

void wii_ext_parse_classic(wii_ext_t *ext, const uint8_t *r, wii_ext_state_t *out)
{
    // Left stick (6-bit) -> 10-bit (shift left 4).
    uint16_t lx = (uint16_t)(r[0] & 0x3F);
    uint16_t ly = (uint16_t)(r[1] & 0x3F);
    // Right stick (5-bit split across three bytes) -> 10-bit (shift left 5).
    uint16_t rx = (uint16_t)(((r[0] >> 3) & 0x18) |
                             ((r[1] >> 5) & 0x06) |
                             ((r[2] >> 7) & 0x01));
    uint16_t ry = (uint16_t)(r[2] & 0x1F);
    // Triggers (5-bit) -> 10-bit (shift left 5).
    uint16_t lt = (uint16_t)(((r[2] >> 2) & 0x18) | ((r[3] >> 5) & 0x07));
    uint16_t rt = (uint16_t)(r[3] & 0x1F);

    out->analog[WII_AXIS_LX] = (uint16_t)(lx << 4);
    out->analog[WII_AXIS_LY] = (uint16_t)(ly << 4);
    out->analog[WII_AXIS_RX] = (uint16_t)(rx << 5);
    out->analog[WII_AXIS_RY] = (uint16_t)(ry << 5);
    out->analog[WII_AXIS_LT] = (uint16_t)(lt << 5);
    out->analog[WII_AXIS_RT] = (uint16_t)(rt << 5);

    // Classic Pro reports zeros in the analog triggers (they're digital-only
    // micro-switches). The L1/R1 digital bits in byte 4 still fire.
    if (ext->type == WII_EXT_TYPE_CLASSIC_PRO) {
        out->analog[WII_AXIS_LT] = 0;
        out->analog[WII_AXIS_RT] = 0;
    }

    // Buttons (active-low in the report; invert once here).
    uint16_t inv = (uint16_t)(((~r[4]) & 0xFF) << 8) | (uint16_t)((~r[5]) & 0xFF);
    // Layout after the invert:
    //   bit 15 BDR, 14 BDD, 13 L_digital, 12 MINUS, 11 HOME, 10 PLUS, 9 R_digital, 8 -
    //   bit 7  ZL,  6  B,   5  Y,         4  A,     3  X,    2  ZR,   1 DL,        0 DU
    uint32_t b = 0;
    if (inv & (1u << 15)) b |= WII_BTN_DR;
    if (inv & (1u << 14)) b |= WII_BTN_DD;
    if (inv & (1u << 13)) b |= WII_BTN_L;
    if (inv & (1u << 12)) b |= WII_BTN_MINUS;
    if (inv & (1u << 11)) b |= WII_BTN_HOME;
    if (inv & (1u << 10)) b |= WII_BTN_PLUS;
    if (inv & (1u <<  9)) b |= WII_BTN_R;
    if (inv & (1u <<  7)) b |= WII_BTN_ZL;
    if (inv & (1u <<  6)) b |= WII_BTN_B;
    if (inv & (1u <<  5)) b |= WII_BTN_Y;
    if (inv & (1u <<  4)) b |= WII_BTN_A;
    if (inv & (1u <<  3)) b |= WII_BTN_X;
    if (inv & (1u <<  2)) b |= WII_BTN_ZR;
    if (inv & (1u <<  1)) b |= WII_BTN_DL;
    if (inv & (1u <<  0)) b |= WII_BTN_DU;
    out->buttons = b;

    out->has_accel = false;
}
