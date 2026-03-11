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
    // ---- Xbox Gamepad Input (Report ID 3) ----
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x03,        //   Report ID (3)

    // 4 stick axes x 16-bit unsigned (0-65535)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x09, 0x30,        //     Usage (X)  - Left Stick X
    0x09, 0x31,        //     Usage (Y)  - Left Stick Y
    0x09, 0x32,        //     Usage (Z)  - Right Stick X
    0x09, 0x35,        //     Usage (Rz) - Right Stick Y
    0x15, 0x00,        //     Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00,  // Logical Maximum (65535)
    0x75, 0x10,        //     Report Size (16)
    0x95, 0x04,        //     Report Count (4)
    0x81, 0x02,        //     Input (Data, Var, Abs)
    0xC0,              //   End Collection (Physical)

    // 2 triggers x 16-bit (0-1023 range, stored in 16-bit field)
    0x09, 0x33,        //   Usage (Rx) - Left Trigger
    0x09, 0x34,        //   Usage (Ry) - Right Trigger
    0x15, 0x00,        //   Logical Minimum (0)
    0x27, 0xFF, 0x03, 0x00, 0x00,  // Logical Maximum (1023)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x02,        //   Report Count (2)
    0x81, 0x02,        //   Input (Data, Var, Abs)

    // Hat switch (1-8 directions, 0=center)
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x39,        //   Usage (Hat Switch)
    0x15, 0x01,        //   Logical Minimum (1)
    0x25, 0x08,        //   Logical Maximum (8)
    0x35, 0x00,        //   Physical Minimum (0)
    0x46, 0x3B, 0x01,  //   Physical Maximum (315)
    0x65, 0x14,        //   Unit (Eng Rot: Angular Pos)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x42,        //   Input (Data, Var, Abs, Null)
    0x65, 0x00,        //   Unit (None)

    // 15 buttons (matches Xbox BLE bit layout including gaps)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x0F,        //   Usage Maximum (Button 15)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x0F,        //   Report Count (15)
    0x81, 0x02,        //   Input (Data, Var, Abs)
    // 1 bit padding to complete the uint16
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x01,        //   Input (Constant)

    // Share button (1 bit) + 7 bits padding
    0x05, 0x09,        //   Usage Page (Button)
    0x09, 0x10,        //   Usage (Button 16)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data, Var, Abs)
    0x75, 0x07,        //   Report Size (7)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x01,        //   Input (Constant) - padding

    0xC0,              // End Collection

    // ---- Rumble Output (Report ID 3) ----
    // Same Report ID as input — real Xbox BLE uses ID 3 for both
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x03,        //   Report ID (3)

    // 8-byte rumble output (after Report ID)
    // [0]=enable, [1]=rt_trigger, [2]=lt_trigger, [3]=right_motor, [4]=left_motor,
    // [5]=duration, [6]=delay, [7]=repeat
    0x05, 0x0F,        //   Usage Page (Physical Interface)
    0x09, 0x21,        //   Usage (Set Effect Report)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x08,        //   Report Count (8)
    0x91, 0x02,        //   Output (Data, Var, Abs)

    0xC0,              // End Collection
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
