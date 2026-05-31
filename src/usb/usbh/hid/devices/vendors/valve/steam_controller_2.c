// steam_controller_2.c - Valve Steam Controller 2 (codename "Triton" / "Roy")
//
// One driver covers two USB transports — they emit identical report layouts:
//   VID 0x28DE PID 0x1302  Direct wired SC2
//   VID 0x28DE PID 0x1304  SC2 paired through Valve's USB "puck" dongle
//
// Source for the report layout: jfedor2/hid-remapper firmware/src/quirks.cc
// @ commit 722ea05 (Valve firmware update changed report ID 0x42 → 0x45).
// The published HID descriptor on the device is unusable; layout is manual.
//
// SC2 ships with kbd + mouse + vendor HID interfaces all under the same
// VID/PID. is_device() matches all three; process() filters out the
// non-vendor reports by checking report ID. We do NOT currently send the
// "disable lizard mode" feature report — Steam itself sends it on real
// systems, but for joypad-os v1 we just ignore the kbd/mouse reports and
// rely on the vendor interface continuing to emit raw input. If we discover
// the vendor interface goes silent, init() is the place to add the
// SC1-style 0x81 + 0x87 feature reports.
//
// 12 of the 30 button bits are not yet identified — almost certainly the
// SC2's grip buttons, back paddles, and trackpad quadrant clicks. SC2_DEBUG
// builds log unknown bits + the Valve-proprietary touch/pressure axes so a
// follow-up commit can name them after hardware bring-up.
//
// SPDX-License-Identifier: Apache-2.0

#include "steam_controller_2.h"
#include "core/buttons.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "tusb.h"
#include <stdio.h>
#include <string.h>

#ifndef SC2_DEBUG
#define SC2_DEBUG 1   // unknown-bit logging on during development
#endif

// Minimum bytes we need before the parse is meaningful (through gyro Z).
// 64-byte interrupt-IN frame per the quirk layout; we accept anything ≥ 45.
#define SC2_MIN_REPORT_LEN 45

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

// --- Driver state ----------------------------------------------------------

static uint8_t prev_buttons_lo[CFG_TUH_DEVICE_MAX + 1][CFG_TUH_HID];
#if SC2_DEBUG
static uint32_t prev_unknown_bits[CFG_TUH_DEVICE_MAX + 1][CFG_TUH_HID];
#endif

// --- DeviceInterface callbacks --------------------------------------------

static bool sc2_is_device(uint16_t vid, uint16_t pid) {
    return vid == SC2_VID && (pid == SC2_PID_WIRED || pid == SC2_PID_PUCK);
}

static bool sc2_init(uint8_t dev_addr, uint8_t instance) {
    printf("[SC2] mounted dev_addr=%u instance=%u\n", dev_addr, instance);
    prev_buttons_lo[dev_addr][instance] = 0;
#if SC2_DEBUG
    prev_unknown_bits[dev_addr][instance] = 0;
#endif
    // No lizard-mode disable in v1. SC2 emits report ID 0x45 alongside the
    // kbd/mouse reports; process() filters by ID. Add 0x81 + 0x87 feature
    // reports here if hardware testing shows the vendor stream goes idle.
    return true;
}

static void sc2_unmount(uint8_t dev_addr, uint8_t instance) {
    printf("[SC2] unmounted dev_addr=%u instance=%u\n", dev_addr, instance);
    prev_buttons_lo[dev_addr][instance] = 0;
#if SC2_DEBUG
    prev_unknown_bits[dev_addr][instance] = 0;
#endif
}

static void sc2_task(uint8_t dev_addr, uint8_t instance,
                     device_output_config_t *config) {
    (void)dev_addr;
    (void)instance;
    (void)config;
    // v1: no output features (haptics/LED). Add via tuh_hid_send_report once
    // the SC2 output report layout is known.
}

static void sc2_process(uint8_t dev_addr, uint8_t instance,
                        const uint8_t *report, uint16_t len) {
    // Filter: SC2 vendor interface emits report ID 0x45. The kbd/mouse
    // interfaces (claimed by us via VID/PID match) emit different IDs and
    // are silently dropped here.
    if (len < SC2_MIN_REPORT_LEN || report[0] != SC2_INPUT_REPORT_ID) {
        return;
    }

    // --- Buttons (confirmed bits → JP_BUTTON_*) ---------------------------
    uint32_t buttons = 0;
    if (sc2_bit(report,  8)) buttons |= JP_BUTTON_B1;  // South / A / Cross
    if (sc2_bit(report,  9)) buttons |= JP_BUTTON_B2;  // East  / B / Circle
    if (sc2_bit(report, 10)) buttons |= JP_BUTTON_B3;  // West  / X / Square
    if (sc2_bit(report, 11)) buttons |= JP_BUTTON_B4;  // North / Y / Triangle
    if (sc2_bit(report, 27)) buttons |= JP_BUTTON_L1;  // LB
    if (sc2_bit(report, 17)) buttons |= JP_BUTTON_R1;  // RB
    if (sc2_bit(report, 35)) buttons |= JP_BUTTON_L2;  // LT digital
    if (sc2_bit(report, 31)) buttons |= JP_BUTTON_R2;  // RT digital
    if (sc2_bit(report, 22)) buttons |= JP_BUTTON_S1;  // Select / Back
    if (sc2_bit(report, 14)) buttons |= JP_BUTTON_S2;  // Start  / Menu
    if (sc2_bit(report, 23)) buttons |= JP_BUTTON_L3;  // Left stick click
    if (sc2_bit(report, 13)) buttons |= JP_BUTTON_R3;  // Right stick click
    if (sc2_bit(report, 24)) buttons |= JP_BUTTON_A1;  // Steam / Home

    // --- Sticks (Valve sends +Y = up, invert to HID convention 0=up) ------
    uint8_t lx = sc2_stick_to_u8(sc2_i16(report,  72));
    uint8_t ly = (uint8_t)(255 - sc2_stick_to_u8(sc2_i16(report,  88)));
    uint8_t rx = sc2_stick_to_u8(sc2_i16(report, 104));
    uint8_t ry = (uint8_t)(255 - sc2_stick_to_u8(sc2_i16(report, 120)));
    if (ly == 0) ly = 1;
    if (ry == 0) ry = 1;

    // --- Triggers (0..32767 unsigned-style, shift to 0..255) --------------
    uint16_t l2_raw = (uint16_t)sc2_i16(report, 40);
    uint16_t r2_raw = (uint16_t)sc2_i16(report, 56);
    uint8_t l2 = (uint8_t)(l2_raw >> 7);
    uint8_t r2 = (uint8_t)(r2_raw >> 7);

    // --- IMU --------------------------------------------------------------
    int16_t accel_x = sc2_i16(report, 264);
    int16_t accel_y = sc2_i16(report, 280);
    int16_t accel_z = sc2_i16(report, 296);
    int16_t gyro_x  = sc2_i16(report, 312);
    int16_t gyro_y  = sc2_i16(report, 328);
    int16_t gyro_z  = sc2_i16(report, 344);

#if SC2_DEBUG
    // Log unmapped button bits on change. These are almost certainly the
    // SC2 grip buttons, back paddles, trackpad-quadrant clicks, and
    // Valve-proprietary edge-touch hints (bits 18-21, page 0xfff9).
    static const int unknown_bits[] = {
        12, 15, 16, 18, 19, 20, 21, 25, 26, 28, 29, 30, 32, 33, 34, 36, 37,
    };
    uint32_t unknown_now = 0;
    for (unsigned i = 0; i < sizeof(unknown_bits) / sizeof(unknown_bits[0]); i++) {
        if (sc2_bit(report, unknown_bits[i])) unknown_now |= (1u << i);
    }
    if (unknown_now != prev_unknown_bits[dev_addr][instance]) {
        printf("[SC2] unknown bits:");
        for (unsigned i = 0; i < sizeof(unknown_bits) / sizeof(unknown_bits[0]); i++) {
            if (unknown_now & (1u << i)) printf(" %d", unknown_bits[i]);
        }
        printf("\n");
        prev_unknown_bits[dev_addr][instance] = unknown_now;
    }
#endif

    input_event_t event = {
        .dev_addr = dev_addr,
        .instance = instance,
        .type = INPUT_TYPE_GAMEPAD,
        .transport = INPUT_TRANSPORT_USB,
        .layout = LAYOUT_MODERN_4FACE,
        .buttons = buttons,
        .button_count = 13,
        .analog = {lx, ly, rx, ry, l2, r2, 0},
        .has_motion = true,
        .accel = {accel_x, accel_y, accel_z},
        .gyro  = {gyro_x,  gyro_y,  gyro_z},
        .gyro_range  = 2000,
        .accel_range = 4000,
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
