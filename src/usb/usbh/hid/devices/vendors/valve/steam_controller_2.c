// steam_controller_2.c - Valve Steam Controller 2 (codename "Triton" / "Roy")
//
// One driver covers two USB transports:
//   VID 0x28DE PID 0x1302  Direct wired SC2
//   VID 0x28DE PID 0x1304  SC2 paired through Valve's USB "puck" dongle
//
// The SC2 ships in "lizard mode" — Valve's keyboard + mouse emulation — and its
// vendor interface stays silent until told otherwise (what Steam does on a PC).
// So on mount we disable lizard mode via SET_SETTINGS (0x87, SETTING_LIZARD_MODE
// = 0) and RE-SEND it on a heartbeat (task) because the firmware re-enables it on
// its own. Once disabled the vendor interface streams the native state report
// ID 0x42 (ID_TRITON_CONTROLLER_STATE, TritonMTUFull_t, 54 bytes) at ~60 Hz,
// which process() parses. Verified working on wired SC2 hardware (buttons, both
// sticks, analog triggers).
//
// Report layout + lizard-disable protocol: CouchTurtle/sc2-research and SDL3.
// (An earlier firmware rev used report ID 0x45 with a different layout; this
// driver targets the shipping 0x42 firmware.) IMU (accel/gyro, bytes 0x22-0x2d)
// ships OFF by default and reads flat until a separate enable-IMU setting is
// sent — a follow-up. Grip (L5/R5) and trackpad touch/click bits are unmapped.
//
// SPDX-License-Identifier: Apache-2.0

#include "steam_controller_2.h"
#include "core/buttons.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "usb/usbh/hid/hid_registry.h"  // device_interfaces[], CONTROLLER_KEYBOARD/MOUSE
#include "platform/platform.h"          // platform_time_ms (lizard heartbeat)
#include "tusb.h"
#include <stdio.h>
#include <string.h>

// Lizard-mode disable must be re-sent periodically — the SC2 firmware re-enables
// keyboard/mouse emulation on its own (SDL3 refreshes ~every 3s). Without this the
// vendor interface stays silent and no gamepad input arrives.
#define SC2_LIZARD_REFRESH_MS 1000

#ifndef SC2_DEBUG
#define SC2_DEBUG 1   // unknown-bit logging on during development
#endif

// Minimum bytes we need before the parse is meaningful (through gyro Z at 0x2d).
// The 0x42 state report is 54 bytes; require enough to read the IMU fields.
#define SC2_MIN_REPORT_LEN 46

// --- Bit-packed report helpers --------------------------------------------
// Every 16-bit axis in the quirk layout is byte-aligned, so the i16 helper
// is a plain LE byte load. Buttons live at non-aligned bit offsets and use
// the generic bit-at-offset accessor.

static inline bool sc2_bit(const uint8_t *r, int bitpos) {
    return (r[bitpos >> 3] >> (bitpos & 7)) & 1;
}

static inline int16_t sc2_i16(const uint8_t *r, int bitpos) {
    int b = bitpos >> 3;
    return (int16_t)((uint16_t)r[b] | ((uint16_t)r[b + 1] << 8));
}

// Scale signed 16-bit stick value (-32767..32767) to unsigned 8-bit (0..255,
// 128 = center). Matches the Switch Pro / DS4 convention used elsewhere.
static inline uint8_t sc2_stick_to_u8(int16_t v) {
    int32_t scaled = ((int32_t)v + 32768) >> 8;  // 0..255
    if (scaled < 0) scaled = 0;
    if (scaled > 255) scaled = 255;
    if (scaled == 0) scaled = 1;  // 0 reserved internally for "no data"
    return (uint8_t)scaled;
}

// SC2 pads are native int16 (±32767). Normalize into the device-agnostic 0..65535
// touch scale (see input_event.h): X straight, Y inverted (SC2 +Y = up, canonical 0 = top).
static inline uint16_t sc2_pad_x(int16_t v) { return touch_norm_from_s16(v); }
static inline uint16_t sc2_pad_y(int16_t v) { return (uint16_t)(32767 - (int32_t)v); }

// --- Driver state ----------------------------------------------------------

static uint8_t prev_buttons_lo[CFG_TUH_DEVICE_MAX + 1][CFG_TUH_HID];
static uint32_t last_lizard_ms[CFG_TUH_DEVICE_MAX + 1][CFG_TUH_HID];
static uint8_t  last_rumble[CFG_TUH_DEVICE_MAX + 1][CFG_TUH_HID];
static uint32_t last_rumble_ms[CFG_TUH_DEVICE_MAX + 1][CFG_TUH_HID];
static uint8_t  batt_level[CFG_TUH_DEVICE_MAX + 1][CFG_TUH_HID];    // 0-100, 0=unknown
static bool     batt_charging[CFG_TUH_DEVICE_MAX + 1][CFG_TUH_HID];
#if SC2_DEBUG
static uint8_t  logged_report_id[CFG_TUH_DEVICE_MAX + 1][CFG_TUH_HID];
#endif

// The SC2 auto-stops rumble after a ~50ms hardware safety timeout, so an active
// rumble must be re-sent on an interval (SDL uses 40ms).
#define SC2_RUMBLE_RESEND_MS 40

// --- DeviceInterface callbacks --------------------------------------------

static bool sc2_is_device(uint16_t vid, uint16_t pid) {
    return vid == SC2_VID && (pid == SC2_PID_WIRED || pid == SC2_PID_PUCK);
}

// Disable "lizard mode" (Valve's default keyboard + mouse emulation) so the SC2
// streams its native gamepad state report — what Steam does on a real PC — AND
// enable the IMU (accel+gyro) in the same write. The SC2 (Triton) uses SET_SETTINGS
// (0x87), which packs multiple [setting, val_lo, val_hi] triples: SETTING_LIZARD_MODE
// (9) = 0 to leave lizard mode, and SETTING_GYRO (0x30) = 0x18 (SEND_RAW_ACCEL 0x08 |
// SEND_RAW_GYRO 0x10) to stream motion (off by default → IMU fields read flat until
// set; mirrors valve_setting_imu_on on the BLE path). Format: [0x87, payload_len,
// <triples...>] as a 64-byte feature report. Must be re-sent periodically (task)
// because the firmware re-enables lizard mode on its own; re-sending the IMU bit is
// harmless. Static buffer outlives the async control transfer. (Ref: SDL3 /
// CouchTurtle/sc2-research.)
static void sc2_disable_lizard(uint8_t dev_addr, uint8_t instance) {
    static uint8_t settings[64];
    memset(settings, 0, sizeof(settings));
    settings[0] = 0x87;  // ID_SET_SETTINGS_VALUES
    settings[1] = 0x06;  // payload length: 2 settings x 3 bytes
    settings[2] = 0x09;  // SETTING_LIZARD_MODE
    settings[3] = 0x00;  // value LO (0 = disabled)
    settings[4] = 0x00;  // value HI
    settings[5] = 0x30;  // SETTING_GYRO
    settings[6] = 0x18;  // SEND_RAW_ACCEL (0x08) | SEND_RAW_GYRO (0x10)
    settings[7] = 0x00;  // value HI
    tuh_hid_set_report(dev_addr, instance, 0, HID_REPORT_TYPE_FEATURE,
                       settings, sizeof(settings));
}

static bool sc2_init(uint8_t dev_addr, uint8_t instance) {
    printf("[SC2] mounted dev_addr=%u instance=%u\n", dev_addr, instance);
    prev_buttons_lo[dev_addr][instance] = 0;
    // 0 forces the first task() tick to send the lizard-disable immediately —
    // sending during mount can race the interface coming up, so defer to task().
    last_lizard_ms[dev_addr][instance] = 0;
#if SC2_DEBUG
    logged_report_id[dev_addr][instance] = 0;
#endif
    return true;
}

static void sc2_unmount(uint8_t dev_addr, uint8_t instance) {
    printf("[SC2] unmounted dev_addr=%u instance=%u\n", dev_addr, instance);
    prev_buttons_lo[dev_addr][instance] = 0;
#if SC2_DEBUG
    logged_report_id[dev_addr][instance] = 0;
#endif
}

// Rumble via the HAPTIC_RUMBLE output report (0x80). The magnitude rides entirely
// on the per-motor "speed" fields (SDL passes the 0..65535 rumble value straight
// through); gain stays 0 dB. It is an OUTPUT report — sent on the vendor interface's
// interrupt-OUT endpoint (a FEATURE SET_REPORT is accepted but silently ignored).
// Ref: SDL src/joystick/hidapi/SDL_hidapi_steam_triton.c.
static void sc2_send_rumble(uint8_t dev_addr, uint8_t instance, uint8_t intensity) {
    uint16_t speed = (uint16_t)(intensity * 257);          // 0..255 -> 0..65535
    // payload after the report id (tuh_hid_send_report prepends 0x80):
    // type, intensity_u16, left.speed_u16, left.gain, right.speed_u16, right.gain
    uint8_t p[9];
    p[0] = 0x00;                                            // type
    p[1] = 0x00; p[2] = 0x00;                              // intensity (unused)
    p[3] = (uint8_t)(speed & 0xFF); p[4] = (uint8_t)(speed >> 8);   // left.speed
    p[5] = 0x00;                                            // left.gain (0 dB)
    p[6] = (uint8_t)(speed & 0xFF); p[7] = (uint8_t)(speed >> 8);   // right.speed
    p[8] = 0x00;                                            // right.gain (0 dB)
    // The SC2 vendor interface has an interrupt-OUT endpoint, so the output report
    // goes there. Fall back to a control OUTPUT SET_REPORT if it is ever absent.
    if (!tuh_hid_send_report(dev_addr, instance, 0x80, p, sizeof(p))) {
        uint8_t q[10]; q[0] = 0x80; memcpy(&q[1], p, sizeof(p));
        tuh_hid_set_report(dev_addr, instance, 0, HID_REPORT_TYPE_OUTPUT, q, sizeof(q));
    }
}

static void sc2_task(uint8_t dev_addr, uint8_t instance,
                     device_output_config_t *config) {
    // Only the vendor interface (protocol NONE) accepts settings/haptic reports.
    if (tuh_hid_interface_protocol(dev_addr, instance) != HID_ITF_PROTOCOL_NONE) {
        return;
    }
    uint32_t now = platform_time_ms();

    // Lizard-disable heartbeat — the firmware re-enables lizard mode on its own,
    // so re-send on an interval to keep the native gamepad stream alive.
    if (now - last_lizard_ms[dev_addr][instance] >= SC2_LIZARD_REFRESH_MS) {
        sc2_disable_lizard(dev_addr, instance);
        last_lizard_ms[dev_addr][instance] = now;
    }

    // Rumble: send on change, then keep re-sending every ~40ms while active so the
    // hardware safety timeout doesn't cut it off. When it drops to 0 we simply stop
    // sending and the controller auto-stops.
    uint8_t rumble = config ? config->rumble : 0;
    bool changed = (rumble != last_rumble[dev_addr][instance]);
    if (rumble > 0 && (changed || now - last_rumble_ms[dev_addr][instance] >= SC2_RUMBLE_RESEND_MS)) {
        sc2_send_rumble(dev_addr, instance, rumble);
        last_rumble_ms[dev_addr][instance] = now;
    }
    last_rumble[dev_addr][instance] = rumble;
}

static void sc2_process(uint8_t dev_addr, uint8_t instance,
                        const uint8_t *report, uint16_t len) {
#if SC2_DEBUG
    // Surface the actual report ID once per distinct ID so hardware bring-up can
    // confirm which native report the firmware streams (0x42 vs 0x45) after the
    // lizard-disable heartbeat kicks in.
    if (len >= 1 && report[0] != logged_report_id[dev_addr][instance]) {
        printf("[SC2] report id=0x%02X len=%u\n", report[0], (unsigned)len);
        logged_report_id[dev_addr][instance] = report[0];
    }
#endif

    // Battery status (0x43, TritonBatteryStatus_t): byte 1 = charge state
    // (0 reset, 1 discharging, 2 charging, 3 src-validate, 4 charge-done),
    // byte 2 = level 0-100. Cache it and attach to the next 0x42 input event.
    if (report[0] == SC2_BATTERY_REPORT_ID && len >= 3) {
        uint8_t state = report[1];
        batt_level[dev_addr][instance] = report[2];
        batt_charging[dev_addr][instance] = (state == 2 || state == 4);  // charging / done
        return;
    }

    // The SC2 vendor interface emits report ID 0x45. Anything else is a lizard
    // report from the kbd/mouse interfaces (which we claimed by VID/PID). Forward
    // those to the generic handlers so lizard mode still drives gamepad output —
    // rather than dropping them and going dead. Native 0x45 reports fall through.
    if (len < 1 || report[0] != SC2_INPUT_REPORT_ID) {
        uint8_t itf = tuh_hid_interface_protocol(dev_addr, instance);
        if (itf == HID_ITF_PROTOCOL_KEYBOARD && device_interfaces[CONTROLLER_KEYBOARD]) {
            device_interfaces[CONTROLLER_KEYBOARD]->process(dev_addr, instance, report, len);
        } else if (itf == HID_ITF_PROTOCOL_MOUSE && device_interfaces[CONTROLLER_MOUSE]) {
            device_interfaces[CONTROLLER_MOUSE]->process(dev_addr, instance, report, len);
        }
        return;
    }
    if (len < SC2_MIN_REPORT_LEN) {
        return;  // native report but truncated
    }

    // --- Buttons (0x42 TritonMTUFull_t; byte 0x02-0x05, bit = byte*8+n) ---
    uint32_t buttons = 0;
    if (sc2_bit(report, 16)) buttons |= JP_BUTTON_B1;  // A   (0x02 b0)
    if (sc2_bit(report, 17)) buttons |= JP_BUTTON_B2;  // B   (0x02 b1)
    if (sc2_bit(report, 18)) buttons |= JP_BUTTON_B3;  // X   (0x02 b2)
    if (sc2_bit(report, 19)) buttons |= JP_BUTTON_B4;  // Y   (0x02 b3)
    if (sc2_bit(report, 20)) buttons |= JP_BUTTON_A2;  // QAM (quick-access menu)
    if (sc2_bit(report, 21)) buttons |= JP_BUTTON_R3;  // R3  (right stick click)
    if (sc2_bit(report, 22)) buttons |= JP_BUTTON_S2;  // View (⧉) -> Start (matches BLE/SDL)
    if (sc2_bit(report, 23)) buttons |= JP_BUTTON_R4;  // R4 upper back paddle
    if (sc2_bit(report, 24)) buttons |= JP_BUTTON_R5;  // R5 lower back paddle
    if (sc2_bit(report, 25)) buttons |= JP_BUTTON_R1;  // RB
    if (sc2_bit(report, 26)) buttons |= JP_BUTTON_DD;  // D-Down
    if (sc2_bit(report, 27)) buttons |= JP_BUTTON_DR;  // D-Right
    if (sc2_bit(report, 28)) buttons |= JP_BUTTON_DL;  // D-Left
    if (sc2_bit(report, 29)) buttons |= JP_BUTTON_DU;  // D-Up
    if (sc2_bit(report, 30)) buttons |= JP_BUTTON_S1;  // Menu (☰) -> Back/Select (matches BLE/SDL)
    if (sc2_bit(report, 31)) buttons |= JP_BUTTON_L3;  // L3 (left stick click)
    if (sc2_bit(report, 32)) buttons |= JP_BUTTON_A1;  // Steam / Home
    if (sc2_bit(report, 33)) buttons |= JP_BUTTON_L4;  // L4 upper back paddle
    if (sc2_bit(report, 34)) buttons |= JP_BUTTON_L5;  // L5 lower back paddle
    if (sc2_bit(report, 35)) buttons |= JP_BUTTON_L1;  // LB
    if (sc2_bit(report, 39)) buttons |= JP_BUTTON_R2;  // RT full-pull click
    if (sc2_bit(report, 43)) buttons |= JP_BUTTON_L2;  // LT full-pull click
    // Grip (L5/R5) and trackpad click bits intentionally left unmapped.

    // --- Trackpads (int16 LE ±32767) → DualSense touchpad space (0..1919 × 0..1079,
    // top-left origin, +Y down) so PS5-type visualizers render at the right scale.
    // Left pad pos 0x12/0x14, touch = byte 0x05 bit1 (bitpos 41). Right pad pos
    // 0x18/0x1a, touch = byte 0x04 bit5 (bitpos 37).
    bool lpad_touch = sc2_bit(report, 41);
    bool rpad_touch = sc2_bit(report, 37);
    uint16_t lpad_x = sc2_pad_x(sc2_i16(report, 144));  // 0x12
    uint16_t lpad_y = sc2_pad_y(sc2_i16(report, 160));  // 0x14
    uint16_t rpad_x = sc2_pad_x(sc2_i16(report, 192));  // 0x18
    uint16_t rpad_y = sc2_pad_y(sc2_i16(report, 208));  // 0x1a
    bool has_touch = (lpad_touch || rpad_touch);

    // --- Sticks (int16 LE ±32767; Valve +Y = up → invert to HID 0=up) ----
    uint8_t lx = sc2_stick_to_u8(sc2_i16(report,  80));                    // 0x0a
    uint8_t ly = (uint8_t)(255 - sc2_stick_to_u8(sc2_i16(report,  96)));   // 0x0c
    uint8_t rx = sc2_stick_to_u8(sc2_i16(report, 112));                    // 0x0e
    uint8_t ry = (uint8_t)(255 - sc2_stick_to_u8(sc2_i16(report, 128)));   // 0x10
    if (ly == 0) ly = 1;
    if (ry == 0) ry = 1;

    // --- Triggers (Hall-effect int16 0..32767 → 0..255) ------------------
    uint16_t l2_raw = (uint16_t)sc2_i16(report, 48);  // 0x06
    uint16_t r2_raw = (uint16_t)sc2_i16(report, 64);  // 0x08
    uint8_t l2 = (uint8_t)(l2_raw >> 7);
    uint8_t r2 = (uint8_t)(r2_raw >> 7);

    // --- IMU (accel 0x22-0x27, gyro 0x28-0x2d; frozen until IMU enabled) --
    int16_t accel_x = sc2_i16(report, 272);  // 0x22
    int16_t accel_y = sc2_i16(report, 288);  // 0x24
    int16_t accel_z = sc2_i16(report, 304);  // 0x26
    int16_t gyro_x  = sc2_i16(report, 320);  // 0x28
    int16_t gyro_y  = sc2_i16(report, 336);  // 0x2a
    int16_t gyro_z  = sc2_i16(report, 352);  // 0x2c

    input_event_t event = {
        .dev_addr = dev_addr,
        .instance = instance,
        .type = INPUT_TYPE_GAMEPAD,
        .transport = INPUT_TRANSPORT_USB,
        .layout = LAYOUT_MODERN_4FACE,
        .buttons = buttons,
        .button_count = 20,
        .analog = {lx, ly, rx, ry, l2, r2, 0},
        .has_motion = true,
        // Steam Controller 2 (Triton) device frame -> canonical SDL frame:
        // (sx,sy,sz) = (+rawX, +rawZ, -rawY), per SDL's steam_triton driver.
        // Normalizing here (instead of in ps4_mode) lets every output stay
        // source-agnostic.
        .accel = {accel_x, accel_z, imu_negate_s16(accel_y)},
        .gyro  = {gyro_x,  gyro_z,  imu_negate_s16(gyro_y)},
        .gyro_range  = 2000,
        .accel_range = 4000,
        .battery_level = batt_level[dev_addr][instance],
        .battery_charging = batt_charging[dev_addr][instance],
        .has_touch = has_touch,
        .touch = {
            { .x = (uint16_t)lpad_x, .y = (uint16_t)lpad_y, .active = lpad_touch },
            { .x = (uint16_t)rpad_x, .y = (uint16_t)rpad_y, .active = rpad_touch },
        },
    };
    router_submit_input(&event);

    prev_buttons_lo[dev_addr][instance] = (uint8_t)(buttons & 0xFF);
}

DeviceInterface steam_controller_2_interface = {
    .name = "Valve Steam Controller 2",
    .is_device = sc2_is_device,
    .init = sc2_init,
    .process = sc2_process,
    .task = sc2_task,
    .unmount = sc2_unmount,
};
