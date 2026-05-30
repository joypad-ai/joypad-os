// smoke.c - libjoypad boundary + contract smoke test.
//
// Compiled by libjoypad's standalone CMake build with -Wall -Wextra -Wpedantic.
// Includes every public header to prove the contract is self-contained: no
// hidden firmware includes, no missing transitive deps, no warnings under
// strict mode.

#include <joypad/input_event.h>
#include <joypad/buttons.h>
#include <joypad/layouts.h>
#include <joypad/capabilities.h>
#include <joypad/feedback.h>

#include <stdio.h>
#include <stdlib.h>

static int fail(const char* msg) {
    fprintf(stderr, "libjoypad smoke FAIL: %s\n", msg);
    return EXIT_FAILURE;
}

int main(void) {
    // input_event_t defaults
    input_event_t e;
    init_input_event(&e);
    if (e.analog[ANALOG_LX] != 128) return fail("LX default != 128");
    if (e.analog[ANALOG_LY] != 128) return fail("LY default != 128");
    if (e.analog[ANALOG_RX] != 128) return fail("RX default != 128");
    if (e.analog[ANALOG_RY] != 128) return fail("RY default != 128");
    if (e.analog[ANALOG_L2] != 0)   return fail("L2 default != 0");
    if (e.analog[ANALOG_R2] != 0)   return fail("R2 default != 0");
    if (e.buttons != 0)             return fail("buttons not cleared");
    if (e.layout != LAYOUT_MODERN_4FACE) return fail("default layout wrong");

    // Layout helpers
    if (!layout_has_6_buttons(LAYOUT_SEGA_6BUTTON)) return fail("SEGA_6BUTTON should have 6");
    if (!layout_has_6_buttons(LAYOUT_PCE_6BUTTON))  return fail("PCE_6BUTTON should have 6");
    if (!layout_has_6_buttons(LAYOUT_ASTROCITY))    return fail("ASTROCITY should have 6");
    if (layout_has_6_buttons(LAYOUT_MODERN_4FACE))  return fail("MODERN_4FACE should not have 6");
    if (!layout_has_3_buttons(LAYOUT_3DO_3BUTTON))  return fail("3DO should have 3");

    // Layout transform pass-through
    if (transform_to_pce_layout(0x1234, LAYOUT_PCE_6BUTTON) != 0x1234)
        return fail("PCE->PCE transform should pass through");

    // Buttons - smoke that JP_BUTTON_* exist and are unique bits
    if ((JP_BUTTON_B1 & JP_BUTTON_B2) != 0) return fail("B1 and B2 should be distinct bits");
    if ((JP_BUTTON_DU & JP_BUTTON_DD) != 0) return fail("DU and DD should be distinct bits");

    // Capabilities
    joypad_caps_t caps;
    joypad_caps_init(&caps);
    if (caps.layout != LAYOUT_UNKNOWN)   return fail("caps default layout wrong");
    if (caps.has_rumble)                 return fail("caps default rumble should be false");
    if (caps.axes_mask != 0)             return fail("caps default axes_mask should be 0");

    caps.vendor_id = 0x054c;
    caps.product_id = 0x0ce6;
    caps.vendor_name = "Sony";
    caps.product_name = "DualSense";
    caps.layout = LAYOUT_MODERN_4FACE;
    caps.axes_mask = JOYPAD_AXIS_LX | JOYPAD_AXIS_LY | JOYPAD_AXIS_RX | JOYPAD_AXIS_RY |
                     JOYPAD_AXIS_L2 | JOYPAD_AXIS_R2;
    caps.has_dual_rumble = true;
    caps.has_adaptive_triggers = true;
    caps.has_lightbar = true;
    caps.has_touchpad = true;
    caps.num_touchpoints = 2;
    caps.has_motion = true;
    caps.has_gyro = true;
    caps.has_accel = true;

    // Feedback
    joypad_feedback_t fb;
    joypad_feedback_init(&fb);
    if (fb.rumble_dirty)                 return fail("feedback default rumble_dirty should be false");
    if (fb.lightbar_dirty)               return fail("feedback default lightbar_dirty should be false");

    fb.rumble_dirty = true;
    fb.rumble_low = 200;
    fb.rumble_high = 100;
    fb.lightbar_dirty = true;
    fb.lightbar = (joypad_rgb_t){10, 20, 30};
    fb.adaptive_right_dirty = true;
    fb.adaptive_right.mode = JOYPAD_TRIGGER_MODE_WEAPON;
    fb.adaptive_right.params[0] = 4;   // start
    fb.adaptive_right.params[1] = 7;   // end
    fb.adaptive_right.params[2] = 8;   // force

    printf("libjoypad smoke OK\n");
    return EXIT_SUCCESS;
}
