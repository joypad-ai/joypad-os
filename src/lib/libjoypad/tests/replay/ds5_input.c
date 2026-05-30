// ds5_input.c
// Sony DualSense parser test using hand-crafted fixtures.
//
// These fixtures replicate the on-the-wire layout of report ID 0x01 produced
// by a real DualSense over USB. Real captured fixtures (from Comrade / USBPcap)
// will be added under tests/fixtures/sony_ds5/ when a physical device is
// connected; the runner shape stays the same.

#include <joypad/devices/sony/ds5.h>
#include <joypad/input_event.h>
#include <joypad/buttons.h>
#include <joypad/layouts.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
static int total    = 0;

#define EXPECT(cond, msg) do {                                          \
    total++;                                                            \
    if (!(cond)) {                                                      \
        fprintf(stderr, "FAIL [%s:%d]: %s\n", __func__, __LINE__, msg); \
        failures++;                                                     \
    }                                                                   \
} while (0)

// ----------------------------------------------------------------------------
// Fixture builder
// ----------------------------------------------------------------------------
// Builds a DS5 USB input report with report ID 0x01 and all-default values
// (sticks centered, no buttons, no touchpad activity). Test bodies tweak
// specific bytes to exercise individual paths.

#define DS5_REPORT_LEN  64  // safe upper bound including battery byte

static void build_idle_report(uint8_t* buf) {
    memset(buf, 0, DS5_REPORT_LEN);
    buf[0] = JOYPAD_SONY_DS5_INPUT_REPORT_ID;
    // Post-strip offsets:
    //   0..6: x1, y1, x2, y2, rx, ry, rz
    //   7:    dpad nibble + face button bits
    //   8:    l1..r3 bits
    //   9:    ps/tpad/mute/counter
    //   10..15: gyro
    //   16..21: accel
    buf[1] = 0x80;   // x1 centered
    buf[2] = 0x80;   // y1 centered
    buf[3] = 0x80;   // x2 centered
    buf[4] = 0x80;   // y2 centered
    buf[5] = 0x00;   // L2 (rx) released
    buf[6] = 0x00;   // R2 (ry) released
    buf[7] = 0x00;   // rz
    buf[8] = 0x08;   // dpad center (8), all face buttons off
    buf[9] = 0x00;   // shoulders/start/select/stick clicks off
    buf[10] = 0x00;  // ps/tpad/mute off, counter 0
}

// ----------------------------------------------------------------------------
// Tests
// ----------------------------------------------------------------------------

static void test_vid_pid(void) {
    EXPECT(joypad_is_sony_ds5(0x054c, 0x0ce6), "Sony DualSense VID/PID rejected");
    EXPECT(joypad_is_sony_ds5(0x0e6f, 0x0209), "Victrix Pro FS for PS5 VID/PID rejected");
    EXPECT(!joypad_is_sony_ds5(0x054c, 0x09cc), "DS4 VID/PID should not match DS5");
    EXPECT(!joypad_is_sony_ds5(0x045e, 0x028e), "Xbox 360 VID/PID should not match DS5");
}

static void test_caps(void) {
    joypad_caps_t c;
    joypad_sony_ds5_caps(&c);
    EXPECT(c.layout == LAYOUT_MODERN_4FACE,                   "DS5 layout should be MODERN_4FACE");
    EXPECT(c.has_motion && c.has_gyro && c.has_accel,         "DS5 should report full motion caps");
    EXPECT(c.has_touchpad && c.num_touchpoints == 2,          "DS5 should report 2-finger touchpad");
    EXPECT(c.has_dual_rumble,                                 "DS5 should report dual rumble");
    EXPECT(c.has_adaptive_triggers,                           "DS5 should report adaptive triggers");
    EXPECT(c.has_lightbar,                                    "DS5 should report lightbar");
    EXPECT(c.has_mic_led,                                     "DS5 should report mic LED");
    EXPECT(c.axes_mask & JOYPAD_AXIS_L2,                      "DS5 should expose L2 axis");
    EXPECT(c.axes_mask & JOYPAD_AXIS_R2,                      "DS5 should expose R2 axis");
}

static void test_reject_wrong_report_id(void) {
    uint8_t buf[DS5_REPORT_LEN];
    build_idle_report(buf);
    buf[0] = 0x02;  // not the input report ID

    input_event_t ev;
    EXPECT(!joypad_parse_sony_ds5(buf, DS5_REPORT_LEN, &ev), "Parser should reject non-input report ID");
}

static void test_reject_short_report(void) {
    uint8_t buf[8] = {JOYPAD_SONY_DS5_INPUT_REPORT_ID, 0x80, 0x80, 0x80, 0x80, 0x00, 0x00, 0x00};
    input_event_t ev;
    EXPECT(!joypad_parse_sony_ds5(buf, sizeof(buf), &ev), "Parser should reject truncated report");
}

static void test_idle_state(void) {
    uint8_t buf[DS5_REPORT_LEN];
    build_idle_report(buf);

    input_event_t ev;
    EXPECT(joypad_parse_sony_ds5(buf, DS5_REPORT_LEN, &ev), "Idle report should parse");
    EXPECT(ev.type == INPUT_TYPE_GAMEPAD,        "Idle: type should be GAMEPAD");
    EXPECT(ev.transport == INPUT_TRANSPORT_USB,  "Idle: transport should be USB");
    EXPECT(ev.layout == LAYOUT_MODERN_4FACE,     "Idle: layout should be MODERN_4FACE");
    EXPECT(ev.buttons == 0,                      "Idle: no buttons should be pressed");
    EXPECT(ev.analog[ANALOG_LX] == 0x80,         "Idle: LX should be centered");
    EXPECT(ev.analog[ANALOG_LY] == 0x80,         "Idle: LY should be centered");
    EXPECT(ev.analog[ANALOG_RX] == 0x80,         "Idle: RX should be centered");
    EXPECT(ev.analog[ANALOG_RY] == 0x80,         "Idle: RY should be centered");
    EXPECT(ev.analog[ANALOG_L2] == 0x00,         "Idle: L2 should be 0");
    EXPECT(ev.analog[ANALOG_R2] == 0x00,         "Idle: R2 should be 0");
    EXPECT(ev.has_motion,                        "Idle: has_motion should be true");
    EXPECT(ev.gyro_range == 2000,                "Idle: gyro range should be 2000 dps");
    EXPECT(ev.accel_range == 4000,               "Idle: accel range should be 4000 milli-g");
    EXPECT(ev.has_touch,                         "Idle: has_touch should be true");
}

static void test_face_buttons(void) {
    uint8_t buf[DS5_REPORT_LEN];
    build_idle_report(buf);
    // Byte 8 bits: [3:0]=dpad nibble (8=center), [4]=square, [5]=cross, [6]=circle, [7]=triangle
    buf[8] = 0x08 | (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7);

    input_event_t ev;
    EXPECT(joypad_parse_sony_ds5(buf, DS5_REPORT_LEN, &ev), "Face-buttons-pressed report should parse");
    EXPECT((ev.buttons & JP_BUTTON_B1) != 0, "Cross should map to B1");
    EXPECT((ev.buttons & JP_BUTTON_B2) != 0, "Circle should map to B2");
    EXPECT((ev.buttons & JP_BUTTON_B3) != 0, "Square should map to B3");
    EXPECT((ev.buttons & JP_BUTTON_B4) != 0, "Triangle should map to B4");
    EXPECT((ev.buttons & (JP_BUTTON_DU|JP_BUTTON_DD|JP_BUTTON_DL|JP_BUTTON_DR)) == 0,
           "D-pad centered: no DU/DD/DL/DR bits should be set");
}

static void test_dpad_cardinals(void) {
    uint8_t buf[DS5_REPORT_LEN];

    // North (0): DU only
    build_idle_report(buf);
    buf[8] = 0x00;
    input_event_t ev;
    EXPECT(joypad_parse_sony_ds5(buf, DS5_REPORT_LEN, &ev), "DPad N report should parse");
    EXPECT((ev.buttons & JP_BUTTON_DU) != 0 && (ev.buttons & JP_BUTTON_DD) == 0,
           "DPad N: DU only");

    // South (4): DD only
    build_idle_report(buf);
    buf[8] = 0x04;
    EXPECT(joypad_parse_sony_ds5(buf, DS5_REPORT_LEN, &ev), "DPad S report should parse");
    EXPECT((ev.buttons & JP_BUTTON_DD) != 0 && (ev.buttons & JP_BUTTON_DU) == 0,
           "DPad S: DD only");

    // East (2): DR only
    build_idle_report(buf);
    buf[8] = 0x02;
    EXPECT(joypad_parse_sony_ds5(buf, DS5_REPORT_LEN, &ev), "DPad E report should parse");
    EXPECT((ev.buttons & JP_BUTTON_DR) != 0 && (ev.buttons & JP_BUTTON_DL) == 0,
           "DPad E: DR only");

    // West (6): DL only
    build_idle_report(buf);
    buf[8] = 0x06;
    EXPECT(joypad_parse_sony_ds5(buf, DS5_REPORT_LEN, &ev), "DPad W report should parse");
    EXPECT((ev.buttons & JP_BUTTON_DL) != 0 && (ev.buttons & JP_BUTTON_DR) == 0,
           "DPad W: DL only");

    // NE (1): DU + DR
    build_idle_report(buf);
    buf[8] = 0x01;
    EXPECT(joypad_parse_sony_ds5(buf, DS5_REPORT_LEN, &ev), "DPad NE report should parse");
    EXPECT((ev.buttons & JP_BUTTON_DU) != 0 && (ev.buttons & JP_BUTTON_DR) != 0,
           "DPad NE: DU + DR");
}

static void test_shoulder_buttons(void) {
    uint8_t buf[DS5_REPORT_LEN];
    build_idle_report(buf);
    buf[9] = 0xff;  // all bits in this byte set

    input_event_t ev;
    EXPECT(joypad_parse_sony_ds5(buf, DS5_REPORT_LEN, &ev), "All-shoulders report should parse");
    EXPECT(ev.buttons & JP_BUTTON_L1, "L1 should be set");
    EXPECT(ev.buttons & JP_BUTTON_R1, "R1 should be set");
    EXPECT(ev.buttons & JP_BUTTON_L2, "L2 should be set");
    EXPECT(ev.buttons & JP_BUTTON_R2, "R2 should be set");
    EXPECT(ev.buttons & JP_BUTTON_S1, "Share should map to S1");
    EXPECT(ev.buttons & JP_BUTTON_S2, "Option should map to S2");
    EXPECT(ev.buttons & JP_BUTTON_L3, "L3 should be set");
    EXPECT(ev.buttons & JP_BUTTON_R3, "R3 should be set");
}

static void test_analog_full_deflection(void) {
    uint8_t buf[DS5_REPORT_LEN];
    build_idle_report(buf);
    buf[1] = 0x00;   // LX left
    buf[2] = 0xff;   // LY full down
    buf[3] = 0xff;   // RX full right
    buf[4] = 0x00;   // RY full up
    buf[5] = 0xff;   // L2 fully pressed
    buf[6] = 0xff;   // R2 fully pressed

    input_event_t ev;
    EXPECT(joypad_parse_sony_ds5(buf, DS5_REPORT_LEN, &ev), "Analog-deflection report should parse");
    EXPECT(ev.analog[ANALOG_LX] == 0x00, "LX should pass through 0x00");
    EXPECT(ev.analog[ANALOG_LY] == 0xff, "LY should pass through 0xff");
    EXPECT(ev.analog[ANALOG_RX] == 0xff, "RX should pass through 0xff");
    EXPECT(ev.analog[ANALOG_RY] == 0x00, "RY should pass through 0x00");
    EXPECT(ev.analog[ANALOG_L2] == 0xff, "L2 should pass through 0xff");
    EXPECT(ev.analog[ANALOG_R2] == 0xff, "R2 should pass through 0xff");
}

static void test_ps_tpad_mute(void) {
    uint8_t buf[DS5_REPORT_LEN];
    build_idle_report(buf);
    buf[10] = 0x01 | 0x02 | 0x04;  // ps + tpad + mute

    input_event_t ev;
    EXPECT(joypad_parse_sony_ds5(buf, DS5_REPORT_LEN, &ev), "PS/TPad/Mute report should parse");
    EXPECT(ev.buttons & JP_BUTTON_A1, "PS should map to A1");
    EXPECT(ev.buttons & JP_BUTTON_A2, "TPad click should map to A2");
    EXPECT(ev.buttons & JP_BUTTON_A3, "Mute should map to A3");
}

static void test_battery_discharging(void) {
    uint8_t buf[DS5_REPORT_LEN];
    build_idle_report(buf);
    // Battery byte is at post-strip offset 52 → buf[1+52] = buf[53]
    // Nibble[0..3] = level (0-10), nibble[4..7] = status (0=discharging)
    buf[53] = 0x00 | 0x05;  // status=0 (discharging), level=5

    input_event_t ev;
    EXPECT(joypad_parse_sony_ds5(buf, DS5_REPORT_LEN, &ev), "Battery-discharging report should parse");
    EXPECT(ev.battery_level == (5*10 + 5), "Discharging level 5 → ~55%");
    EXPECT(!ev.battery_charging,            "Discharging should set charging=false");
}

static void test_battery_charging_full(void) {
    uint8_t buf[DS5_REPORT_LEN];
    build_idle_report(buf);
    buf[53] = 0x20;  // status=2 (full), level ignored

    input_event_t ev;
    EXPECT(joypad_parse_sony_ds5(buf, DS5_REPORT_LEN, &ev), "Battery-full report should parse");
    EXPECT(ev.battery_level == 100, "Full battery → 100%");
}

static void test_feedback_rumble_lightbar(void) {
    joypad_feedback_t fb;
    joypad_feedback_init(&fb);
    fb.rumble_dirty = true;
    fb.rumble_low = 200;
    fb.rumble_high = 100;
    fb.lightbar_dirty = true;
    fb.lightbar.r = 64;
    fb.lightbar.g = 128;
    fb.lightbar.b = 255;

    uint8_t buf[JOYPAD_SONY_DS5_FEEDBACK_PAYLOAD_LEN];
    uint16_t n = joypad_build_sony_ds5_feedback(&fb, buf, sizeof(buf));
    EXPECT(n == sizeof(buf), "Feedback build should fill full payload length");

    // Layout: flags(2) rumble_r(1) rumble_l(1) unk(4) mic_led(1) unk(1) trigger_r(11) trigger_l(11) unk(11) player_led(1) RGB(3)
    EXPECT(buf[2] == 100, "rumble_r at offset 2 should be rumble_high");
    EXPECT(buf[3] == 200, "rumble_l at offset 3 should be rumble_low");
    EXPECT(buf[44] == 64,  "lightbar_r at offset 44");
    EXPECT(buf[45] == 128, "lightbar_g at offset 45");
    EXPECT(buf[46] == 255, "lightbar_b at offset 46");

    // Flags should have rumble + lightbar bits set
    uint16_t flags = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    EXPECT(flags & (1u << 10), "lightbar enable bit should be set");
    EXPECT(flags & 0x07,        "rumble emulation low-nibble pattern should be set");
}

static void test_feedback_buffer_too_small(void) {
    joypad_feedback_t fb;
    joypad_feedback_init(&fb);
    fb.rumble_dirty = true;
    fb.rumble_low = 100;

    uint8_t small[10];
    EXPECT(joypad_build_sony_ds5_feedback(&fb, small, sizeof(small)) == 0,
           "Feedback build should return 0 for too-small buffer");
}

static void test_feedback_player_index(void) {
    joypad_feedback_t fb;
    joypad_feedback_init(&fb);
    fb.player_index_dirty = true;
    fb.player_index = 1;

    uint8_t buf[JOYPAD_SONY_DS5_FEEDBACK_PAYLOAD_LEN];
    uint16_t n = joypad_build_sony_ds5_feedback(&fb, buf, sizeof(buf));
    EXPECT(n > 0, "Feedback build should succeed");
    // Player 1 = center LED only (0x04 per the chosen pattern table)
    EXPECT(buf[43] == 0x04, "Player 1 should light the center LED");
}

int main(void) {
    test_vid_pid();
    test_caps();
    test_reject_wrong_report_id();
    test_reject_short_report();
    test_idle_state();
    test_face_buttons();
    test_dpad_cardinals();
    test_shoulder_buttons();
    test_analog_full_deflection();
    test_ps_tpad_mute();
    test_battery_discharging();
    test_battery_charging_full();
    test_feedback_rumble_lightbar();
    test_feedback_buffer_too_small();
    test_feedback_player_index();

    if (failures) {
        fprintf(stderr, "\n%d/%d DS5 assertions FAILED\n", failures, total);
        return EXIT_FAILURE;
    }
    printf("%d/%d DS5 assertions OK\n", total, total);
    return EXIT_SUCCESS;
}
