// Host-side unit test for profile_scale_axis() (core/services/profiles/profile.h),
// the helper every stick-sensitivity path uses. The copy below must match the
// header; it is here so the test runs without a Pico toolchain.
//
// Build: gcc -std=c11 -Wall test_stick_sensitivity.c -o test_stick_sensitivity && ./test_stick_sensitivity
// Pass criterion: prints "ALL PASS".

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

static inline uint8_t profile_scale_axis(uint8_t value, float sens)
{
    int32_t v = 128 + (int32_t)(((int32_t)value - 128) * sens);
    if (v < 0) v = 0;
    if (v > 255) v = 255;
    return (uint8_t)v;
}

// The expression this helper replaced, kept to pin the behaviour that must
// NOT change: every in-range result is identical to what shipped before.
static int old_unclamped(uint8_t value, float sens)
{
    int16_t rel = (int16_t)value - 128;
    return 128 + (int16_t)(rel * sens);
}

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); exit(1); } } while (0)

int main(void)
{
    // The reported symptom: at 150% a full push right came out left of centre.
    CHECK(profile_scale_axis(255, 1.5f) == 255, "full right at 150%% -> %d", profile_scale_axis(255, 1.5f));
    CHECK(profile_scale_axis(0,   1.5f) == 0,   "full left at 150%% -> %d",  profile_scale_axis(0, 1.5f));
    CHECK(profile_scale_axis(255, 1.01f) == 255, "full right at 101%% -> %d", profile_scale_axis(255, 1.01f));
    CHECK(profile_scale_axis(128, 2.0f) == 128, "centre moved");

    // Whole configurable range (0-200%), every axis value.
    for (int pct = 0; pct <= 200; pct++) {
        float sens = pct / 100.0f;
        int prev = -1;
        for (int v = 0; v <= 255; v++) {
            int got = profile_scale_axis((uint8_t)v, sens);
            int old = old_unclamped((uint8_t)v, sens);

            // Identical wherever the old expression stayed in range.
            if (old >= 0 && old <= 255)
                CHECK(got == old, "pct=%d v=%d changed %d -> %d", pct, v, old, got);
            else
                CHECK(got == (old < 0 ? 0 : 255), "pct=%d v=%d not saturated: %d", pct, v, got);

            // Never reverses: pushing further never reads as less.
            CHECK(got >= prev, "pct=%d v=%d not monotonic (%d after %d)", pct, v, got, prev);
            prev = got;
        }
    }

    printf("ALL PASS\n");
    return 0;
}
