// ble_output_xbox.h - Xbox BLE Gamepad Report Helpers
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#ifndef BLE_OUTPUT_XBOX_H
#define BLE_OUTPUT_XBOX_H

#include "core/input_event.h"
#include <stdint.h>
#include <stdbool.h>

// Xbox BLE input report (16 bytes, Report ID 3)
// Matches Xbox One S / Series X BLE format
typedef struct __attribute__((packed)) {
    uint16_t lx;            // Left stick X  (0-65535, 32768=center)
    uint16_t ly;            // Left stick Y  (0-65535, 32768=center)
    uint16_t rx;            // Right stick X (0-65535, 32768=center)
    uint16_t ry;            // Right stick Y (0-65535, 32768=center)
    uint16_t lt;            // Left trigger  (0-1023)
    uint16_t rt;            // Right trigger (0-1023)
    uint8_t hat;            // Hat switch (0=center, 1=N ... 8=NW)
    uint16_t buttons;       // Button bitfield (Xbox layout with gaps)
    uint8_t share;          // Share button (bit 0) + padding
} ble_xbox_report_t;

// Xbox BLE rumble output report (8 bytes, Report ID 4)
typedef struct __attribute__((packed)) {
    uint8_t enable;         // Actuator enable bits (0x03 = strong + weak)
    uint8_t lt_force;       // Left trigger rumble (0-100)
    uint8_t rt_force;       // Right trigger rumble (0-100)
    uint8_t strong_force;   // Strong motor (0-100)
    uint8_t weak_force;     // Weak motor (0-100)
    uint8_t duration;       // Duration (0xFF = continuous)
    uint8_t delay;          // Delay
    uint8_t repeat;         // Repeat count
} ble_xbox_rumble_t;

// Xbox button masks (matching real Xbox BLE controller layout)
#define XBOX_OUT_A               0x0001
#define XBOX_OUT_B               0x0002
#define XBOX_OUT_X               0x0008
#define XBOX_OUT_Y               0x0010
#define XBOX_OUT_LB              0x0040
#define XBOX_OUT_RB              0x0080
#define XBOX_OUT_VIEW            0x0400
#define XBOX_OUT_MENU            0x0800
#define XBOX_OUT_GUIDE           0x1000
#define XBOX_OUT_L3              0x2000
#define XBOX_OUT_R3              0x4000

// Get Xbox BLE HID descriptor
const uint8_t* ble_xbox_get_descriptor(void);
uint16_t ble_xbox_get_descriptor_size(void);

// Convert input_event_t to Xbox BLE report
void ble_xbox_report_from_event(const input_event_t *event, ble_xbox_report_t *report);

// Parse rumble output report, returns true if valid
// Sets rumble_left (strong) and rumble_right (weak) in 0-255 range
bool ble_xbox_parse_rumble(const uint8_t *data, uint16_t len,
                           uint8_t *rumble_left, uint8_t *rumble_right);

#endif // BLE_OUTPUT_XBOX_H
