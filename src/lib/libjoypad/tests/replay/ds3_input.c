// ds3_input.c — Sony DualShock 3 (SIXAXIS) parser tests.

#include <joypad/devices/sony/ds3.h>
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

#define DS3_LEN 80

static void build_idle(uint8_t* buf) {
    memset(buf, 0, DS3_LEN);
    buf[0] = JOYPAD_SONY_DS3_INPUT_REPORT_ID;
    // Post-strip:
    //   0: buttons byte 1 (select/l3/r3/start/dpad)
    //   1: buttons byte 2 (l2/r2/l1/r1/face)
    //   2: ps
    //   3: not_used
    //   4..7: lx, ly, rx, ry
    //   8..19: pressure[12]
    //   20..55: unused[36] (face button pressures at [20..23])
    //   ...
    //   40..47 (post-strip) = motion bytes (big-endian, centered at 512)
    buf[1+4] = 0x80; buf[1+5] = 0x80; buf[1+6] = 0x80; buf[1+7] = 0x80;
    // Motion centers: 512 = 0x02 0x00 big-endian
    for (int i = 0; i < 4; i++) {
        buf[1 + 40 + i*2 + 0] = 0x02;
        buf[1 + 40 + i*2 + 1] = 0x00;
    }
}

static void test_vid_pid(void) {
    EXPECT(joypad_is_sony_ds3(0x054c, 0x0268), "DS3 VID/PID rejected");
    EXPECT(!joypad_is_sony_ds3(0x054c, 0x09cc), "DS4 should not match DS3");
    EXPECT(!joypad_is_sony_ds3(0x054c, 0x0ce6), "DS5 should not match DS3");
}

static void test_caps(void) {
    joypad_caps_t c;
    joypad_sony_ds3_caps(&c);
    EXPECT(c.has_pressure, "DS3 has pressure-sensitive buttons");
    EXPECT(c.gyro_range_dps == 100, "DS3 gyro range ±100 dps");
    EXPECT(c.accel_range_milli_g == 2000, "DS3 accel range ±2g");
    EXPECT(c.has_player_leds && c.num_player_leds == 4, "DS3 4 player LEDs");
    EXPECT(!c.has_lightbar, "DS3 has no lightbar");
    EXPECT(!c.has_touchpad, "DS3 has no touchpad");
    EXPECT(c.button_count == 10, "10 buttons");
}

static void test_reject_wrong_id(void) {
    uint8_t buf[DS3_LEN];
    build_idle(buf);
    buf[0] = 0x02;
    input_event_t ev;
    EXPECT(!joypad_parse_sony_ds3(buf, DS3_LEN, &ev), "wrong report ID rejected");
}

static void test_idle(void) {
    uint8_t buf[DS3_LEN];
    build_idle(buf);
    input_event_t ev;
    EXPECT(joypad_parse_sony_ds3(buf, DS3_LEN, &ev), "idle parses");
    EXPECT(ev.buttons == 0, "no buttons");
    EXPECT(ev.analog[ANALOG_LX] == 128, "LX centered");
    EXPECT(ev.has_pressure, "pressure reported");
    EXPECT(ev.has_motion, "motion reported");
    EXPECT(ev.gyro[0] == 0 && ev.gyro[1] == 0, "X/Y gyro = 0 (DS3 only has Z)");
    // accel center = 512 → after normalization = 0
    EXPECT(ev.accel[0] == 0 && ev.accel[1] == 0 && ev.accel[2] == 0, "accel center → 0");
}

static void test_face_buttons(void) {
    uint8_t buf[DS3_LEN];
    build_idle(buf);
    // Buttons byte 2 (post-strip offset 1): bits 4=triangle, 5=circle, 6=cross, 7=square
    buf[1+1] = (1u<<4) | (1u<<5) | (1u<<6) | (1u<<7);
    input_event_t ev;
    joypad_parse_sony_ds3(buf, DS3_LEN, &ev);
    EXPECT(ev.buttons & JP_BUTTON_B1, "Cross → B1");
    EXPECT(ev.buttons & JP_BUTTON_B2, "Circle → B2");
    EXPECT(ev.buttons & JP_BUTTON_B3, "Square → B3");
    EXPECT(ev.buttons & JP_BUTTON_B4, "Triangle → B4");
}

static void test_dpad(void) {
    uint8_t buf[DS3_LEN];
    build_idle(buf);
    // Buttons byte 1 bits: 4=up, 5=right, 6=down, 7=left
    buf[1+0] = (1u<<4);
    input_event_t ev;
    joypad_parse_sony_ds3(buf, DS3_LEN, &ev);
    EXPECT(ev.buttons & JP_BUTTON_DU, "Up");

    build_idle(buf);
    buf[1+0] = (1u<<6);
    joypad_parse_sony_ds3(buf, DS3_LEN, &ev);
    EXPECT(ev.buttons & JP_BUTTON_DD, "Down");
}

static void test_pressure_layout(void) {
    uint8_t buf[DS3_LEN];
    build_idle(buf);
    // pressure[0..11] starts at post-strip offset 8 → buf[1+8]..buf[1+19]
    for (int i = 0; i < 12; i++) buf[1 + 8 + i] = (uint8_t)(i + 10);
    // Face button pressures at unused[0..3] → post-strip offset 20..23 → buf[1+20]..
    buf[1+20] = 50; buf[1+21] = 60; buf[1+22] = 70; buf[1+23] = 80;
    input_event_t ev;
    joypad_parse_sony_ds3(buf, DS3_LEN, &ev);
    // Canonical order: up, right, down, left, L2, R2, L1, R1, triangle, circle, cross, square
    EXPECT(ev.pressure[0] == 14, "up pressure = pressure[4] = 14");
    EXPECT(ev.pressure[3] == 17, "left pressure = pressure[7] = 17");
    EXPECT(ev.pressure[4] == 18, "L2 pressure = pressure[8] = 18");
    EXPECT(ev.pressure[7] == 21, "R1 pressure = pressure[11] = 21");
    EXPECT(ev.pressure[8] == 50, "triangle pressure = unused[0] = 50");
    EXPECT(ev.pressure[11] == 80, "square pressure = unused[3] = 80");
}

static void test_pressure_drives_triggers(void) {
    uint8_t buf[DS3_LEN];
    build_idle(buf);
    buf[1 + 8 + 8] = 0xC0;   // pressure[8] = L2
    buf[1 + 8 + 9] = 0x40;   // pressure[9] = R2
    input_event_t ev;
    joypad_parse_sony_ds3(buf, DS3_LEN, &ev);
    EXPECT(ev.analog[ANALOG_L2] == 0xC0, "L2 analog drives from pressure[8]");
    EXPECT(ev.analog[ANALOG_R2] == 0x40, "R2 analog drives from pressure[9]");
}

static void test_battery_charging(void) {
    uint8_t buf[DS3_LEN];
    build_idle(buf);
    // Battery at post-strip offset 29 → buf[1+29] = buf[30]
    buf[30] = 0xEE;  // charging
    input_event_t ev;
    joypad_parse_sony_ds3(buf, DS3_LEN, &ev);
    EXPECT(ev.battery_level == 100, "charging → reported as 100%");
    EXPECT(ev.battery_charging, "0xEE is charging");

    build_idle(buf);
    buf[30] = 0xEF;
    joypad_parse_sony_ds3(buf, DS3_LEN, &ev);
    EXPECT(!ev.battery_charging, "0xEF is full, not charging");
}

static void test_battery_levels(void) {
    uint8_t buf[DS3_LEN];
    build_idle(buf);
    buf[30] = 3;
    input_event_t ev;
    joypad_parse_sony_ds3(buf, DS3_LEN, &ev);
    EXPECT(ev.battery_level == 50, "raw 3 → 50%");
}

int main(void) {
    test_vid_pid();
    test_caps();
    test_reject_wrong_id();
    test_idle();
    test_face_buttons();
    test_dpad();
    test_pressure_layout();
    test_pressure_drives_triggers();
    test_battery_charging();
    test_battery_levels();

    if (failures) {
        fprintf(stderr, "\n%d/%d DS3 assertions FAILED\n", failures, total);
        return EXIT_FAILURE;
    }
    printf("%d/%d DS3 assertions OK\n", total, total);
    return EXIT_SUCCESS;
}
