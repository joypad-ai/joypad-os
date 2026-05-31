// devices/usb/hid/sony/ds4.c — Sony DualShock 4 pure parser + feedback.

#include <joypad/devices/sony/ds4.h>
#include <joypad/buttons.h>
#include <joypad/layouts.h>

#include <string.h>

// ----------------------------------------------------------------------------
// Wire-format input report (post-strip of report ID byte)
// ----------------------------------------------------------------------------

typedef struct __attribute__((packed)) {
    uint8_t x, y, z, rz;  // LX, LY, RX, RY

    struct {
        uint8_t dpad     : 4;  // 0..7 hat + 8=center
        uint8_t square   : 1;
        uint8_t cross    : 1;
        uint8_t circle   : 1;
        uint8_t triangle : 1;
    };
    struct {
        uint8_t l1     : 1;
        uint8_t r1     : 1;
        uint8_t l2     : 1;
        uint8_t r2     : 1;
        uint8_t share  : 1;
        uint8_t option : 1;
        uint8_t l3     : 1;
        uint8_t r3     : 1;
    };
    struct {
        uint8_t ps      : 1;
        uint8_t tpad    : 1;
        uint8_t counter : 6;
    };

    uint8_t l2_trigger;
    uint8_t r2_trigger;

    uint16_t timestamp;
    uint8_t  battery;   // raw, not used (we read battery from a later byte)
    int16_t  gyro[3];
    int16_t  accel[3];
    int8_t   unknown_a[5];
    uint8_t  headset;
    int8_t   unknown_b[2];

    struct {
        uint8_t tpad_event : 4;
        uint8_t unknown_c  : 4;
    };

    uint8_t tpad_counter;

    struct {
        uint8_t tpad_f1_count : 7;
        uint8_t tpad_f1_down  : 1;
    };
    int8_t tpad_f1_pos[3];

    struct {
        uint8_t tpad_f2_count : 7;
        uint8_t tpad_f2_down  : 1;
    };
    int8_t tpad_f2_pos[3];
} ds4_report_t;

// Per the firmware reference: battery status byte is at offset 29 of the
// post-strip buffer (offset 30 of the full report).
#define DS4_BATTERY_OFFSET_POST_STRIP 29

// ----------------------------------------------------------------------------
// Wire-format feedback report (post-strip of report ID)
// ----------------------------------------------------------------------------

typedef struct __attribute__((packed)) {
    uint8_t flags1;             // bit0=rumble, bit1=led, bit2=led_blink, bit3=ext_write,
                                // bit4..7 = volume bits
    uint8_t flags2;
    uint8_t reserved;

    uint8_t motor_right;        // high-frequency motor (light)
    uint8_t motor_left;         // low-frequency motor (heavy)

    uint8_t lightbar_r;
    uint8_t lightbar_g;
    uint8_t lightbar_b;
    uint8_t lightbar_blink_on;
    uint8_t lightbar_blink_off;

    uint8_t ext_data[8];

    uint8_t volume_left;
    uint8_t volume_right;
    uint8_t volume_mic;
    uint8_t volume_speaker;

    uint8_t other[9];
} ds4_feedback_t;

_Static_assert(sizeof(ds4_feedback_t) == JOYPAD_SONY_DS4_FEEDBACK_PAYLOAD_LEN,
               "DS4 feedback payload size mismatch");

// ----------------------------------------------------------------------------
// VID/PID
// ----------------------------------------------------------------------------

bool joypad_is_sony_ds4(uint16_t vid, uint16_t pid) {
    // Sony
    if (vid == 0x054c && (pid == 0x09cc || pid == 0x05c4 || pid == 0x0ba0)) return true;
    // Hori
    if (vid == 0x0f0d && (pid == 0x005e || pid == 0x0066 || pid == 0x008a || pid == 0x00ee)) return true;
    // Razer
    if (vid == 0x1532 && (pid == 0x0401 || pid == 0x1004 || pid == 0x1008)) return true;
    // Brook
    if (vid == 0x0c12 && (pid == 0x0c30 || pid == 0x0ef7 || pid == 0x1e1b)) return true;
    // Mad Catz
    if (vid == 0x0738 && (pid == 0x8180 || pid == 0x8384 || pid == 0x8481)) return true;
    // Qanba
    if (vid == 0x2c22 && (pid == 0x2000 || pid == 0x2200 || pid == 0x2300)) return true;
    // Misc third-party
    if (vid == 0x146b && pid == 0x0d09) return true;  // Nacon Daija
    if (vid == 0x20d6 && pid == 0x792a) return true;  // PowerA FUSION FightPad
    if (vid == 0x1f4f && pid == 0x1002) return true;  // ASW Guilty Gear xrd
    if (vid == 0x04d8 && pid == 0x1529) return true;  // UPCB
    if (vid == 0x0e6f && pid == 0x020a) return true;  // Victrix Pro FS for PS4
    return false;
}

// ----------------------------------------------------------------------------
// Capabilities
// ----------------------------------------------------------------------------

void joypad_sony_ds4_caps(joypad_caps_t* out) {
    if (!out) return;
    joypad_caps_init(out);

    out->layout = LAYOUT_MODERN_4FACE;
    out->axes_mask = JOYPAD_AXIS_LX | JOYPAD_AXIS_LY |
                     JOYPAD_AXIS_RX | JOYPAD_AXIS_RY |
                     JOYPAD_AXIS_L2 | JOYPAD_AXIS_R2;
    out->button_count = 10;
    out->has_dpad = true;

    out->has_motion = true;
    out->has_gyro = true;
    out->has_accel = true;
    out->gyro_range_dps = 2000;
    out->accel_range_milli_g = 4000;

    out->has_touchpad = true;
    out->num_touchpoints = 2;
    out->touch_max_x = 1919;
    out->touch_max_y = 942;
    out->touchpad_has_click = true;

    out->has_rumble = true;
    out->has_dual_rumble = true;
    out->has_lightbar = true;
    out->has_speaker = true;
    out->has_headset_jack = true;

    out->reports_battery_level = true;
    out->reports_charging_state = true;
}

// ----------------------------------------------------------------------------
// Input parser
// ----------------------------------------------------------------------------

bool joypad_parse_sony_ds4(const uint8_t* report, uint16_t len, input_event_t* out) {
    if (!report || !out) return false;
    if (len < 1 + sizeof(ds4_report_t)) return false;
    if (report[0] != JOYPAD_SONY_DS4_INPUT_REPORT_ID) return false;

    ds4_report_t r;
    memcpy(&r, report + 1, sizeof(r));

    init_input_event(out);
    out->type = INPUT_TYPE_GAMEPAD;
    out->transport = INPUT_TRANSPORT_USB;
    out->layout = LAYOUT_MODERN_4FACE;
    out->button_count = 10;

    bool dpad_up    = (r.dpad == 0 || r.dpad == 1 || r.dpad == 7);
    bool dpad_right = (r.dpad >= 1 && r.dpad <= 3);
    bool dpad_down  = (r.dpad >= 3 && r.dpad <= 5);
    bool dpad_left  = (r.dpad >= 5 && r.dpad <= 7);

    uint16_t tx  = (uint16_t)((((r.tpad_f1_pos[1] & 0x0f) << 8)) | ((uint8_t)r.tpad_f1_pos[0] & 0xff));
    uint16_t ty  = (uint16_t)((((r.tpad_f1_pos[1] & 0xf0) >> 4)) | (((uint8_t)r.tpad_f1_pos[2] & 0xff) << 4));
    uint16_t tx2 = (uint16_t)((((r.tpad_f2_pos[1] & 0x0f) << 8)) | ((uint8_t)r.tpad_f2_pos[0] & 0xff));
    uint16_t ty2 = (uint16_t)((((r.tpad_f2_pos[1] & 0xf0) >> 4)) | (((uint8_t)r.tpad_f2_pos[2] & 0xff) << 4));

    bool tpad_left  = r.tpad && !r.tpad_f1_down && tx <  960;
    bool tpad_right = r.tpad && !r.tpad_f1_down && tx >= 960;

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
        (r.option   ? JP_BUTTON_S2 : 0u) |
        (r.l3       ? JP_BUTTON_L3 : 0u) |
        (r.r3       ? JP_BUTTON_R3 : 0u) |
        (r.ps       ? JP_BUTTON_A1 : 0u) |
        (r.tpad     ? JP_BUTTON_A2 : 0u) |
        (tpad_left  ? JP_BUTTON_L4 : 0u) |
        (tpad_right ? JP_BUTTON_R4 : 0u);

    out->analog[ANALOG_LX] = r.x;
    out->analog[ANALOG_LY] = r.y;
    out->analog[ANALOG_RX] = r.z;
    out->analog[ANALOG_RY] = r.rz;
    out->analog[ANALOG_L2] = r.l2_trigger;
    out->analog[ANALOG_R2] = r.r2_trigger;

    out->has_motion = true;
    out->gyro[0]  = r.gyro[0];
    out->gyro[1]  = r.gyro[1];
    out->gyro[2]  = r.gyro[2];
    out->accel[0] = r.accel[0];
    out->accel[1] = r.accel[1];
    out->accel[2] = r.accel[2];
    out->gyro_range = 2000;
    out->accel_range = 4000;

    out->has_touch = true;
    out->touch[0].x = tx;
    out->touch[0].y = ty;
    out->touch[0].active = !r.tpad_f1_down;
    out->touch[1].x = tx2;
    out->touch[1].y = ty2;
    out->touch[1].active = !r.tpad_f2_down;

    // Battery byte at post-strip offset 29 (full-report offset 30).
    // Linux hid-playstation.c semantics: bits 0..3 = level, bit 4 = cable.
    if (len > 1 + DS4_BATTERY_OFFSET_POST_STRIP) {
        uint8_t raw = report[1 + DS4_BATTERY_OFFSET_POST_STRIP];
        uint8_t level = raw & 0x0F;
        bool cable    = (raw & 0x10) != 0;

        if (cable) {
            if (level < 10) {
                out->battery_level = (uint8_t)(level * 10 + 5);
                out->battery_charging = true;
            } else if (level == 10) {
                out->battery_level = 100;
                out->battery_charging = true;
            } else if (level == 11) {
                out->battery_level = 100;
                out->battery_charging = false;  // Full
            } else {
                out->battery_level = 0;          // 14=voltage/temp, 15=charge error
                out->battery_charging = false;
            }
        } else {
            out->battery_level = (level < 10) ? (uint8_t)(level * 10 + 5) : 100;
            out->battery_charging = false;
        }
    }

    return true;
}

// ----------------------------------------------------------------------------
// Feedback builder
// ----------------------------------------------------------------------------

uint16_t joypad_build_sony_ds4_feedback(const joypad_feedback_t* state,
                                        uint8_t* out_buf,
                                        uint16_t out_buf_size) {
    if (!state || !out_buf) return 0;
    if (out_buf_size < sizeof(ds4_feedback_t)) return 0;

    ds4_feedback_t fb;
    memset(&fb, 0, sizeof(fb));

    if (state->rumble_dirty) {
        fb.flags1 |= (1u << 0);  // rumble enable
        fb.motor_left  = state->rumble_low;
        fb.motor_right = state->rumble_high;
    }

    if (state->lightbar_dirty) {
        fb.flags1 |= (1u << 1);  // led enable
        fb.lightbar_r = state->lightbar.r;
        fb.lightbar_g = state->lightbar.g;
        fb.lightbar_b = state->lightbar.b;
    }
    // DS4 has no discrete player-LED hardware; player_index_dirty has no
    // direct mapping. Higher-level glue (firmware) colors the lightbar by
    // player slot instead, which is what consumers should do.

    memcpy(out_buf, &fb, sizeof(fb));
    return (uint16_t)sizeof(fb);
}
