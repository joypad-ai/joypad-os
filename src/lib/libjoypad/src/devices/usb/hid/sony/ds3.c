// devices/usb/hid/sony/ds3.c — Sony DualShock 3 / SIXAXIS pure parser.

#include <joypad/devices/sony/ds3.h>
#include <joypad/buttons.h>
#include <joypad/layouts.h>

#include <string.h>

// ----------------------------------------------------------------------------
// Wire-format input report (post-strip of report ID byte)
// ----------------------------------------------------------------------------

typedef struct __attribute__((packed)) {
    struct {
        uint8_t select : 1;
        uint8_t l3     : 1;
        uint8_t r3     : 1;
        uint8_t start  : 1;
        uint8_t up     : 1;
        uint8_t right  : 1;
        uint8_t down   : 1;
        uint8_t left   : 1;
    };
    struct {
        uint8_t l2       : 1;
        uint8_t r2       : 1;
        uint8_t l1       : 1;
        uint8_t r1       : 1;
        uint8_t triangle : 1;
        uint8_t circle   : 1;
        uint8_t cross    : 1;
        uint8_t square   : 1;
    };
    uint8_t ps;
    uint8_t not_used;
    uint8_t lx, ly, rx, ry;
    uint8_t pressure[12];   // DPad (4) + L2/R2/L1/R1 (4) + face buttons (4)
    uint8_t unused[36];
    uint8_t charge;
    uint8_t connection;
    uint8_t power_rating;
    uint8_t communication_rating;
    uint8_t pad[5];
    uint8_t counter;
} ds3_report_t;

bool joypad_is_sony_ds3(uint16_t vid, uint16_t pid) {
    return (vid == 0x054c && pid == 0x0268);
}

void joypad_sony_ds3_caps(joypad_caps_t* out) {
    if (!out) return;
    joypad_caps_init(out);

    out->layout = LAYOUT_MODERN_4FACE;
    out->axes_mask = JOYPAD_AXIS_LX | JOYPAD_AXIS_LY |
                     JOYPAD_AXIS_RX | JOYPAD_AXIS_RY |
                     JOYPAD_AXIS_L2 | JOYPAD_AXIS_R2;
    out->button_count = 10;
    out->has_dpad = true;

    out->has_motion = true;
    out->has_gyro = true;          // Z-axis only
    out->has_accel = true;
    out->gyro_range_dps = 100;     // DS3 gyro is ±100 dps
    out->accel_range_milli_g = 2000;

    out->has_pressure = true;      // All face/dpad/shoulder buttons are pressure-sensitive

    out->has_rumble = true;
    out->has_dual_rumble = true;   // L motor (force 0..255) + R motor (on/off)
    out->has_player_leds = true;
    out->num_player_leds = 4;

    out->reports_battery_level = true;
    out->reports_charging_state = true;
}

bool joypad_parse_sony_ds3(const uint8_t* report, uint16_t len, input_event_t* out) {
    if (!report || !out) return false;
    if (len < 1 + sizeof(ds3_report_t)) return false;
    if (report[0] != JOYPAD_SONY_DS3_INPUT_REPORT_ID) return false;

    ds3_report_t r;
    memcpy(&r, report + 1, sizeof(r));

    init_input_event(out);
    out->type = INPUT_TYPE_GAMEPAD;
    out->transport = INPUT_TRANSPORT_USB;
    out->layout = LAYOUT_MODERN_4FACE;
    out->button_count = 10;

    out->buttons =
        (r.up       ? JP_BUTTON_DU : 0u) |
        (r.down     ? JP_BUTTON_DD : 0u) |
        (r.left     ? JP_BUTTON_DL : 0u) |
        (r.right    ? JP_BUTTON_DR : 0u) |
        (r.cross    ? JP_BUTTON_B1 : 0u) |
        (r.circle   ? JP_BUTTON_B2 : 0u) |
        (r.square   ? JP_BUTTON_B3 : 0u) |
        (r.triangle ? JP_BUTTON_B4 : 0u) |
        (r.l1       ? JP_BUTTON_L1 : 0u) |
        (r.r1       ? JP_BUTTON_R1 : 0u) |
        (r.l2       ? JP_BUTTON_L2 : 0u) |
        (r.r2       ? JP_BUTTON_R2 : 0u) |
        (r.select   ? JP_BUTTON_S1 : 0u) |
        (r.start    ? JP_BUTTON_S2 : 0u) |
        (r.l3       ? JP_BUTTON_L3 : 0u) |
        (r.r3       ? JP_BUTTON_R3 : 0u) |
        (r.ps       ? JP_BUTTON_A1 : 0u);

    out->analog[ANALOG_LX] = r.lx;
    out->analog[ANALOG_LY] = r.ly;
    out->analog[ANALOG_RX] = r.rx;
    out->analog[ANALOG_RY] = r.ry;
    out->analog[ANALOG_L2] = r.pressure[8];   // DS3 trigger "axis" comes from pressure
    out->analog[ANALOG_R2] = r.pressure[9];

    // Pressure-sensitive button data, in the canonical input_event_t order:
    //   {up, right, down, left, L2, R2, L1, R1, triangle, circle, cross, square}
    out->has_pressure = true;
    out->pressure[0]  = r.pressure[4];   // up
    out->pressure[1]  = r.pressure[5];   // right
    out->pressure[2]  = r.pressure[6];   // down
    out->pressure[3]  = r.pressure[7];   // left
    out->pressure[4]  = r.pressure[8];   // L2
    out->pressure[5]  = r.pressure[9];   // R2
    out->pressure[6]  = r.pressure[10];  // L1
    out->pressure[7]  = r.pressure[11];  // R1
    out->pressure[8]  = r.unused[0];     // triangle
    out->pressure[9]  = r.unused[1];     // circle
    out->pressure[10] = r.unused[2];     // cross
    out->pressure[11] = r.unused[3];     // square

    // Motion at offset 40..47 of the post-strip buffer (= 41..48 of full report).
    // DS3 uses big-endian 16-bit values centered at ~512. Gyro is Z-axis only.
    // Normalize to canonical SInput-style ±32767 = full-scale.
    if (len > 1 + 47) {
        const uint8_t* m = report + 1 + 40;
        int16_t raw_accel_x = (int16_t)((m[0] << 8) | m[1]);
        int16_t raw_accel_y = (int16_t)((m[2] << 8) | m[3]);
        int16_t raw_accel_z = (int16_t)((m[4] << 8) | m[5]);
        int16_t raw_gyro_z  = (int16_t)((m[6] << 8) | m[7]);

        // DS3 accel: ±512 = ±2g  →  normalize so full-scale = ±32767 → /1024
        out->accel[0] = (int16_t)(((int32_t)(raw_accel_x - 512) * 32767) / 1024);
        out->accel[1] = (int16_t)(((int32_t)(raw_accel_y - 512) * 32767) / 1024);
        out->accel[2] = (int16_t)(((int32_t)(raw_accel_z - 512) * 32767) / 1024);

        // DS3 gyro: ±512 = ±100 dps  →  normalize so SInput full-scale = ±2000 dps  → /10240
        out->gyro[0] = 0;   // DS3 has no X-axis gyro
        out->gyro[1] = 0;   // DS3 has no Y-axis gyro
        out->gyro[2] = (int16_t)(((int32_t)(raw_gyro_z - 512) * 32767) / 10240);

        out->has_motion = true;
        out->gyro_range  = 100;
        out->accel_range = 2000;
    }

    // Battery byte: post-strip offset 29 = full-report offset 30.
    // 0..5 = lookup table; 0xEE = charging; 0xEF = full.
    if (len > 1 + 29) {
        static const uint8_t ds3_battery[] = { 0, 1, 25, 50, 75, 100 };
        uint8_t charge = report[1 + 29];
        if (charge >= 0xEE) {
            out->battery_level = 100;
            out->battery_charging = ((charge & 0x01) == 0);  // 0xEE charging, 0xEF full
        } else if (charge <= 5) {
            out->battery_level = ds3_battery[charge];
            out->battery_charging = false;
        }
    }

    return true;
}
