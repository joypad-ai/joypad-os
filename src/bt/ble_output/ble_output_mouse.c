// ble_output_mouse.c - BLE Mouse Report Helpers
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Converts input_event_t mouse data to BLE mouse HID report.

#include "ble_output_mouse.h"
#include "core/buttons.h"
#include <string.h>

void ble_mouse_report_from_event(const input_event_t *event, ble_mouse_report_t *report)
{
    memset(report, 0, sizeof(ble_mouse_report_t));

    if (!event || event->type != INPUT_TYPE_MOUSE) return;

    // Map JP_BUTTON_* to mouse buttons
    uint8_t buttons = 0;
    if (event->buttons & JP_BUTTON_B1) buttons |= (1 << 0);  // Left
    if (event->buttons & JP_BUTTON_B2) buttons |= (1 << 1);  // Right
    if (event->buttons & JP_BUTTON_B3) buttons |= (1 << 2);  // Middle
    if (event->buttons & JP_BUTTON_S1) buttons |= (1 << 3);  // Back
    if (event->buttons & JP_BUTTON_S2) buttons |= (1 << 4);  // Forward

    report->buttons = buttons;
    report->x = event->delta_x;
    report->y = event->delta_y;
    report->wheel = event->delta_wheel;
}
