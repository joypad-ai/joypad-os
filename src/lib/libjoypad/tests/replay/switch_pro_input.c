// switch_pro_input.c
// Switch Pro / Joy-Con USB input parser tests.

#include <joypad/devices/nintendo/switch_pro.h>
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

// Build a 0x30 input report with sticks centered at 2048 and no buttons.
// Layout offsets after report_id (byte 0):
//   1:  timer
//   2:  battery + conn
//   3:  buttons byte 1 (face + right shoulder)
//   4:  buttons byte 2 (system + stick clicks)
//   5:  buttons byte 3 (dpad + left shoulder)
//   6..8:  left_stick   [3 bytes packed 12+12]
//   9..11: right_stick  [3 bytes packed 12+12]
//   12: vibration_ack

#define SWP_REPORT_LEN 64

static void pack_stick(uint16_t x, uint16_t y, uint8_t* out) {
    out[0] = (uint8_t)(x & 0xff);
    out[1] = (uint8_t)(((x >> 8) & 0x0f) | ((y & 0x0f) << 4));
    out[2] = (uint8_t)((y >> 4) & 0xff);
}

static void build_idle_report(uint8_t* buf) {
    memset(buf, 0, SWP_REPORT_LEN);
    buf[0] = JOYPAD_NINTENDO_SWITCH_PRO_INPUT_REPORT_ID;
    buf[1] = 0x00;  // timer
    buf[2] = 0x80;  // battery + conn: level 8 (full), no charging
    pack_stick(2048, 2048, buf + 6);
    pack_stick(2048, 2048, buf + 9);
}

static void test_vid_pid(void) {
    EXPECT(joypad_is_nintendo_switch_pro(0x057e, 0x2009), "Pro Controller VID/PID rejected");
    EXPECT(joypad_is_nintendo_switch_pro(0x057e, 0x200e), "Joy-Con Charging Grip VID/PID rejected");
    EXPECT(joypad_is_nintendo_switch_pro(0x057e, 0x2017), "SNES NSO VID/PID rejected");
    EXPECT(!joypad_is_nintendo_switch_pro(0x057e, 0x1234), "Random Nintendo PID should not match");
    EXPECT(!joypad_is_nintendo_switch_pro(0x054c, 0x0ce6), "Sony DS5 should not match Switch");
}

static void test_caps(void) {
    joypad_caps_t c;
    joypad_nintendo_switch_pro_caps(&c);
    EXPECT(c.layout == LAYOUT_NINTENDO_4FACE,            "Switch Pro layout should be NINTENDO_4FACE");
    EXPECT(c.button_count == 10,                         "Switch Pro button_count should be 10");
    EXPECT(c.has_dpad,                                   "Switch Pro should report d-pad");
    EXPECT(c.has_dual_rumble,                            "Switch Pro should report dual rumble (HD Rumble)");
    EXPECT(c.has_player_leds && c.num_player_leds == 4,  "Switch Pro should expose 4 player LEDs");
    EXPECT(c.has_motion && c.has_gyro && c.has_accel,    "Switch Pro IMU should be reported");
    EXPECT(c.axes_mask == (JOYPAD_AXIS_LX | JOYPAD_AXIS_LY | JOYPAD_AXIS_RX | JOYPAD_AXIS_RY),
           "Switch Pro should expose 4 stick axes only (no analog triggers)");
}

static void test_reject_wrong_report_id(void) {
    uint8_t buf[SWP_REPORT_LEN];
    build_idle_report(buf);
    buf[0] = 0x81;  // not 0x30 or 0x21
    input_event_t ev;
    EXPECT(!joypad_parse_nintendo_switch_pro(buf, SWP_REPORT_LEN, &ev), "Parser should reject non-input report ID");
}

static void test_reject_short(void) {
    uint8_t buf[10] = {0x30};
    input_event_t ev;
    EXPECT(!joypad_parse_nintendo_switch_pro(buf, sizeof(buf), &ev), "Parser should reject truncated report");
}

static void test_idle_centered(void) {
    uint8_t buf[SWP_REPORT_LEN];
    build_idle_report(buf);
    input_event_t ev;
    EXPECT(joypad_parse_nintendo_switch_pro(buf, SWP_REPORT_LEN, &ev), "Idle report should parse");
    EXPECT(ev.type == INPUT_TYPE_GAMEPAD,        "type should be GAMEPAD");
    EXPECT(ev.transport == INPUT_TRANSPORT_USB,  "transport should be USB");
    EXPECT(ev.layout == LAYOUT_NINTENDO_4FACE,   "layout should be NINTENDO_4FACE");
    EXPECT(ev.buttons == 0,                      "no buttons should be pressed");
    EXPECT(ev.analog[ANALOG_LX] == 128,          "LX centered → 128");
    // Y-axes get inverted via (255 - X), which maps centered X=128 to 127
    // (asymmetric 0..255 range with center 128 — one tick off either way).
    // Tolerate ±1 from 128 for centered Y.
    EXPECT(ev.analog[ANALOG_LY] == 127 || ev.analog[ANALOG_LY] == 128, "LY centered → 127 or 128 (Y-inversion quirk)");
    EXPECT(ev.analog[ANALOG_RX] == 128,          "RX centered → 128");
    EXPECT(ev.analog[ANALOG_RY] == 127 || ev.analog[ANALOG_RY] == 128, "RY centered → 127 or 128 (Y-inversion quirk)");
}

static void test_battery(void) {
    uint8_t buf[SWP_REPORT_LEN];
    build_idle_report(buf);
    buf[2] = 0x08;  // level=0, charging bit set
    input_event_t ev;
    joypad_parse_nintendo_switch_pro(buf, SWP_REPORT_LEN, &ev);
    EXPECT(ev.battery_charging, "Charging bit should be detected");

    buf[2] = 0x80;  // level=8 (full), no charging
    joypad_parse_nintendo_switch_pro(buf, SWP_REPORT_LEN, &ev);
    EXPECT(ev.battery_level == (uint8_t)(8 * 12 + 5), "Level 8 → ~101% clamped formula");
    EXPECT(!ev.battery_charging, "Not charging when bit clear");
}

static void test_face_buttons(void) {
    uint8_t buf[SWP_REPORT_LEN];
    build_idle_report(buf);
    // Byte 3 (buttons byte 1): bits 0=y, 1=x, 2=b, 3=a
    buf[3] = 0x01 | 0x02 | 0x04 | 0x08;
    input_event_t ev;
    EXPECT(joypad_parse_nintendo_switch_pro(buf, SWP_REPORT_LEN, &ev), "Face buttons report should parse");
    EXPECT(ev.buttons & JP_BUTTON_B1, "B → B1");
    EXPECT(ev.buttons & JP_BUTTON_B2, "A → B2");
    EXPECT(ev.buttons & JP_BUTTON_B3, "Y → B3");
    EXPECT(ev.buttons & JP_BUTTON_B4, "X → B4");
}

static void test_dpad(void) {
    uint8_t buf[SWP_REPORT_LEN];
    // Byte 5 (buttons byte 3): bits 0=down,1=up,2=right,3=left
    build_idle_report(buf);
    buf[5] = 0x02;  // up only
    input_event_t ev;
    joypad_parse_nintendo_switch_pro(buf, SWP_REPORT_LEN, &ev);
    EXPECT((ev.buttons & JP_BUTTON_DU) && !(ev.buttons & JP_BUTTON_DD), "Up only");

    build_idle_report(buf);
    buf[5] = 0x04;
    joypad_parse_nintendo_switch_pro(buf, SWP_REPORT_LEN, &ev);
    EXPECT(ev.buttons & JP_BUTTON_DR, "Right");

    build_idle_report(buf);
    buf[5] = 0x08;
    joypad_parse_nintendo_switch_pro(buf, SWP_REPORT_LEN, &ev);
    EXPECT(ev.buttons & JP_BUTTON_DL, "Left");
}

static void test_shoulders_and_system(void) {
    uint8_t buf[SWP_REPORT_LEN];
    build_idle_report(buf);
    // byte 3: bits 6=R, 7=ZR
    buf[3] = (1u << 6) | (1u << 7);
    // byte 5: bits 6=L, 7=ZL
    buf[5] = (1u << 6) | (1u << 7);
    // byte 4: bits 0=select, 1=start, 2=rstick, 3=lstick, 4=home, 5=cap
    buf[4] = 0x3f;
    input_event_t ev;
    EXPECT(joypad_parse_nintendo_switch_pro(buf, SWP_REPORT_LEN, &ev), "All-shoulder+system should parse");
    EXPECT(ev.buttons & JP_BUTTON_R1, "R → R1");
    EXPECT(ev.buttons & JP_BUTTON_R2, "ZR → R2");
    EXPECT(ev.buttons & JP_BUTTON_L1, "L → L1");
    EXPECT(ev.buttons & JP_BUTTON_L2, "ZL → L2");
    EXPECT(ev.buttons & JP_BUTTON_S1, "- → S1");
    EXPECT(ev.buttons & JP_BUTTON_S2, "+ → S2");
    EXPECT(ev.buttons & JP_BUTTON_L3, "LStick");
    EXPECT(ev.buttons & JP_BUTTON_R3, "RStick");
    EXPECT(ev.buttons & JP_BUTTON_A1, "Home → A1");
    EXPECT(ev.buttons & JP_BUTTON_A2, "Capture → A2");
}

static void test_stick_full_deflection(void) {
    uint8_t buf[SWP_REPORT_LEN];
    build_idle_report(buf);
    // Push left stick to 4095/0, right stick to 0/4095
    pack_stick(4095, 0, buf + 6);
    pack_stick(0, 4095, buf + 9);
    input_event_t ev;
    EXPECT(joypad_parse_nintendo_switch_pro(buf, SWP_REPORT_LEN, &ev), "Full-deflection report should parse");
    // LX raw=4095 → centered=+2047 → scaled = 2047*127/1600 = 162 (clamped to 127) → 255
    EXPECT(ev.analog[ANALOG_LX] == 255, "LX max → 255");
    // LY raw=0 → centered=-2048 → scaled = -2048*127/1600 = -162 (clamped to -128) → 0; then 255-0 = 255 (down)
    EXPECT(ev.analog[ANALOG_LY] == 255, "LY raw 0 after inversion → 255 (down)");
    // RX raw=0 → 0
    EXPECT(ev.analog[ANALOG_RX] == 0,   "RX min → 0");
    // RY raw=4095 → 255 then 255-255 = 0 (up)
    EXPECT(ev.analog[ANALOG_RY] == 0,   "RY raw 4095 after inversion → 0 (up)");
}

int main(void) {
    test_vid_pid();
    test_caps();
    test_reject_wrong_report_id();
    test_reject_short();
    test_idle_centered();
    test_battery();
    test_face_buttons();
    test_dpad();
    test_shoulders_and_system();
    test_stick_full_deflection();

    if (failures) {
        fprintf(stderr, "\n%d/%d Switch Pro assertions FAILED\n", failures, total);
        return EXIT_FAILURE;
    }
    printf("%d/%d Switch Pro assertions OK\n", total, total);
    return EXIT_SUCCESS;
}
