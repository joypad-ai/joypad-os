// ms_sidewinder_dualstrike.c - Microsoft SideWinder Dual Strike
// VID: 0x045E  PID: 0x0028
//
// Bit layout (40 bits / 5 bytes):
//   Bits  0-9:  X axis (10-bit signed, tilt L/R)
//   Bits 10-19: Y axis (10-bit signed, tilt F/B)
//   Bits 16-19: 3rd axis (4-bit signed, twist/rotation)
//   Bits 20-21: Constant
//   Bit  22:    L3 (tilt extreme click left)
//   Bit  23:    R3 (tilt extreme click right)
//   Bits 24-32: Buttons 1-9 (B4, B3, B2, B1, L1, R1, L2, R2, S2)
//   Bits 33-35: Constant
//   Bits 36-39: Hat switch (4-bit, standard 8-direction)
//
// SPDX-License-Identifier: Apache-2.0

#include "ms_sidewinder_dualstrike.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include <string.h>

#define MICROSOFT_VID       0x045E
#define DUALSTRIKE_PID      0x0028

static uint8_t prev_report[CFG_TUH_DEVICE_MAX + 1][CFG_TUH_HID][DUALSTRIKE_REPORT_SIZE];

static bool is_ms_sidewinder_dualstrike(uint16_t vid, uint16_t pid) {
    return (vid == MICROSOFT_VID && pid == DUALSTRIKE_PID);
}

static bool init_ms_sidewinder_dualstrike(uint8_t dev_addr, uint8_t instance) {
    printf("[DualStrike] Device mounted: dev_addr=%d, instance=%d\n", dev_addr, instance);
    memset(&prev_report[dev_addr][instance], 0, DUALSTRIKE_REPORT_SIZE);
    return true;
}

static void unmount_ms_sidewinder_dualstrike(uint8_t dev_addr, uint8_t instance) {
    printf("[DualStrike] Device unmounted: dev_addr=%d, instance=%d\n", dev_addr, instance);
}

// Sign-extend a 10-bit value to int16_t
static inline int16_t sign_extend_10(uint16_t val) {
    if (val & 0x200) {
        return (int16_t)(val | 0xFC00);  // Sign extend
    }
    return (int16_t)val;
}

// Sign-extend a 4-bit value to int8_t
static inline int8_t sign_extend_4(uint8_t val) {
    if (val & 0x8) {
        return (int8_t)(val | 0xF0);  // Sign extend
    }
    return (int8_t)val;
}

static void process_ms_sidewinder_dualstrike(uint8_t dev_addr, uint8_t instance,
                                              uint8_t const* report, uint16_t len) {
    if (len < DUALSTRIKE_REPORT_SIZE) return;

    // Check for changes
    if (memcmp(report, prev_report[dev_addr][instance], DUALSTRIKE_REPORT_SIZE) == 0) {
        return;
    }

    // Extract 10-bit X axis (bits 0-9)
    uint16_t raw_x = report[0] | ((uint16_t)(report[1] & 0x03) << 8);
    int16_t axis_x = sign_extend_10(raw_x);

    // Extract 10-bit Y axis (bits 10-19)
    uint16_t raw_y = (report[1] >> 2) | ((uint16_t)(report[2] & 0x0F) << 6);
    int16_t axis_y = sign_extend_10(raw_y);

    // Extract 4-bit 3rd axis / twist (bits 16-19, lower nibble of byte 2)
    uint8_t raw_twist = report[2] & 0x0F;
    int8_t twist = sign_extend_4(raw_twist);

    // Extract L3/R3 (bits 22-23)
    bool l3 = (report[2] >> 6) & 1;
    bool r3 = (report[2] >> 7) & 1;

    // Extract buttons 1-9 (bits 24-32)
    uint16_t btns = report[3] | ((uint16_t)(report[4] & 0x01) << 8);

    // Extract hat switch (bits 36-39)
    uint8_t hat = (report[4] >> 4) & 0x0F;

    // Hat switch to D-pad (standard 8-direction encoding)
    bool dpad_up    = (hat == 0 || hat == 1 || hat == 7);
    bool dpad_right = (hat >= 1 && hat <= 3);
    bool dpad_down  = (hat >= 3 && hat <= 5);
    bool dpad_left  = (hat >= 5 && hat <= 7);

    // Map buttons to JP_BUTTON format
    uint32_t buttons = 0;

    // D-pad
    if (dpad_up)    buttons |= JP_BUTTON_DU;
    if (dpad_down)  buttons |= JP_BUTTON_DD;
    if (dpad_left)  buttons |= JP_BUTTON_DL;
    if (dpad_right) buttons |= JP_BUTTON_DR;

    // Face buttons (bits 24-27: B4, B3, B2, B1)
    if (btns & (1 << 0)) buttons |= JP_BUTTON_B4;  // Button 1 = North
    if (btns & (1 << 1)) buttons |= JP_BUTTON_B3;  // Button 2 = West
    if (btns & (1 << 2)) buttons |= JP_BUTTON_B2;  // Button 3 = East
    if (btns & (1 << 3)) buttons |= JP_BUTTON_B1;  // Button 4 = South

    // Shoulders and triggers (bits 28-31: L1, R1, L2, R2)
    if (btns & (1 << 4)) buttons |= JP_BUTTON_L1;
    if (btns & (1 << 5)) buttons |= JP_BUTTON_R1;
    if (btns & (1 << 6)) buttons |= JP_BUTTON_L2;
    if (btns & (1 << 7)) buttons |= JP_BUTTON_R2;

    // Start (bit 32)
    if (btns & (1 << 8)) buttons |= JP_BUTTON_S2;

    // Tilt extreme clicks
    if (l3) buttons |= JP_BUTTON_L3;
    if (r3) buttons |= JP_BUTTON_R3;

    // Scale 10-bit signed (-512..511) to 8-bit unsigned (0..255, center 128)
    uint8_t analog_lx = (uint8_t)((axis_x + 512) * 255 / 1023);
    uint8_t analog_ly = (uint8_t)((axis_y + 512) * 255 / 1023);

    // Scale 4-bit signed (-8..7) to 8-bit unsigned (0..255, center 128)
    uint8_t analog_ry = (uint8_t)((twist + 8) * 255 / 15);

    // Ensure non-zero (internal convention)
    analog_lx = (analog_lx == 0) ? 1 : analog_lx;
    analog_ly = (analog_ly == 0) ? 1 : analog_ly;
    analog_ry = (analog_ry == 0) ? 1 : analog_ry;

    input_event_t event = {
        .dev_addr = dev_addr,
        .instance = instance,
        .type = INPUT_TYPE_GAMEPAD,
        .transport = INPUT_TRANSPORT_USB,
        .buttons = buttons,
        .button_count = 11,
        .analog = {128, 128, analog_lx, analog_ly, 0, 0},
        .keys = 0,
    };
    router_submit_input(&event);

    memcpy(prev_report[dev_addr][instance], report, DUALSTRIKE_REPORT_SIZE);
}

DeviceInterface ms_sidewinder_dualstrike_interface = {
    .name = "Microsoft SideWinder Dual Strike",
    .is_device = is_ms_sidewinder_dualstrike,
    .init = init_ms_sidewinder_dualstrike,
    .process = process_ms_sidewinder_dualstrike,
    .task = NULL,
    .unmount = unmount_ms_sidewinder_dualstrike,
};
