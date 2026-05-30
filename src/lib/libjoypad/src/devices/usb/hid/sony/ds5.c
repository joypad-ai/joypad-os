// devices/usb/hid/sony/ds5.c
// Sony DualSense (PS5) — pure parser + feedback builder.
//
// No platform deps. Only the C standard library and libjoypad's own headers
// are allowed. The firmware glue (router submission, player slot management,
// rumble dispatch lifecycle) stays in joypad-os; this file produces an
// input_event_t from raw bytes and produces feedback report bytes from a
// joypad_feedback_t.

#include <joypad/devices/sony/ds5.h>
#include <joypad/buttons.h>
#include <joypad/layouts.h>

#include <string.h>

// ============================================================================
// Wire-format report struct (DS5 USB input report, post-strip of report ID)
// ============================================================================
// Layout reverse-engineered from Linux's hid-playstation.c and corroborated
// by the firmware-side sony_ds5.h. Bitfields packed little-endian on every
// platform libjoypad targets (clang/gcc/msvc on x86/arm).

typedef struct __attribute__((packed)) {
    uint8_t x1, y1, x2, y2, rx, ry, rz;

    struct {
        uint8_t dpad     : 4;   // 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW, 8=center
        uint8_t square   : 1;   // west
        uint8_t cross    : 1;   // south
        uint8_t circle   : 1;   // east
        uint8_t triangle : 1;   // north
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
        uint8_t mute    : 1;
        uint8_t counter : 5;
    };

    int16_t gyro[3];        // x, y, z
    int16_t accel[3];       // x, y, z
    int8_t  unknown_a[5];
    uint8_t headset;
    int8_t  unknown_b[2];

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
} ds5_report_t;

// Battery byte sits at offset 52 in the post-strip buffer (i.e. byte 53 of
// the full report including the report-ID byte).
#define DS5_BATTERY_OFFSET_POST_STRIP 52

// ============================================================================
// Wire-format feedback struct (DS5 USB output report, post-strip of report ID)
// ============================================================================

typedef struct __attribute__((packed)) {
    uint16_t flags;             // bit0+1 = haptics enable, bit2 = trigger_r,
                                // bit3 = trigger_l, bit8 = mic_led,
                                // bit10 = lightbar, bit12 = player_led
    uint8_t  rumble_r;
    uint8_t  rumble_l;
    uint8_t  unk_4_7[4];
    uint8_t  mic_led;           // 0=off, 1=on, 2=pulse
    uint8_t  unk_9;
    struct __attribute__((packed)) {
        uint8_t motor_mode;     // 0x02=resistance, 0x06=vibration, 0x23=2-step
        uint8_t start_resistance;
        uint8_t effect_force;
        uint8_t range_force;
        uint8_t near_release_str;
        uint8_t near_middle_str;
        uint8_t pressed_str;
        uint8_t unk1[2];
        uint8_t actuation_freq;
        uint8_t unk2;
    } trigger_r;                // bytes 10-20
    struct __attribute__((packed)) {
        uint8_t motor_mode;
        uint8_t start_resistance;
        uint8_t effect_force;
        uint8_t range_force;
        uint8_t near_release_str;
        uint8_t near_middle_str;
        uint8_t pressed_str;
        uint8_t unk1[2];
        uint8_t actuation_freq;
        uint8_t unk2;
    } trigger_l;                // bytes 21-31
    uint8_t  unk_32_42[11];
    uint8_t  player_led;        // 5-bit LED mask, LSB = leftmost LED
    uint8_t  lightbar_r;
    uint8_t  lightbar_g;
    uint8_t  lightbar_b;
} ds5_feedback_t;

_Static_assert(sizeof(ds5_feedback_t) == JOYPAD_SONY_DS5_FEEDBACK_PAYLOAD_LEN,
               "DS5 feedback payload size mismatch");

// ============================================================================
// VID/PID
// ============================================================================

bool joypad_is_sony_ds5(uint16_t vid, uint16_t pid) {
    return ((vid == 0x054c && pid == 0x0ce6) ||  // Sony DualSense
            (vid == 0x0e6f && pid == 0x0209));   // Victrix Pro FS for PS5
}

// ============================================================================
// Capabilities
// ============================================================================

void joypad_sony_ds5_caps(joypad_caps_t* out) {
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
    out->touch_max_y = 1079;
    out->touchpad_has_click = true;

    out->has_rumble = true;
    out->has_dual_rumble = true;
    out->has_adaptive_triggers = true;
    out->has_lightbar = true;
    out->has_mic_led = true;
    out->has_speaker = true;
    out->has_headset_jack = true;

    out->reports_battery_level = true;
    out->reports_charging_state = true;
}

// ============================================================================
// Input parser
// ============================================================================

bool joypad_parse_sony_ds5(const uint8_t* report, uint16_t len, input_event_t* out) {
    if (!report || !out) return false;
    if (len < 1 + sizeof(ds5_report_t)) return false;
    if (report[0] != JOYPAD_SONY_DS5_INPUT_REPORT_ID) return false;

    ds5_report_t r;
    memcpy(&r, report + 1, sizeof(r));

    init_input_event(out);
    out->type = INPUT_TYPE_GAMEPAD;
    out->transport = INPUT_TRANSPORT_USB;
    out->layout = LAYOUT_MODERN_4FACE;
    out->button_count = 10;

    // D-pad: hat-encoded (0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW, 8=center)
    bool dpad_up    = (r.dpad == 0 || r.dpad == 1 || r.dpad == 7);
    bool dpad_right = (r.dpad >= 1 && r.dpad <= 3);
    bool dpad_down  = (r.dpad >= 3 && r.dpad <= 5);
    bool dpad_left  = (r.dpad >= 5 && r.dpad <= 7);

    // Touch positions (DS5 packs two 12-bit coords across three bytes per finger).
    uint16_t tx  = (uint16_t)((((r.tpad_f1_pos[1] & 0x0f) << 8)) | ((uint8_t)r.tpad_f1_pos[0] & 0xff));
    uint16_t ty  = (uint16_t)((((r.tpad_f1_pos[1] & 0xf0) >> 4)) | (((uint8_t)r.tpad_f1_pos[2] & 0xff) << 4));
    uint16_t tx2 = (uint16_t)((((r.tpad_f2_pos[1] & 0x0f) << 8)) | ((uint8_t)r.tpad_f2_pos[0] & 0xff));
    uint16_t ty2 = (uint16_t)((((r.tpad_f2_pos[1] & 0xf0) >> 4)) | (((uint8_t)r.tpad_f2_pos[2] & 0xff) << 4));

    // Touchpad click as left/right half (used by mouse-style remapping).
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
        (r.mute     ? JP_BUTTON_A3 : 0u) |
        (tpad_left  ? JP_BUTTON_L4 : 0u) |
        (tpad_right ? JP_BUTTON_R4 : 0u);

    // Analog axes — raw HID values, no nonzero clamping. The firmware wrapper
    // can apply its console-output workarounds if needed.
    out->analog[ANALOG_LX] = r.x1;
    out->analog[ANALOG_LY] = r.y1;
    out->analog[ANALOG_RX] = r.x2;
    out->analog[ANALOG_RY] = r.y2;
    out->analog[ANALOG_L2] = r.rx;
    out->analog[ANALOG_R2] = r.ry;

    // Motion
    out->has_motion = true;
    out->gyro[0] = r.gyro[0];
    out->gyro[1] = r.gyro[1];
    out->gyro[2] = r.gyro[2];
    out->accel[0] = r.accel[0];
    out->accel[1] = r.accel[1];
    out->accel[2] = r.accel[2];
    out->gyro_range = 2000;
    out->accel_range = 4000;

    // Touchpad
    out->has_touch = true;
    out->touch[0].x = tx;
    out->touch[0].y = ty;
    out->touch[0].active = !r.tpad_f1_down;
    out->touch[1].x = tx2;
    out->touch[1].y = ty2;
    out->touch[1].active = !r.tpad_f2_down;

    // Battery: byte 52 of the post-strip buffer (byte 53 of full report).
    // Linux hid-playstation.c semantics: nibble 0 = level (0-10),
    // nibble 1 = status (0=discharging, 1=charging, 2=full, others=error).
    if (len > 1 + DS5_BATTERY_OFFSET_POST_STRIP) {
        uint8_t raw = report[1 + DS5_BATTERY_OFFSET_POST_STRIP];
        uint8_t level  = raw & 0x0F;
        uint8_t status = (raw >> 4) & 0x0F;
        switch (status) {
            case 0x0:   // Discharging
                out->battery_level = (level > 10) ? 100 : (uint8_t)(level * 10 + 5);
                out->battery_charging = false;
                break;
            case 0x1:   // Charging
                out->battery_level = (level > 10) ? 100 : (uint8_t)(level * 10 + 5);
                out->battery_charging = true;
                break;
            case 0x2:   // Full
                out->battery_level = 100;
                out->battery_charging = false;
                break;
            default:    // Error / voltage / temp out-of-spec
                out->battery_level = 0;
                out->battery_charging = false;
                break;
        }
    }

    return true;
}

// ============================================================================
// Feedback builder
// ============================================================================

uint16_t joypad_build_sony_ds5_feedback(const joypad_feedback_t* state,
                                        uint8_t* out_buf,
                                        uint16_t out_buf_size) {
    if (!state || !out_buf) return 0;
    if (out_buf_size < sizeof(ds5_feedback_t)) return 0;

    ds5_feedback_t fb;
    memset(&fb, 0, sizeof(fb));

    // Always enable the lanes we might write to. This matches what the
    // existing firmware-side driver emits — the DS5 firmware ignores fields
    // whose enable bits are clear.
    uint16_t flags = 0;

    if (state->rumble_dirty || state->trigger_rumble_dirty) {
        // Enable rumble emulation (low nibble must be 0x7 per RE notes).
        flags |= (1u << 0) | (1u << 1) | (1u << 2);  // Lower nibble pattern
        fb.rumble_l = state->rumble_low;
        fb.rumble_r = state->rumble_high;
    }

    if (state->adaptive_right_dirty) {
        flags |= (1u << 2);  // trigger_r enable
        switch (state->adaptive_right.mode) {
            case JOYPAD_TRIGGER_MODE_OFF:
                fb.trigger_r.motor_mode = 0x00;
                break;
            case JOYPAD_TRIGGER_MODE_RESISTANCE:
                fb.trigger_r.motor_mode = 0x02;
                fb.trigger_r.start_resistance = state->adaptive_right.params[0];
                fb.trigger_r.effect_force     = state->adaptive_right.params[1];
                fb.trigger_r.range_force      = 0xff;
                break;
            case JOYPAD_TRIGGER_MODE_WEAPON:
                // Approximated; DS5 weapon mode has a richer parameter set.
                fb.trigger_r.motor_mode = 0x02;
                fb.trigger_r.start_resistance = state->adaptive_right.params[0];
                fb.trigger_r.effect_force     = state->adaptive_right.params[2];
                fb.trigger_r.range_force      = state->adaptive_right.params[1];
                break;
            case JOYPAD_TRIGGER_MODE_VIBRATION:
                fb.trigger_r.motor_mode    = 0x06;
                fb.trigger_r.start_resistance = state->adaptive_right.params[0];
                fb.trigger_r.effect_force     = state->adaptive_right.params[1];
                fb.trigger_r.actuation_freq   = state->adaptive_right.params[2];
                break;
            case JOYPAD_TRIGGER_MODE_VENDOR_RAW:
                memcpy(&fb.trigger_r, state->adaptive_right.params,
                       sizeof(state->adaptive_right.params) < sizeof(fb.trigger_r)
                       ? sizeof(state->adaptive_right.params)
                       : sizeof(fb.trigger_r));
                break;
            default:
                fb.trigger_r.motor_mode = 0x00;
                break;
        }
    }

    if (state->adaptive_left_dirty) {
        flags |= (1u << 3);  // trigger_l enable
        switch (state->adaptive_left.mode) {
            case JOYPAD_TRIGGER_MODE_OFF:
                fb.trigger_l.motor_mode = 0x00;
                break;
            case JOYPAD_TRIGGER_MODE_RESISTANCE:
                fb.trigger_l.motor_mode = 0x02;
                fb.trigger_l.start_resistance = state->adaptive_left.params[0];
                fb.trigger_l.effect_force     = state->adaptive_left.params[1];
                fb.trigger_l.range_force      = 0xff;
                break;
            case JOYPAD_TRIGGER_MODE_WEAPON:
                fb.trigger_l.motor_mode = 0x02;
                fb.trigger_l.start_resistance = state->adaptive_left.params[0];
                fb.trigger_l.effect_force     = state->adaptive_left.params[2];
                fb.trigger_l.range_force      = state->adaptive_left.params[1];
                break;
            case JOYPAD_TRIGGER_MODE_VIBRATION:
                fb.trigger_l.motor_mode    = 0x06;
                fb.trigger_l.start_resistance = state->adaptive_left.params[0];
                fb.trigger_l.effect_force     = state->adaptive_left.params[1];
                fb.trigger_l.actuation_freq   = state->adaptive_left.params[2];
                break;
            case JOYPAD_TRIGGER_MODE_VENDOR_RAW:
                memcpy(&fb.trigger_l, state->adaptive_left.params,
                       sizeof(state->adaptive_left.params) < sizeof(fb.trigger_l)
                       ? sizeof(state->adaptive_left.params)
                       : sizeof(fb.trigger_l));
                break;
            default:
                fb.trigger_l.motor_mode = 0x00;
                break;
        }
    }

    if (state->mic_led_dirty) {
        flags |= (1u << 8);
        fb.mic_led = state->mic_led_mode;
    }

    if (state->lightbar_dirty) {
        flags |= (1u << 10);
        fb.lightbar_r = state->lightbar.r;
        fb.lightbar_g = state->lightbar.g;
        fb.lightbar_b = state->lightbar.b;
    }

    if (state->player_index_dirty) {
        flags |= (1u << 12);
        // DS5 player LEDs: 5 LEDs, bitmask with LSB = leftmost.
        // Use the classic 1-of-5 pattern for player 1..5; 6+ rolls over.
        switch (state->player_index) {
            case 0: fb.player_led = 0x00; break;        // None
            case 1: fb.player_led = 0x04; break;        // Center LED
            case 2: fb.player_led = 0x0A; break;        // Inner two LEDs
            case 3: fb.player_led = 0x15; break;        // 1, 3, 5
            case 4: fb.player_led = 0x1B; break;        // 1, 2, 4, 5
            default: fb.player_led = 0x1F; break;       // All five
        }
    }

    fb.flags = flags;
    memcpy(out_buf, &fb, sizeof(fb));
    return (uint16_t)sizeof(fb);
}
