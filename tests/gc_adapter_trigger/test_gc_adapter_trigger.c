// Host-side unit test for GC Adapter mode digital L/R.
// Mimics the L/R expression in src/usb/usbd/modes/gc_adapter_mode.c so it can
// be checked without a Pico toolchain.
//
// Build: gcc -std=c11 -Wall test_gc_adapter_trigger.c -o test_gc_adapter_trigger && ./test_gc_adapter_trigger
//        add -DOLD_BUTTON_ONLY to model the old expression (button bit only)
// Pass criterion: prints "ALL PASS" and exits 0.

#include <stdio.h>
#include <stdint.h>

#define JP_BUTTON_L2 (1u << 6)
#define GC_ADAPTER_TRIGGER_DIGITAL_THRESHOLD 250

static int l_digital(uint32_t buttons, uint8_t l2_analog)
{
#ifdef OLD_BUTTON_ONLY
    (void)l2_analog;
    return (buttons & JP_BUTTON_L2) ? 1 : 0;
#else
    return ((buttons & JP_BUTTON_L2) ||
            l2_analog >= GC_ADAPTER_TRIGGER_DIGITAL_THRESHOLD) ? 1 : 0;
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
    check(l_digital(0, 255) == 1, "analog-only trigger fully pulled gives digital L");
    check(l_digital(0, 250) == 1, "analog at the threshold gives digital L");
    check(l_digital(0, 200) == 0, "a partial pull stays analog only");
    check(l_digital(0, 0) == 0, "released trigger gives nothing");
    check(l_digital(JP_BUTTON_L2, 0) == 1, "a pad with a real click still sets L");
    printf("%s\n", fails ? "SOME FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
