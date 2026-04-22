// ble_output_keyboard.h - BLE Keyboard Report Helpers
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#ifndef BLE_OUTPUT_KEYBOARD_H
#define BLE_OUTPUT_KEYBOARD_H

#include "core/input_event.h"
#include <stdint.h>

// Standard 8-byte keyboard report (modifier + reserved + 6 keycodes)
typedef struct __attribute__((packed)) {
    uint8_t modifier;       // Modifier keys (Ctrl, Shift, Alt, GUI)
    uint8_t reserved;       // Reserved byte (always 0)
    uint8_t keycode[6];     // Up to 6 simultaneous keycodes
} ble_keyboard_report_t;

// Convert input_event_t keyboard data to BLE keyboard report
void ble_keyboard_report_from_event(const input_event_t *event, ble_keyboard_report_t *report);

#endif // BLE_OUTPUT_KEYBOARD_H
