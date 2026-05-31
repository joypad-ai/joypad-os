// xinput_input.c — Microsoft XInput state → input_event_t tests.

#include <joypad/devices/microsoft/xinput.h>
#include <joypad/input_event.h>
#include <joypad/buttons.h>
#include <joypad/layouts.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static int total = 0;

#define EXPECT(cond, msg) do {                                          \
    total++;                                                            \
    if (!(cond)) {                                                      \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __func__, __LINE__, msg); \
        failures++;                                                     \
    }                                                                   \
} while (0)

static void test_caps_xbox_one(void) {
    joypad_caps_t c;
    joypad_xinput_caps(JOYPAD_XINPUT_TYPE_XBOX_ONE, &c);
    EXPECT(c.has_dual_rumble, "Xbox One dual rumble");
    EXPECT(c.has_trigger_rumble, "Xbox One impulse triggers");
    EXPECT(c.button_count == 10, "Xbox One 10 buttons");
    EXPECT(c.layout == LAYOUT_MODERN_4FACE, "Xbox One layout");
}

static void test_caps_xbox_og(void) {
    joypad_caps_t c;
    joypad_xinput_caps(JOYPAD_XINPUT_TYPE_XBOX_OG, &c);
    EXPECT(c.has_pressure, "Xbox OG per-button pressure");
    EXPECT(c.button_count == 8, "Xbox OG 8 buttons (no Guide)");
    EXPECT(!c.has_trigger_rumble, "Xbox OG has no trigger rumble");
}

static void test_idle(void) {
    joypad_xinput_gamepad_t pad = {0};
    input_event_t ev;
    joypad_xinput_gamepad_to_event(&pad, JOYPAD_XINPUT_TYPE_XBOX_360_WIRED, &ev);
    EXPECT(ev.buttons == 0, "idle: no buttons");
    EXPECT(ev.type == INPUT_TYPE_GAMEPAD, "idle: type");
    EXPECT(ev.transport == INPUT_TRANSPORT_USB, "idle: transport");
    EXPECT(ev.layout == LAYOUT_MODERN_4FACE, "idle: layout");
    // sThumb*=0 -> byte_scale = ((0 + 32768) / 257) = 127. After Y inversion: 256-127 = 129.
    EXPECT(ev.analog[ANALOG_LX] == 127, "LX center → 127");
    EXPECT(ev.analog[ANALOG_LY] == 129, "LY center → 129 (Y inverted)");
    EXPECT(ev.analog[ANALOG_L2] == 0, "L2 trigger 0");
}

static void test_face_buttons(void) {
    joypad_xinput_gamepad_t pad = {0};
    pad.wButtons = JOYPAD_XINPUT_BTN_A | JOYPAD_XINPUT_BTN_B | JOYPAD_XINPUT_BTN_X | JOYPAD_XINPUT_BTN_Y;
    input_event_t ev;
    joypad_xinput_gamepad_to_event(&pad, JOYPAD_XINPUT_TYPE_XBOX_360_WIRED, &ev);
    EXPECT(ev.buttons & JP_BUTTON_B1, "A → B1 (south)");
    EXPECT(ev.buttons & JP_BUTTON_B2, "B → B2 (east)");
    EXPECT(ev.buttons & JP_BUTTON_B3, "X → B3 (west)");
    EXPECT(ev.buttons & JP_BUTTON_B4, "Y → B4 (north)");
}

static void test_dpad(void) {
    joypad_xinput_gamepad_t pad = {0};
    pad.wButtons = JOYPAD_XINPUT_BTN_DPAD_UP | JOYPAD_XINPUT_BTN_DPAD_RIGHT;
    input_event_t ev;
    joypad_xinput_gamepad_to_event(&pad, JOYPAD_XINPUT_TYPE_XBOX_360_WIRED, &ev);
    EXPECT(ev.buttons & JP_BUTTON_DU, "DU");
    EXPECT(ev.buttons & JP_BUTTON_DR, "DR");
    EXPECT(!(ev.buttons & JP_BUTTON_DD), "no DD");
}

static void test_shoulders_and_system(void) {
    joypad_xinput_gamepad_t pad = {0};
    pad.wButtons = JOYPAD_XINPUT_BTN_LEFT_SHOULDER | JOYPAD_XINPUT_BTN_RIGHT_SHOULDER
                 | JOYPAD_XINPUT_BTN_BACK | JOYPAD_XINPUT_BTN_START
                 | JOYPAD_XINPUT_BTN_LEFT_THUMB | JOYPAD_XINPUT_BTN_RIGHT_THUMB
                 | JOYPAD_XINPUT_BTN_GUIDE | JOYPAD_XINPUT_BTN_SHARE;
    input_event_t ev;
    joypad_xinput_gamepad_to_event(&pad, JOYPAD_XINPUT_TYPE_XBOX_ONE, &ev);
    EXPECT(ev.buttons & JP_BUTTON_L1, "LB → L1");
    EXPECT(ev.buttons & JP_BUTTON_R1, "RB → R1");
    EXPECT(ev.buttons & JP_BUTTON_S1, "Back → S1");
    EXPECT(ev.buttons & JP_BUTTON_S2, "Start → S2");
    EXPECT(ev.buttons & JP_BUTTON_L3, "L3");
    EXPECT(ev.buttons & JP_BUTTON_R3, "R3");
    EXPECT(ev.buttons & JP_BUTTON_A1, "Guide → A1");
    EXPECT(ev.buttons & JP_BUTTON_A2, "Share → A2");
}

static void test_triggers_and_sticks(void) {
    joypad_xinput_gamepad_t pad = {0};
    pad.bLeftTrigger = 0xff;
    pad.bRightTrigger = 0x80;
    pad.sThumbLX = -32768;
    pad.sThumbLY = 32767;
    pad.sThumbRX = 32767;
    pad.sThumbRY = -32768;
    input_event_t ev;
    joypad_xinput_gamepad_to_event(&pad, JOYPAD_XINPUT_TYPE_XBOX_360_WIRED, &ev);
    EXPECT(ev.analog[ANALOG_L2] == 0xff, "L2 full");
    EXPECT(ev.analog[ANALOG_R2] == 0x80, "R2 half");
    EXPECT(ev.analog[ANALOG_LX] == 1, "LX min");        // -32768 -> 0 -> clamped to 1
    EXPECT(ev.analog[ANALOG_LY] == 1, "LY pushed up = HID 1 after inversion (255 → 1 via 256-255)");
    EXPECT(ev.analog[ANALOG_RX] == 255, "RX max");
    EXPECT(ev.analog[ANALOG_RY] == 255, "RY pushed down (XInput min, HID max) → 255");
}

static void test_og_pressure(void) {
    joypad_xinput_gamepad_t pad = {0};
    pad.wButtons = JOYPAD_XINPUT_BTN_A;
    pad.bLeftTrigger  = 0xaa;
    pad.bRightTrigger = 0xbb;
    pad.pressure_a = 0x10;
    pad.pressure_b = 0x20;
    pad.pressure_x = 0x30;
    pad.pressure_y = 0x40;
    pad.pressure_black = 0x50;
    pad.pressure_white = 0x60;
    input_event_t ev;
    joypad_xinput_gamepad_to_event(&pad, JOYPAD_XINPUT_TYPE_XBOX_OG, &ev);
    EXPECT(ev.has_pressure, "OG sets has_pressure");
    EXPECT(ev.button_count == 8, "OG 8 buttons");
    // Canonical pressure[] order: {U,R,D,L, L2,R2,L1,R1, triangle,circle,cross,square}
    EXPECT(ev.pressure[4]  == 0xaa, "OG L2 pressure = LT");
    EXPECT(ev.pressure[5]  == 0xbb, "OG R2 pressure = RT");
    EXPECT(ev.pressure[6]  == 0x60, "OG L1 pressure = white");
    EXPECT(ev.pressure[7]  == 0x50, "OG R1 pressure = black");
    EXPECT(ev.pressure[8]  == 0x40, "OG triangle pressure = Y");
    EXPECT(ev.pressure[9]  == 0x20, "OG circle pressure = B");
    EXPECT(ev.pressure[10] == 0x10, "OG cross pressure = A");
    EXPECT(ev.pressure[11] == 0x30, "OG square pressure = X");
}

int main(void) {
    test_caps_xbox_one();
    test_caps_xbox_og();
    test_idle();
    test_face_buttons();
    test_dpad();
    test_shoulders_and_system();
    test_triggers_and_sticks();
    test_og_pressure();

    if (failures) {
        fprintf(stderr, "\n%d/%d XInput assertions FAILED\n", failures, total);
        return EXIT_FAILURE;
    }
    printf("%d/%d XInput assertions OK\n", total, total);
    return EXIT_SUCCESS;
}
