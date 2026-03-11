// ble_output_mouse.h - BLE Mouse Report Helpers
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#ifndef BLE_OUTPUT_MOUSE_H
#define BLE_OUTPUT_MOUSE_H

#include "core/input_event.h"
#include <stdint.h>

// 4-byte mouse report (buttons + X + Y + wheel)
typedef struct __attribute__((packed)) {
    uint8_t buttons;    // Button states (5 buttons)
    int8_t x;           // X movement (-127 to 127)
    int8_t y;           // Y movement (-127 to 127)
    int8_t wheel;       // Vertical scroll (-127 to 127)
} ble_mouse_report_t;

// Convert input_event_t mouse data to BLE mouse report
void ble_mouse_report_from_event(const input_event_t *event, ble_mouse_report_t *report);

#endif // BLE_OUTPUT_MOUSE_H
