// Host-side unit test for the Xbox One mode stick Y conversion.
// Mimics convert_axis_to_s16 / convert_axis_to_s16_inverted from
// src/usb/usbd/modes/xbone_mode.c so the arithmetic can be checked without a
// Pico toolchain.
//
// Build: gcc -std=c11 -Wall -Wno-unused-function test_xbone_axis.c -o test_xbone_axis && ./test_xbone_axis
//        add -DOLD_NEGATE to model the old "-convert_axis_to_s16(y)" expression
// Pass criterion: prints "ALL PASS" and exits 0.

#include <stdio.h>
#include <stdint.h>

static int16_t convert_axis_to_s16(uint8_t value)
{
    int32_t scaled = ((int32_t)value - 128) * 256;
    if (scaled < -32768) scaled = -32768;
    if (scaled > 32767) scaled = 32767;
    return (int16_t)scaled;
}

static int16_t convert_axis_to_s16_inverted(uint8_t value)
{
    int32_t scaled = -((int32_t)value - 128) * 256;
    if (scaled < -32768) scaled = -32768;
    if (scaled > 32767) scaled = 32767;
    return (int16_t)scaled;
}

static int16_t stick_y(uint8_t v)
{
#ifdef OLD_NEGATE
    return -convert_axis_to_s16(v);   /* the expression the mode used */
#else
    return convert_axis_to_s16_inverted(v);
#endif
}

static int fails;
static void check(int cond, const char *what)
{
    printf("%-60s %s\n", what, cond ? "PASS" : "FAIL");
    if (!cond) fails++;
}

int main(void)
{
    int v, monotonic = 1, prev = 40000;
    check(stick_y(0) > 0, "input 0 (full one way) gives a positive Y");
    check(stick_y(0) == 32767, "input 0 saturates at +32767");
    check(stick_y(128) == 0, "centre gives 0");
    check(stick_y(255) < 0, "input 255 gives a negative Y");
    for (v = 0; v < 256; v++) {
        int y = stick_y((uint8_t)v);
        if (y > prev) monotonic = 0;
        prev = y;
    }
    check(monotonic, "Y never increases as the input rises (no wrap)");
    printf("%s\n", fails ? "SOME FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
