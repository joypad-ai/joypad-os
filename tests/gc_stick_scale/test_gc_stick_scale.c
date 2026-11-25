// Host-side unit test for the GameCube host stick scaling (gc_host.c).
// Mirrors stick_scale() with stubbed state so it runs without a Pico toolchain.
//
// Build: gcc -std=c11 -Wall test_gc_stick_scale.c -o test_gc_stick_scale && ./test_gc_stick_scale
// Old behaviour: add -DOLD_SCALE (expected to FAIL the centre checks).
// Pass criterion: prints "ALL PASS".

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define GC_STICK_INIT_MIN  40
#define GC_STICK_INIT_MAX  215
#define GC_STICK_DEADZONE  3

static struct { uint8_t min, max, centre; bool centre_set; } r;

static void reset(void) { r.min = GC_STICK_INIT_MIN; r.max = GC_STICK_INIT_MAX; r.centre_set = false; }

#ifdef OLD_SCALE
static uint8_t stick_scale(uint8_t raw)
{
    if (raw < 128 - GC_STICK_DEADZONE || raw > 128 + GC_STICK_DEADZONE) {
        if (raw < r.min) r.min = raw;
        if (raw > r.max) r.max = raw;
    }
    uint8_t lo = r.min, hi = r.max;
    if (hi <= lo || hi - lo < 20) return raw;
    if (raw <= lo) return 0;
    if (raw >= hi) return 255;
    return (uint8_t)(((uint16_t)(raw - lo) * 255) / (hi - lo));
}
#else
static uint8_t stick_scale(uint8_t raw)
{
    if (!r.centre_set) { r.centre = raw; r.centre_set = true; }
    uint8_t c = r.centre;
    if (raw + GC_STICK_DEADZONE < c || raw > c + GC_STICK_DEADZONE) {
        if (raw < r.min) r.min = raw;
        if (raw > r.max) r.max = raw;
    }
    uint8_t lo = r.min, hi = r.max;
    if (hi <= lo || hi - lo < 20 || c <= lo || c >= hi) return raw;
    if (raw <= lo) return 0;
    if (raw >= hi) return 255;
    if (raw <= c) return (uint8_t)(((uint16_t)(raw - lo) * 128) / (c - lo));
    return (uint8_t)(128 + ((uint16_t)(raw - c) * 127) / (hi - c));
}
#endif

static int fails;
static void check(bool ok, const char *what, int got)
{
    printf("%-58s %s (got %d)\n", what, ok ? "PASS" : "FAIL", got);
    if (!ok) fails++;
}

int main(void)
{
    // A controller resting at 128 with even travel: rest reads 128.
    reset();
    int v = stick_scale(128);
    check(v == 128, "rest at 128 reads 128", v);

    // A controller resting at 120 (origin off centre), pushed to 30 and 220.
    reset();
    stick_scale(120);                 // first report after connect = origin
    stick_scale(30); stick_scale(220);
    v = stick_scale(120);
    check(v == 128, "rest at 120 still reads 128 after full travel", v);
    v = stick_scale(30);
    check(v == 0, "full travel one way reads 0", v);
    v = stick_scale(220);
    check(v == 255, "full travel the other way reads 255", v);

    // Uneven travel (more one way than the other) does not move the centre.
    reset();
    stick_scale(128);
    stick_scale(60); stick_scale(230);
    v = stick_scale(128);
    check(v == 128, "uneven travel 60..230 keeps rest at 128", v);

    // Every value from the bottom to the top of travel never steps backwards.
    bool mono = true; int prev = -1;
    for (int x = 30; x <= 220; x++) { int y = stick_scale((uint8_t)x); if (y < prev) mono = false; prev = y; }
    check(mono, "output never steps backwards across travel", prev);

    printf(fails ? "SOME FAILED\n" : "ALL PASS\n");
    return fails ? 1 : 0;
}
