// ble_output_keyboard.c - BLE Keyboard Report Helpers
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Converts input_event_t keyboard data to BLE keyboard HID report.

#include "ble_output_keyboard.h"
#include <string.h>

void ble_keyboard_report_from_event(const input_event_t *event, ble_keyboard_report_t *report)
{
    memset(report, 0, sizeof(ble_keyboard_report_t));

    if (!event || event->type != INPUT_TYPE_KEYBOARD) return;

    // The keys field packs keycodes from LSB, with modifiers mixed in.
    // Keycodes 0xE0-0xE7 are modifier keys — extract them as modifier bits.
    // Regular keycodes go into the keycode array (up to 6).
    uint32_t keys = event->keys;
    uint8_t keycode_index = 0;

    for (int i = 0; i < 4 && keys != 0; i++) {
        uint8_t code = keys & 0xFF;
        keys >>= 8;
        if (code == 0) continue;

        if (code >= 0xE0 && code <= 0xE7) {
            // Modifier key — set corresponding bit
            report->modifier |= (1 << (code - 0xE0));
        } else {
            // Regular keycode
            if (keycode_index < 6) {
                report->keycode[keycode_index++] = code;
            }
        }
    }
}
