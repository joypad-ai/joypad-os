// psc_input.c — Sony PlayStation Classic parser tests.

#include <joypad/devices/sony/psc.h>
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

#define PSC_LEN 3

static void build_idle(uint8_t* buf) {
    buf[0] = 0x00;
    buf[1] = 0x3C;   // share=0, option=0, dpad=0x0F (released) in bits 2..5
    buf[2] = 0x00;   // counter
}

static void test_vid_pid(void) {
    EXPECT(joypad_is_sony_psc(0x054c, 0x0cda),  "Sony PSC VID/PID rejected");
    EXPECT(!joypad_is_sony_psc(0x054c, 0x0ce6), "DS5 should not match PSC");
}

static void test_caps(void) {
    joypad_caps_t c;
    joypad_sony_psc_caps(&c);
    EXPECT(c.layout == LAYOUT_MODERN_4FACE, "PSC layout should be MODERN_4FACE");
    EXPECT(c.button_count == 8,             "PSC has 8 face/shoulder buttons");
    EXPECT(c.axes_mask == 0,                "PSC has no analog axes");
    EXPECT(c.has_dpad,                      "PSC has a d-pad");
    EXPECT(!c.has_rumble,                   "PSC has no rumble");
    EXPECT(!c.has_motion,                   "PSC has no motion");
    EXPECT(!c.has_lightbar,                 "PSC has no lightbar");
}

static void test_reject_short(void) {
    uint8_t buf[2] = {0, 0};
    input_event_t ev;
    EXPECT(!joypad_parse_sony_psc(buf, sizeof(buf), &ev), "Short report should reject");
}

static void test_idle(void) {
    uint8_t buf[PSC_LEN];
    build_idle(buf);
    input_event_t ev;
    EXPECT(joypad_parse_sony_psc(buf, PSC_LEN, &ev), "Idle should parse");
    EXPECT(ev.buttons == 0,        "Idle: no buttons");
    EXPECT(ev.layout == LAYOUT_MODERN_4FACE, "Layout = MODERN_4FACE");
    EXPECT(ev.button_count == 8,   "button_count = 8");
    EXPECT(ev.analog[ANALOG_LX] == 128, "Idle: LX defaults to centered");
}

static void test_face_buttons(void) {
    uint8_t buf[PSC_LEN];
    build_idle(buf);
    buf[0] = 0x0F;  // square + cross + circle + triangle (low nibble) — actually all 4: 0x0F
    input_event_t ev;
    EXPECT(joypad_parse_sony_psc(buf, PSC_LEN, &ev), "Face buttons parse");
    EXPECT(ev.buttons & JP_BUTTON_B1, "Cross → B1");
    EXPECT(ev.buttons & JP_BUTTON_B2, "Circle → B2");
    EXPECT(ev.buttons & JP_BUTTON_B3, "Square → B3");
    EXPECT(ev.buttons & JP_BUTTON_B4, "Triangle → B4");
}

static void test_shoulders(void) {
    uint8_t buf[PSC_LEN];
    build_idle(buf);
    buf[0] = 0xF0;  // l1, r1, l2, r2
    input_event_t ev;
    joypad_parse_sony_psc(buf, PSC_LEN, &ev);
    EXPECT(ev.buttons & JP_BUTTON_L1, "L1");
    EXPECT(ev.buttons & JP_BUTTON_R1, "R1");
    EXPECT(ev.buttons & JP_BUTTON_L2, "L2");
    EXPECT(ev.buttons & JP_BUTTON_R2, "R2");
}

static void test_system_buttons(void) {
    uint8_t buf[PSC_LEN];
    build_idle(buf);
    buf[1] = 0x3C | 0x01 | 0x02;  // dpad released (0x0F<<2) + share + option
    input_event_t ev;
    joypad_parse_sony_psc(buf, PSC_LEN, &ev);
    EXPECT(ev.buttons & JP_BUTTON_S1, "Share → S1");
    EXPECT(ev.buttons & JP_BUTTON_S2, "Option → S2");
}

static void test_dpad(void) {
    uint8_t buf[PSC_LEN];
    input_event_t ev;
    struct { uint8_t code; uint32_t expect; const char* name; } cases[] = {
        {0x01, JP_BUTTON_DU, "N (0x01)"},
        {0x05, JP_BUTTON_DR, "E (0x05) bad code"}, // Will fall through to no-match
        {0x06, JP_BUTTON_DR, "E (0x06)"},
        {0x09, JP_BUTTON_DD, "S (0x09)"},
        {0x04, JP_BUTTON_DL, "W (0x04)"},
        {0x02, JP_BUTTON_DU|JP_BUTTON_DR, "NE (0x02)"},
        {0x00, JP_BUTTON_DU|JP_BUTTON_DL, "NW (0x00)"},
        {0x08, JP_BUTTON_DD|JP_BUTTON_DL, "SW (0x08)"},
        {0x0A, JP_BUTTON_DD|JP_BUTTON_DR, "SE (0x0A)"},
        {0x0F, 0,           "released"},
    };
    for (size_t i = 0; i < sizeof(cases)/sizeof(cases[0]); i++) {
        build_idle(buf);
        buf[1] = (uint8_t)((cases[i].code << 2) & 0x3C);
        joypad_parse_sony_psc(buf, PSC_LEN, &ev);
        uint32_t dpad_bits = ev.buttons & (JP_BUTTON_DU|JP_BUTTON_DD|JP_BUTTON_DL|JP_BUTTON_DR);
        if (cases[i].code == 0x05) {
            // 0x05 (center) — no dpad bits expected
            total++;
            if (dpad_bits != 0) { fprintf(stderr, "FAIL: %s should have no dpad bits, got 0x%x\n", cases[i].name, dpad_bits); failures++; }
            continue;
        }
        total++;
        if (dpad_bits != cases[i].expect) {
            fprintf(stderr, "FAIL: dpad %s expected 0x%x got 0x%x\n", cases[i].name, cases[i].expect, dpad_bits);
            failures++;
        }
    }
}

int main(void) {
    test_vid_pid();
    test_caps();
    test_reject_short();
    test_idle();
    test_face_buttons();
    test_shoulders();
    test_system_buttons();
    test_dpad();

    if (failures) {
        fprintf(stderr, "\n%d/%d PSC assertions FAILED\n", failures, total);
        return EXIT_FAILURE;
    }
    printf("%d/%d PSC assertions OK\n", total, total);
    return EXIT_SUCCESS;
}
