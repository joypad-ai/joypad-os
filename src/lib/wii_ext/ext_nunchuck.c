// ext_nunchuck.c - Wii Nunchuck report parser
//
// Native report (6 bytes from register 0x00):
//   [0]     Stick X, 8-bit unsigned
//   [1]     Stick Y, 8-bit unsigned
//   [2]     Accel X, high 8 bits of 10-bit value
//   [3]     Accel Y, high 8 bits
//   [4]     Accel Z, high 8 bits
//   [5]     bit 7..6 = Accel X low 2 bits
//           bit 5..4 = Accel Y low 2 bits
//           bit 3..2 = Accel Z low 2 bits
//           bit 1    = !C  (0 = pressed)
//           bit 0    = !Z  (0 = pressed)

#include "wii_ext.h"

void wii_ext_parse_nunchuck(wii_ext_t *ext, const uint8_t *r, wii_ext_state_t *out)
{
    (void)ext;

    // Upscale 8-bit stick to common 10-bit space (<<2 = 0..1020).
    out->analog[WII_AXIS_LX] = (uint16_t)(r[0] << 2);
    out->analog[WII_AXIS_LY] = (uint16_t)(r[1] << 2);
    // Nunchuck has no right stick; leave RX/RY at neutral (will be 0 after
    // memset, but the host adapter writes 128 = center when translating).
    out->analog[WII_AXIS_RX] = 512;
    out->analog[WII_AXIS_RY] = 512;
    out->analog[WII_AXIS_LT] = 0;
    out->analog[WII_AXIS_RT] = 0;

    // Accel: 10-bit = (high << 2) | (low 2 bits from byte 5). Reported
    // unsigned with ~0x200 neutral; convert to signed around 0 for
    // consumers that treat it as a tilt vector.
    uint16_t ax = (uint16_t)((r[2] << 2) | ((r[5] >> 2) & 0x03));
    uint16_t ay = (uint16_t)((r[3] << 2) | ((r[5] >> 4) & 0x03));
    uint16_t az = (uint16_t)((r[4] << 2) | ((r[5] >> 6) & 0x03));
    out->accel[0] = (int16_t)ax - 512;
    out->accel[1] = (int16_t)ay - 512;
    out->accel[2] = (int16_t)az - 512;
    out->has_accel = true;

    // Buttons are active-low in the report.
    uint32_t btns = 0;
    if ((r[5] & 0x02) == 0) btns |= WII_BTN_C;
    if ((r[5] & 0x01) == 0) btns |= WII_BTN_Z;
    out->buttons = btns;
}
