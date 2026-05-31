// devices/usb/hid/sony/psc.c
// Sony PlayStation Classic — pure parser.

#include <joypad/devices/sony/psc.h>
#include <joypad/buttons.h>
#include <joypad/layouts.h>

#include <string.h>

// 3-byte report, no report_id prefix.
typedef struct __attribute__((packed)) {
    struct {
        uint8_t triangle : 1;
        uint8_t circle   : 1;
        uint8_t cross    : 1;
        uint8_t square   : 1;
        uint8_t l2       : 1;
        uint8_t r2       : 1;
        uint8_t l1       : 1;
        uint8_t r1       : 1;
    };
    struct {
        uint8_t share   : 1;
        uint8_t option  : 1;
        uint8_t dpad    : 4;  // 4-bit hat-style direction (0x0F = released)
        uint8_t padding : 2;
    };
    uint8_t counter;
} psc_report_t;

bool joypad_is_sony_psc(uint16_t vid, uint16_t pid) {
    return (vid == 0x054c && pid == 0x0cda);
}

void joypad_sony_psc_caps(joypad_caps_t* out) {
    if (!out) return;
    joypad_caps_init(out);

    out->layout = LAYOUT_MODERN_4FACE;
    out->axes_mask = 0;            // No sticks or analog triggers
    out->button_count = 8;
    out->has_dpad = true;
}

bool joypad_parse_sony_psc(const uint8_t* report, uint16_t len, input_event_t* out) {
    if (!report || !out) return false;
    if (len < sizeof(psc_report_t)) return false;

    psc_report_t r;
    memcpy(&r, report, sizeof(r));

    init_input_event(out);
    out->type = INPUT_TYPE_GAMEPAD;
    out->transport = INPUT_TRANSPORT_USB;
    out->layout = LAYOUT_MODERN_4FACE;
    out->button_count = 8;

    // PSC d-pad uses a 4-bit hat with discrete diagonals:
    //   0x00 = NW, 0x01 = N,  0x02 = NE
    //   0x04 = W,  0x05 = (center), 0x06 = E
    //   0x08 = SW, 0x09 = S,  0x0A = SE
    //   0x0F = released
    bool dpad_up    = (r.dpad <= 0x02);
    bool dpad_right = (r.dpad == 0x02 || r.dpad == 0x06 || r.dpad == 0x0A);
    bool dpad_down  = (r.dpad >= 0x08 && r.dpad <= 0x0A);
    bool dpad_left  = (r.dpad == 0x00 || r.dpad == 0x04 || r.dpad == 0x08);

    out->buttons =
        (dpad_up    ? JP_BUTTON_DU : 0u) |
        (dpad_down  ? JP_BUTTON_DD : 0u) |
        (dpad_left  ? JP_BUTTON_DL : 0u) |
        (dpad_right ? JP_BUTTON_DR : 0u) |
        (r.cross    ? JP_BUTTON_B1 : 0u) |
        (r.circle   ? JP_BUTTON_B2 : 0u) |
        (r.square   ? JP_BUTTON_B3 : 0u) |
        (r.triangle ? JP_BUTTON_B4 : 0u) |
        (r.l1       ? JP_BUTTON_L1 : 0u) |
        (r.r1       ? JP_BUTTON_R1 : 0u) |
        (r.l2       ? JP_BUTTON_L2 : 0u) |
        (r.r2       ? JP_BUTTON_R2 : 0u) |
        (r.share    ? JP_BUTTON_S1 : 0u) |
        (r.option   ? JP_BUTTON_S2 : 0u);

    return true;
}
