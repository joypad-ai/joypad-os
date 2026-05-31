// ds4_input.c — Sony DualShock 4 parser tests.

#include <joypad/devices/sony/ds4.h>
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

#define DS4_LEN 64

static void build_idle(uint8_t* buf) {
    memset(buf, 0, DS4_LEN);
    buf[0] = JOYPAD_SONY_DS4_INPUT_REPORT_ID;
    buf[1] = 0x80;  // LX
    buf[2] = 0x80;  // LY
    buf[3] = 0x80;  // RX
    buf[4] = 0x80;  // RY
    buf[5] = 0x08;  // dpad center, no face buttons
    buf[6] = 0x00;  // no l1..r3
    buf[7] = 0x00;  // no ps/tpad
    buf[8] = 0x00;  // L2 trigger
    buf[9] = 0x00;  // R2 trigger
}

static void test_vid_pid(void) {
    EXPECT(joypad_is_sony_ds4(0x054c, 0x09cc), "DualShock 4 (v2) VID/PID rejected");
    EXPECT(joypad_is_sony_ds4(0x054c, 0x05c4), "DualShock 4 (v1) VID/PID rejected");
    EXPECT(joypad_is_sony_ds4(0x054c, 0x0ba0), "PS4 Wireless Adapter PC rejected");
    EXPECT(joypad_is_sony_ds4(0x0f0d, 0x005e), "Hori FC4 rejected");
    EXPECT(joypad_is_sony_ds4(0x1532, 0x0401), "Razer Panthera rejected");
    EXPECT(joypad_is_sony_ds4(0x2c22, 0x2300), "Qanba Obsidian rejected");
    EXPECT(joypad_is_sony_ds4(0x0e6f, 0x020a), "Victrix Pro FS PS4 rejected");
    EXPECT(!joypad_is_sony_ds4(0x054c, 0x0ce6), "DS5 should not match DS4");
}

static void test_caps(void) {
    joypad_caps_t c;
    joypad_sony_ds4_caps(&c);
    EXPECT(c.layout == LAYOUT_MODERN_4FACE, "DS4 layout");
    EXPECT(c.button_count == 10, "DS4 button_count");
    EXPECT(c.has_dual_rumble, "DS4 dual rumble");
    EXPECT(!c.has_adaptive_triggers, "DS4 does not have adaptive triggers");
    EXPECT(c.has_lightbar, "DS4 lightbar");
    EXPECT(!c.has_mic_led, "DS4 has no mic LED");
    EXPECT(c.has_touchpad && c.num_touchpoints == 2, "DS4 touchpad");
    EXPECT(c.touch_max_y == 942, "DS4 touchpad max Y");
    EXPECT(c.has_motion && c.has_gyro && c.has_accel, "DS4 IMU");
    EXPECT(c.axes_mask & JOYPAD_AXIS_L2, "DS4 L2 axis");
}

static void test_reject_wrong_id(void) {
    uint8_t buf[DS4_LEN];
    build_idle(buf);
    buf[0] = 0x05;
    input_event_t ev;
    EXPECT(!joypad_parse_sony_ds4(buf, DS4_LEN, &ev), "Wrong report ID rejected");
}

static void test_idle(void) {
    uint8_t buf[DS4_LEN];
    build_idle(buf);
    input_event_t ev;
    EXPECT(joypad_parse_sony_ds4(buf, DS4_LEN, &ev), "Idle parses");
    EXPECT(ev.buttons == 0, "no buttons");
    EXPECT(ev.analog[ANALOG_LX] == 0x80, "LX centered");
    EXPECT(ev.analog[ANALOG_RY] == 0x80, "RY centered");
    EXPECT(ev.has_motion && ev.has_touch, "motion + touch reported");
}

static void test_face_buttons(void) {
    uint8_t buf[DS4_LEN];
    build_idle(buf);
    buf[5] = 0x08 | (1u<<4) | (1u<<5) | (1u<<6) | (1u<<7);  // square+cross+circle+triangle
    input_event_t ev;
    joypad_parse_sony_ds4(buf, DS4_LEN, &ev);
    EXPECT(ev.buttons & JP_BUTTON_B1, "Cross → B1");
    EXPECT(ev.buttons & JP_BUTTON_B2, "Circle → B2");
    EXPECT(ev.buttons & JP_BUTTON_B3, "Square → B3");
    EXPECT(ev.buttons & JP_BUTTON_B4, "Triangle → B4");
}

static void test_shoulders_and_system(void) {
    uint8_t buf[DS4_LEN];
    build_idle(buf);
    buf[6] = 0xff;
    buf[7] = (1u<<0) | (1u<<1);  // ps + tpad
    input_event_t ev;
    joypad_parse_sony_ds4(buf, DS4_LEN, &ev);
    EXPECT(ev.buttons & JP_BUTTON_L1, "L1");
    EXPECT(ev.buttons & JP_BUTTON_R1, "R1");
    EXPECT(ev.buttons & JP_BUTTON_L2, "L2");
    EXPECT(ev.buttons & JP_BUTTON_R2, "R2");
    EXPECT(ev.buttons & JP_BUTTON_S1, "Share");
    EXPECT(ev.buttons & JP_BUTTON_S2, "Option");
    EXPECT(ev.buttons & JP_BUTTON_L3, "L3");
    EXPECT(ev.buttons & JP_BUTTON_R3, "R3");
    EXPECT(ev.buttons & JP_BUTTON_A1, "PS → A1");
    EXPECT(ev.buttons & JP_BUTTON_A2, "Touchpad → A2");
}

static void test_triggers_and_sticks(void) {
    uint8_t buf[DS4_LEN];
    build_idle(buf);
    buf[1] = 0x00; buf[2] = 0xff; buf[3] = 0xff; buf[4] = 0x00;
    buf[8] = 0x80; buf[9] = 0xa0;
    input_event_t ev;
    joypad_parse_sony_ds4(buf, DS4_LEN, &ev);
    EXPECT(ev.analog[ANALOG_LX] == 0x00, "LX min");
    EXPECT(ev.analog[ANALOG_LY] == 0xff, "LY max");
    EXPECT(ev.analog[ANALOG_RX] == 0xff, "RX max");
    EXPECT(ev.analog[ANALOG_RY] == 0x00, "RY min");
    EXPECT(ev.analog[ANALOG_L2] == 0x80, "L2 half");
    EXPECT(ev.analog[ANALOG_R2] == 0xa0, "R2 ~62%");
}

static void test_battery_discharging(void) {
    uint8_t buf[DS4_LEN];
    build_idle(buf);
    // Post-strip offset 29 → full-report offset 30 (= buf[30])
    buf[30] = 0x05;  // cable=0, level=5 → 55%
    input_event_t ev;
    joypad_parse_sony_ds4(buf, DS4_LEN, &ev);
    EXPECT(ev.battery_level == 55, "discharging level 5 → 55%");
    EXPECT(!ev.battery_charging, "not charging");
}

static void test_battery_charging_full(void) {
    uint8_t buf[DS4_LEN];
    build_idle(buf);
    buf[30] = 0x10 | 0x0b;  // cable=1, level=11 → full, not charging
    input_event_t ev;
    joypad_parse_sony_ds4(buf, DS4_LEN, &ev);
    EXPECT(ev.battery_level == 100, "full battery → 100%");
    EXPECT(!ev.battery_charging, "level=11 means cable connected but full, not charging");
}

static void test_feedback(void) {
    joypad_feedback_t fb;
    joypad_feedback_init(&fb);
    fb.rumble_dirty = true;
    fb.rumble_low  = 200;
    fb.rumble_high = 100;
    fb.lightbar_dirty = true;
    fb.lightbar.r = 32; fb.lightbar.g = 64; fb.lightbar.b = 200;

    uint8_t buf[JOYPAD_SONY_DS4_FEEDBACK_PAYLOAD_LEN];
    uint16_t n = joypad_build_sony_ds4_feedback(&fb, buf, sizeof(buf));
    EXPECT(n == sizeof(buf), "full payload written");

    // Layout: flags1, flags2, reserved, motor_right, motor_left, R, G, B, ...
    EXPECT(buf[0] & 0x01, "rumble flag set");
    EXPECT(buf[0] & 0x02, "led flag set");
    EXPECT(buf[3] == 100, "motor_right = high");
    EXPECT(buf[4] == 200, "motor_left = low");
    EXPECT(buf[5] == 32 && buf[6] == 64 && buf[7] == 200, "lightbar RGB");
}

int main(void) {
    test_vid_pid();
    test_caps();
    test_reject_wrong_id();
    test_idle();
    test_face_buttons();
    test_shoulders_and_system();
    test_triggers_and_sticks();
    test_battery_discharging();
    test_battery_charging_full();
    test_feedback();

    if (failures) {
        fprintf(stderr, "\n%d/%d DS4 assertions FAILED\n", failures, total);
        return EXIT_FAILURE;
    }
    printf("%d/%d DS4 assertions OK\n", total, total);
    return EXIT_SUCCESS;
}
