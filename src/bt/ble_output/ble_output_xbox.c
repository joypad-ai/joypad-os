// ble_output_xbox.c - Xbox BLE Gamepad Report Helpers
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Converts input_event_t to Xbox BLE gamepad format (matching Xbox One S / Series X).
// HID descriptor matches the real Xbox controller's BLE report layout.

#include "ble_output_xbox.h"
#include "core/buttons.h"
#include <string.h>

// ============================================================================
// XBOX BLE HID DESCRIPTOR
// ============================================================================
// Matches the Xbox One S / Series X BLE HID report format.
// Report ID 3: Input (16 bytes) - gamepad
// Report ID 4: Output (8 bytes) - rumble

static const uint8_t xbox_hid_descriptor[] = {
    // Byte-exact HID report map read from a real Xbox Series X pad (model
    // 1914, PnP 045E:0B13) — via Mystfit/ESP32-BLE-CompositeHID
    // (XboxOneS_1914_HIDDescriptor, MIT). Hosts key their Xbox handling on
    // identity AND these bytes; do not tidy or reorder. Reports: 0x01 INPUT
    // 16 B (sticks 4x16-bit, Sim-page Brake/Accelerator 10-bit+pad, 4-bit
    // hat+pad, 15 gapped buttons+pad, Consumer Record+pad), 0x03 OUTPUT 8 B
    // (PID rumble: enable nibble, 4 magnitudes 0-100, duration, delay, loop).
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x85, 0x01, 0x09, 0x01, 0xA1, 0x00,
    0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x27, 0xFF, 0xFF, 0x00, 0x00, 0x95,
    0x02, 0x75, 0x10, 0x81, 0x02, 0xC0, 0x09, 0x01, 0xA1, 0x00, 0x09, 0x32,
    0x09, 0x35, 0x15, 0x00, 0x27, 0xFF, 0xFF, 0x00, 0x00, 0x95, 0x02, 0x75,
    0x10, 0x81, 0x02, 0xC0, 0x05, 0x02, 0x09, 0xC5, 0x15, 0x00, 0x26, 0xFF,
    0x03, 0x95, 0x01, 0x75, 0x0A, 0x81, 0x02, 0x15, 0x00, 0x25, 0x00, 0x75,
    0x06, 0x95, 0x01, 0x81, 0x03, 0x05, 0x02, 0x09, 0xC4, 0x15, 0x00, 0x26,
    0xFF, 0x03, 0x95, 0x01, 0x75, 0x0A, 0x81, 0x02, 0x15, 0x00, 0x25, 0x00,
    0x75, 0x06, 0x95, 0x01, 0x81, 0x03, 0x05, 0x01, 0x09, 0x39, 0x15, 0x01,
    0x25, 0x08, 0x35, 0x00, 0x46, 0x3B, 0x01, 0x66, 0x14, 0x00, 0x75, 0x04,
    0x95, 0x01, 0x81, 0x42, 0x75, 0x04, 0x95, 0x01, 0x15, 0x00, 0x25, 0x00,
    0x35, 0x00, 0x45, 0x00, 0x65, 0x00, 0x81, 0x03, 0x05, 0x09, 0x19, 0x01,
    0x29, 0x0F, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x0F, 0x81, 0x02,
    0x15, 0x00, 0x25, 0x00, 0x75, 0x01, 0x95, 0x01, 0x81, 0x03, 0x05, 0x0C,
    0x0A, 0xB2, 0x00, 0x15, 0x00, 0x25, 0x01, 0x95, 0x01, 0x75, 0x01, 0x81,
    0x02, 0x15, 0x00, 0x25, 0x00, 0x75, 0x07, 0x95, 0x01, 0x81, 0x03, 0x05,
    0x0F, 0x09, 0x21, 0x85, 0x03, 0xA1, 0x02, 0x09, 0x97, 0x15, 0x00, 0x25,
    0x01, 0x75, 0x04, 0x95, 0x01, 0x91, 0x02, 0x15, 0x00, 0x25, 0x00, 0x75,
    0x04, 0x95, 0x01, 0x91, 0x03, 0x09, 0x70, 0x15, 0x00, 0x25, 0x64, 0x75,
    0x08, 0x95, 0x04, 0x91, 0x02, 0x09, 0x50, 0x66, 0x01, 0x10, 0x55, 0x0E,
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x01, 0x91, 0x02, 0x09,
    0xA7, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x01, 0x91, 0x02,
    0x65, 0x00, 0x55, 0x00, 0x09, 0x7C, 0x15, 0x00, 0x26, 0xFF, 0x00, 0x75,
    0x08, 0x95, 0x01, 0x91, 0x02, 0xC0, 0xC0
};

// ============================================================================
// DESCRIPTOR ACCESS
// ============================================================================

const uint8_t* ble_xbox_get_descriptor(void)
{
    return xbox_hid_descriptor;
}

uint16_t ble_xbox_get_descriptor_size(void)
{
    return sizeof(xbox_hid_descriptor);
}

// ============================================================================
// REPORT CONVERSION
// ============================================================================

// Convert dpad buttons to Xbox hat switch value
// Xbox: 0=center, 1=N, 2=NE, 3=E, 4=SE, 5=S, 6=SW, 7=W, 8=NW
static uint8_t convert_dpad_to_xbox_hat(uint32_t buttons)
{
    uint8_t up    = (buttons & JP_BUTTON_DU) ? 1 : 0;
    uint8_t down  = (buttons & JP_BUTTON_DD) ? 1 : 0;
    uint8_t left  = (buttons & JP_BUTTON_DL) ? 1 : 0;
    uint8_t right = (buttons & JP_BUTTON_DR) ? 1 : 0;

    if (up && right) return 2;  // NE
    if (up && left)  return 8;  // NW
    if (down && right) return 4; // SE
    if (down && left)  return 6; // SW
    if (up)    return 1;  // N
    if (down)  return 5;  // S
    if (left)  return 7;  // W
    if (right) return 3;  // E

    return 0;  // Center
}

void ble_xbox_report_from_event(const input_event_t *event, ble_xbox_report_t *report)
{
    memset(report, 0, sizeof(ble_xbox_report_t));

    // Scale 8-bit sticks (0-255) to 16-bit unsigned (0-65535)
    #define SCALE_8_TO_U16(v) ((uint16_t)((uint32_t)(v) * 65535 / 255))
    report->lx = SCALE_8_TO_U16(event->analog[ANALOG_LX]);
    report->ly = SCALE_8_TO_U16(event->analog[ANALOG_LY]);
    report->rx = SCALE_8_TO_U16(event->analog[ANALOG_RX]);
    report->ry = SCALE_8_TO_U16(event->analog[ANALOG_RY]);

    // Scale 8-bit triggers (0-255) to 10-bit (0-1023)
    report->lt = (uint16_t)((uint32_t)event->analog[ANALOG_L2] * 1023 / 255);
    report->rt = (uint16_t)((uint32_t)event->analog[ANALOG_R2] * 1023 / 255);

    // Hat switch
    report->hat = convert_dpad_to_xbox_hat(event->buttons);

    // Xbox button bitfield (matches real Xbox BLE bit positions, with gaps)
    uint16_t btn = 0;
    if (event->buttons & JP_BUTTON_B1) btn |= XBOX_OUT_A;
    if (event->buttons & JP_BUTTON_B2) btn |= XBOX_OUT_B;
    if (event->buttons & JP_BUTTON_B3) btn |= XBOX_OUT_X;
    if (event->buttons & JP_BUTTON_B4) btn |= XBOX_OUT_Y;
    if (event->buttons & JP_BUTTON_L1) btn |= XBOX_OUT_LB;
    if (event->buttons & JP_BUTTON_R1) btn |= XBOX_OUT_RB;
    if (event->buttons & JP_BUTTON_S1) btn |= XBOX_OUT_VIEW;
    if (event->buttons & JP_BUTTON_S2) btn |= XBOX_OUT_MENU;
    if (event->buttons & JP_BUTTON_A1) btn |= XBOX_OUT_GUIDE;
    if (event->buttons & JP_BUTTON_L3) btn |= XBOX_OUT_L3;
    if (event->buttons & JP_BUTTON_R3) btn |= XBOX_OUT_R3;
    report->buttons = btn;

    // Share button (byte 15, bit 0)
    if (event->buttons & JP_BUTTON_A2) report->share = 0x01;
}

// ============================================================================
// RUMBLE PARSING
// ============================================================================

bool ble_xbox_parse_rumble(const uint8_t *data, uint16_t len,
                           uint8_t *rumble_left, uint8_t *rumble_right)
{
    if (len < 5) return false;

    // Xbox BLE rumble format:
    // [0]=enable_mask, [1]=rt_trigger, [2]=lt_trigger,
    // [3]=right_motor, [4]=left_motor, [5]=duration, [6]=delay, [7]=repeat
    // Motor values are 0-100, scale to 0-255
    uint8_t right = data[3];
    uint8_t left = data[4];

    *rumble_left = (uint8_t)(((uint16_t)left * 255) / 100);
    *rumble_right = (uint8_t)(((uint16_t)right * 255) / 100);

    return true;
}
