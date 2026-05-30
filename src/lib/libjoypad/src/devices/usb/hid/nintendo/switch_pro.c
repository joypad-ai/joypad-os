// devices/usb/hid/nintendo/switch_pro.c
// Nintendo Switch Pro / Joy-Con (USB) — pure input parser.

#include <joypad/devices/nintendo/switch_pro.h>
#include <joypad/buttons.h>
#include <joypad/layouts.h>

#include <string.h>

// Effective stick range from the calibrated center. Switch sticks reach only
// ~75-80% of the theoretical 0..4095 range before mechanical limits, so the
// effective half-range is ~1600 ticks rather than 2048.
#define STICK_RANGE 1600

// ----------------------------------------------------------------------------
// Wire-format input prefix (matches report IDs 0x30 and 0x21)
// ----------------------------------------------------------------------------

typedef struct __attribute__((packed)) {
    uint8_t report_id;
    uint8_t timer;
    uint8_t battery_and_conn;

    // Buttons byte 1: face + right shoulder side
    struct {
        uint8_t y    : 1;
        uint8_t x    : 1;
        uint8_t b    : 1;
        uint8_t a    : 1;
        uint8_t sr_r : 1;
        uint8_t sl_r : 1;
        uint8_t r    : 1;
        uint8_t zr   : 1;
    };
    // Buttons byte 2: system + stick clicks
    struct {
        uint8_t select  : 1;
        uint8_t start   : 1;
        uint8_t rstick  : 1;
        uint8_t lstick  : 1;
        uint8_t home    : 1;
        uint8_t cap     : 1;
        uint8_t padding : 2;
    };
    // Buttons byte 3: d-pad + left shoulder side
    struct {
        uint8_t down  : 1;
        uint8_t up    : 1;
        uint8_t right : 1;
        uint8_t left  : 1;
        uint8_t sr_l  : 1;
        uint8_t sl_l  : 1;
        uint8_t l     : 1;
        uint8_t zl    : 1;
    };

    uint8_t left_stick[3];   // 12-bit packed X|Y little-endian
    uint8_t right_stick[3];
    uint8_t vibration_ack;
} switch_pro_input_prefix_t;

// ----------------------------------------------------------------------------
// VID/PID
// ----------------------------------------------------------------------------

bool joypad_is_nintendo_switch_pro(uint16_t vid, uint16_t pid) {
    if (vid != 0x057e) return false;
    return (pid == 0x2009 ||  // Pro Controller
            pid == 0x200e ||  // Joy-Con Charging Grip
            pid == 0x2017);   // SNES Controller (NSO)
}

// ----------------------------------------------------------------------------
// Capabilities
// ----------------------------------------------------------------------------

void joypad_nintendo_switch_pro_caps(joypad_caps_t* out) {
    if (!out) return;
    joypad_caps_init(out);

    out->layout = LAYOUT_NINTENDO_4FACE;
    out->axes_mask = JOYPAD_AXIS_LX | JOYPAD_AXIS_LY |
                     JOYPAD_AXIS_RX | JOYPAD_AXIS_RY;
    out->button_count = 10;
    out->has_dpad = true;

    out->has_motion = true;   // Pro and Joy-Cons have IMU (raw IMU not yet
    out->has_gyro = true;     // surfaced by this parser — TODO)
    out->has_accel = true;
    out->gyro_range_dps = 2000;
    out->accel_range_milli_g = 8000;

    out->has_rumble = true;
    out->has_dual_rumble = true;     // HD Rumble has two motors (L/R grip)
    out->has_player_leds = true;
    out->num_player_leds = 4;

    out->reports_battery_level = true;
    out->reports_charging_state = true;
}

// ----------------------------------------------------------------------------
// Helpers
// ----------------------------------------------------------------------------

static uint8_t scale_stick_axis(uint16_t raw, uint16_t center) {
    int32_t centered = (int32_t)raw - (int32_t)center;
    int32_t scaled = (centered * 127) / STICK_RANGE;
    if (scaled < -128) scaled = -128;
    if (scaled >  127) scaled =  127;
    return (uint8_t)(scaled + 128);
}

// ----------------------------------------------------------------------------
// Parser
// ----------------------------------------------------------------------------

bool joypad_parse_nintendo_switch_pro(const uint8_t* report, uint16_t len, input_event_t* out) {
    if (!report || !out) return false;
    if (len < sizeof(switch_pro_input_prefix_t)) return false;
    const uint8_t id = report[0];
    if (id != JOYPAD_NINTENDO_SWITCH_PRO_INPUT_REPORT_ID &&
        id != JOYPAD_NINTENDO_SWITCH_PRO_REPLY_REPORT_ID) {
        return false;
    }

    switch_pro_input_prefix_t r;
    memcpy(&r, report, sizeof(r));

    init_input_event(out);
    out->type = INPUT_TYPE_GAMEPAD;
    out->transport = INPUT_TRANSPORT_USB;
    out->layout = LAYOUT_NINTENDO_4FACE;
    out->button_count = 10;

    // 12-bit raw stick values
    uint16_t lx = (uint16_t)(((uint16_t)r.left_stick[0]) | ((uint16_t)(r.left_stick[1] & 0x0F) << 8));
    uint16_t ly = (uint16_t)((((uint16_t)r.left_stick[1] & 0xF0) >> 4) | ((uint16_t)r.left_stick[2] << 4));
    uint16_t rx = (uint16_t)(((uint16_t)r.right_stick[0]) | ((uint16_t)(r.right_stick[1] & 0x0F) << 8));
    uint16_t ry = (uint16_t)((((uint16_t)r.right_stick[1] & 0xF0) >> 4) | ((uint16_t)r.right_stick[2] << 4));

    const uint16_t C = JOYPAD_NINTENDO_SWITCH_PRO_DEFAULT_STICK_CENTER;
    out->analog[ANALOG_LX] = scale_stick_axis(lx, C);
    out->analog[ANALOG_LY] = (uint8_t)(255 - scale_stick_axis(ly, C));   // invert Y to HID convention
    out->analog[ANALOG_RX] = scale_stick_axis(rx, C);
    out->analog[ANALOG_RY] = (uint8_t)(255 - scale_stick_axis(ry, C));

    out->buttons =
        (r.up     ? JP_BUTTON_DU : 0u) |
        (r.down   ? JP_BUTTON_DD : 0u) |
        (r.left   ? JP_BUTTON_DL : 0u) |
        (r.right  ? JP_BUTTON_DR : 0u) |
        (r.b      ? JP_BUTTON_B1 : 0u) |  // Nintendo B → south
        (r.a      ? JP_BUTTON_B2 : 0u) |  // Nintendo A → east
        (r.y      ? JP_BUTTON_B3 : 0u) |  // Nintendo Y → west
        (r.x      ? JP_BUTTON_B4 : 0u) |  // Nintendo X → north
        (r.l      ? JP_BUTTON_L1 : 0u) |
        (r.r      ? JP_BUTTON_R1 : 0u) |
        (r.zl     ? JP_BUTTON_L2 : 0u) |
        (r.zr     ? JP_BUTTON_R2 : 0u) |
        (r.select ? JP_BUTTON_S1 : 0u) |  // - button
        (r.start  ? JP_BUTTON_S2 : 0u) |  // + button
        (r.lstick ? JP_BUTTON_L3 : 0u) |
        (r.rstick ? JP_BUTTON_R3 : 0u) |
        (r.home   ? JP_BUTTON_A1 : 0u) |
        (r.cap    ? JP_BUTTON_A2 : 0u);

    // Battery: bits 7-4 = level (0/2/4/6/8 raw → scale to 0..100), bit 3 = charging
    uint8_t bat_raw = (uint8_t)(r.battery_and_conn >> 4);
    out->battery_level     = (bat_raw > 8) ? 100 : (uint8_t)(bat_raw * 12 + 5);
    out->battery_charging  = (r.battery_and_conn & 0x08) != 0;

    return true;
}
