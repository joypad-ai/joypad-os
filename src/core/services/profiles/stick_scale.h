// stick_scale.h - Analog stick sensitivity scaling
//
// Scales one 0-255 (128 = centre) axis by a sensitivity multiplier and
// saturates at the ends of the range. Sensitivities above 1.0 are how users
// with limited range of motion reach full deflection from a partial push, so
// the result must clamp: the previous inline (uint8_t)(128 + rel * sens)
// wrapped, sending a full-right stick left at 150% and back to centre at 200%.
//
// Truncation toward zero is kept so in-range results are bit-identical to the
// old expression. Header-only and dependency-free so host tests can include it.

#ifndef STICK_SCALE_H
#define STICK_SCALE_H

#include <stdint.h>

static inline uint8_t stick_scale_axis(uint8_t value, float sens)
{
    int32_t rel = (int32_t)value - 128;
    int32_t out = 128 + (int32_t)((float)rel * sens);
    if (out < 0)   return 0;
    if (out > 255) return 255;
    return (uint8_t)out;
}

#endif // STICK_SCALE_H
