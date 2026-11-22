// Host-side unit test for stick sensitivity scaling.
//
// Sensitivity above 100% used to wrap: (uint8_t)(128 + rx*sens) overflows,
// so at 150% a full-right stick (255) came out as 62 (left) and at 200% a
// full deflection came out near centre. stick_scale_axis() must saturate.
//
// Build (from repo root):
//   gcc -std=c11 -Wall -Wextra -Isrc tests/stick_scale/test_stick_scale.c -o test_stick_scale && ./test_stick_scale
// Pass criterion: prints "ALL PASS" and exits 0.

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "core/services/profiles/stick_scale.h"

static int failures = 0;

#define CHECK(cond, ...) do { \
    if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                   printf(__VA_ARGS__); printf("\n"); } } while (0)

// The pre-fix expression, used to prove behaviour is unchanged wherever it
// did not overflow.
static uint8_t legacy_scale(uint8_t v, float sens) {
    int16_t rel = (int16_t)v - 128;
    return (uint8_t)(128 + (int16_t)(rel * sens));
}

int main(void) {
    // Sensitivities reachable from web config / CDC (0..200 percent).
    for (int pct = 0; pct <= 200; pct++) {
        float sens = pct / 100.0f;
        int prev = -1;
        for (int v = 0; v <= 255; v++) {
            int out = stick_scale_axis((uint8_t)v, sens);

            // Never reverses direction relative to centre.
            if (v > 128) CHECK(out >= 128, "pct=%d v=%d -> %d crossed centre", pct, v, out);
            if (v < 128) CHECK(out <= 128, "pct=%d v=%d -> %d crossed centre", pct, v, out);

            // Monotonic: pushing further never moves the output back.
            CHECK(out >= prev, "pct=%d v=%d -> %d not monotonic (prev %d)", pct, v, out, prev);
            prev = out;

            // Identical to the old maths whenever the old maths stayed in range.
            int16_t raw = 128 + (int16_t)(((int16_t)v - 128) * sens);
            if (raw >= 0 && raw <= 255)
                CHECK(out == legacy_scale((uint8_t)v, sens),
                      "pct=%d v=%d -> %d differs from legacy %d", pct, v, out, legacy_scale((uint8_t)v, sens));
        }
        CHECK(stick_scale_axis(128, sens) == 128, "pct=%d centre moved", pct);
    }

    // The concrete regressions from the bug report.
    CHECK(stick_scale_axis(255, 1.5f) == 255, "150%% full right -> %d", stick_scale_axis(255, 1.5f));
    CHECK(stick_scale_axis(0,   1.5f) == 0,   "150%% full left -> %d",  stick_scale_axis(0, 1.5f));
    CHECK(stick_scale_axis(255, 2.0f) == 255, "200%% full right -> %d", stick_scale_axis(255, 2.0f));
    CHECK(stick_scale_axis(0,   2.0f) == 0,   "200%% full left -> %d",  stick_scale_axis(0, 2.0f));
    // Partial deflection reaching full scale is the point of >100%.
    CHECK(stick_scale_axis(192, 2.0f) == 255, "200%% half right -> %d", stick_scale_axis(192, 2.0f));
    CHECK(stick_scale_axis(64,  2.0f) == 0,   "200%% half left -> %d",  stick_scale_axis(64, 2.0f));

    if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
    printf("ALL PASS\n");
    return 0;
}
