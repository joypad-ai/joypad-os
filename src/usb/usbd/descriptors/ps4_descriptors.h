// ps4_descriptors.h - PlayStation 4 controller descriptors
// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: Copyright (c) 2024 OpenStickCommunity (gp2040-ce.info)
// SPDX-FileCopyrightText: Copyright (c) 2024 Robert Dale Smith
//
// PlayStation 4 (DualShock 4) USB controller emulation.
// Uses Razer Panthera VID/PID for compatibility.
// Includes auth feature reports (0xF0-0xF3) for future passthrough support.

#ifndef PS4_DESCRIPTORS_H
#define PS4_DESCRIPTORS_H

#include <stdint.h>
#include "tusb.h"

// ============================================================================
// USB IDENTIFIERS
// ============================================================================

// Using Hori Fighting Commander for better Gamepad compatibility
#define PS4_VID             0x0F0D  // Hori
#define PS4_PID             0x0086  // Fighting Commander
#define PS4_BCD             0x0100  // v1.00
#define PS4_MANUFACTURER    "HORI CO., LTD."
#define PS4_PRODUCT         "Fighting Commander"

#define PS4_ENDPOINT_SIZE   64

// ============================================================================
// HAT SWITCH VALUES
// ============================================================================

#define PS4_HAT_UP          0x00
#define PS4_HAT_UP_RIGHT    0x01
#define PS4_HAT_RIGHT       0x02
#define PS4_HAT_DOWN_RIGHT  0x03
#define PS4_HAT_DOWN        0x04
#define PS4_HAT_DOWN_LEFT   0x05
#define PS4_HAT_LEFT        0x06
#define PS4_HAT_UP_LEFT     0x07
#define PS4_HAT_NOTHING     0x08  // Null state - Brook and many DS4 use 0x08 instead of 0x0F

// ============================================================================
// BUTTON MASKS
// ============================================================================

#define PS4_MASK_SQUARE     (1U <<  0)
#define PS4_MASK_CROSS      (1U <<  1)
#define PS4_MASK_CIRCLE     (1U <<  2)
#define PS4_MASK_TRIANGLE   (1U <<  3)
#define PS4_MASK_L1         (1U <<  4)
#define PS4_MASK_R1         (1U <<  5)
#define PS4_MASK_L2         (1U <<  6)
#define PS4_MASK_R2         (1U <<  7)
#define PS4_MASK_SELECT     (1U <<  8)  // Share
#define PS4_MASK_START      (1U <<  9)  // Options
#define PS4_MASK_L3         (1U << 10)
#define PS4_MASK_R3         (1U << 11)
#define PS4_MASK_PS         (1U << 12)
#define PS4_MASK_TP         (1U << 13)  // Touchpad click

// ============================================================================
// ANALOG CONSTANTS
// ============================================================================

#define PS4_JOYSTICK_MIN    0x00
#define PS4_JOYSTICK_MID    0x80
#define PS4_JOYSTICK_MAX    0xFF

// ============================================================================
// REPORT STRUCTURES
// ============================================================================

// Touchpad finger data (4 bytes per finger)
typedef struct __attribute__((packed)) {
    uint8_t counter : 7;
    uint8_t unpressed : 1;
    uint8_t data[3];  // 12-bit X, 12-bit Y
} ps4_touchpad_finger_t;

// Touchpad data (both fingers)
typedef struct __attribute__((packed)) {
    ps4_touchpad_finger_t p1;
    ps4_touchpad_finger_t p2;
} ps4_touchpad_data_t;

// PS4 Input Report (Report ID 1, 64 bytes total)
// Byte layout:
//   0:     report_id
//   1-4:   sticks (lx, ly, rx, ry)
//   5:     dpad(4) + square + cross + circle + triangle
//   6:     L1 + R1 + L2 + R2 + share + options + L3 + R3
//   7:     PS + tpad + counter(6)
//   8-9:   triggers (l2_trigger, r2_trigger)
//   10-11: timestamp
//   12:    padding
//   13-63: sensor data + touchpad
typedef struct __attribute__((packed)) {
    uint8_t report_id;          // 0x01
    uint8_t lx;                 // Left stick X
    uint8_t ly;                 // Left stick Y
    uint8_t rx;                 // Right stick X
    uint8_t ry;                 // Right stick Y

    // Byte 5: D-pad (4 bits) + face buttons (4 bits)
    uint8_t dpad : 4;           // Hat switch (0-7, 0x08 = center)
    uint8_t square : 1;
    uint8_t cross : 1;
    uint8_t circle : 1;
    uint8_t triangle : 1;

    // Byte 6: Shoulder buttons + select/start + stick clicks
    uint8_t l1 : 1;
    uint8_t r1 : 1;
    uint8_t l2 : 1;
    uint8_t r2 : 1;
    uint8_t share : 1;
    uint8_t options : 1;
    uint8_t l3 : 1;
    uint8_t r3 : 1;

    // Byte 7: PS + touchpad + counter
    uint8_t ps : 1;
    uint8_t tpad : 1;
    uint8_t counter : 6;

    // Bytes 8-9: Analog triggers (separate from bitfield!)
    uint8_t l2_trigger;
    uint8_t r2_trigger;

    // Bytes 10-11: Timestamp
    uint16_t timestamp;

    // Bytes 12-32: Sensor data (gyro/accel/status)
    uint8_t mystery[21];

    // Byte 33: Touchpad active status
    uint8_t touchpad_active;

    // Byte 34: Touchpad increment/counter
    uint8_t touchpad_increment;

    // Bytes 35-42: Touchpad data (2 fingers)
    ps4_touchpad_data_t touchpad;

    // Bytes 43-63: Padding to 64 bytes
    uint8_t mystery2[21];
} ps4_in_report_t;

// PS4 Output Report (Report ID 5, 32 bytes)
typedef struct __attribute__((packed)) {
    uint8_t report_id;          // 0x05
    uint8_t set_rumble : 1;
    uint8_t set_led : 1;
    uint8_t set_led_blink : 1;
    uint8_t set_ext_write : 1;
    uint8_t set_left_volume : 1;
    uint8_t set_right_volume : 1;
    uint8_t set_mic_volume : 1;
    uint8_t set_speaker_volume : 1;
    uint8_t set_flags2;
    uint8_t reserved;
    uint8_t motor_right;        // Small motor
    uint8_t motor_left;         // Large motor
    uint8_t lightbar_red;
    uint8_t lightbar_green;
    uint8_t lightbar_blue;
    uint8_t lightbar_blink_on;
    uint8_t lightbar_blink_off;
    uint8_t ext_data[8];
    uint8_t volume_left;
    uint8_t volume_right;
    uint8_t volume_mic;
    uint8_t volume_speaker;
    uint8_t other[9];
} ps4_out_report_t;

// Helper to initialize report to neutral state
static inline void ps4_init_report(ps4_in_report_t* report) {
    memset(report, 0, sizeof(ps4_in_report_t));
    report->report_id = 0x01;
    report->lx = PS4_JOYSTICK_MID;
    report->ly = PS4_JOYSTICK_MID;
    report->rx = PS4_JOYSTICK_MID;
    report->ry = PS4_JOYSTICK_MID;
    report->dpad = PS4_HAT_NOTHING;  // Now 0x08 for neutral
    report->touchpad_active = 0;
    report->touchpad_increment = 0;
    // Touchpad fingers unpressed and centered (X=960, Y=471)
    report->touchpad.p1.unpressed = 1;
    report->touchpad.p1.data[0] = 0xC0; // X LSB
    report->touchpad.p1.data[1] = 0x73; // X MSB / Y LSB
    report->touchpad.p1.data[2] = 0x1D; // Y MSB
    report->touchpad.p2.unpressed = 1;
    report->touchpad.p2.data[0] = 0xC0;
    report->touchpad.p2.data[1] = 0x73;
    report->touchpad.p2.data[2] = 0x1D;
}

// ============================================================================
// DEVICE DESCRIPTOR
// ============================================================================

static const tusb_desc_device_t ps4_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,  // USB 2.0
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = 64,
    .idVendor           = PS4_VID,
    .idProduct          = PS4_PID,
    .bcdDevice          = PS4_BCD,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x00,  // Disable Serial Number
    .bNumConfigurations = 0x01
};

// ============================================================================
// HID REPORT DESCRIPTOR
// ============================================================================

// Full PS4 HID report descriptor including auth feature reports
static const uint8_t ps4_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)

    // Report ID 1: Input Report
    0x85, 0x01,        //   Report ID (1)

    // Left/Right sticks (4 axes, 8 bits each)
    0x09, 0x30,        //   Usage (X)
    0x09, 0x31,        //   Usage (Y)
    0x09, 0x32,        //   Usage (Z)
    0x09, 0x35,        //   Usage (Rz)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x04,        //   Report Count (4)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // D-pad (hat switch, 4 bits)
    0x09, 0x39,        //   Usage (Hat switch)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x07,        //   Logical Maximum (7)
    0x35, 0x00,        //   Physical Minimum (0)
    0x46, 0x3B, 0x01,  //   Physical Maximum (315)
    0x65, 0x14,        //   Unit (Eng Rot: Degree)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x42,        //   Input (Data,Var,Abs,Null)

    // 14 buttons
    0x65, 0x00,        //   Unit (None)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (1)
    0x29, 0x0E,        //   Usage Maximum (14)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x0E,        //   Report Count (14)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // 6-bit counter (vendor specific)
    0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
    0x09, 0x20,        //   Usage (0x20)
    0x75, 0x06,        //   Report Size (6)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // Triggers (Rx, Ry)
    0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
    0x09, 0x33,        //   Usage (Rx)
    0x09, 0x34,        //   Usage (Ry)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x02,        //   Report Count (2)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // Vendor-specific data (54 bytes - gyro, accel, touchpad, etc.)
    0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined 0xFF00)
    0x09, 0x21,        //   Usage (0x21)
    0x95, 0x36,        //   Report Count (54)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // Report ID 5: Output Report (LED/Rumble)
    0x85, 0x05,        //   Report ID (5)
    0x09, 0x22,        //   Usage (0x22)
    0x95, 0x1F,        //   Report Count (31)
    0x91, 0x02,        //   Output (Data,Var,Abs)

    // Report ID 3: Feature Report (controller definition)
    0x85, 0x03,        //   Report ID (3)
    0x0A, 0x21, 0x27,  //   Usage (0x2721)
    0x95, 0x2F,        //   Report Count (47)
    0xB1, 0x02,        //   Feature (Data,Var,Abs)

    0xC0,              // End Collection

    // Auth Feature Reports (separate collection)
    0x06, 0xF0, 0xFF,  // Usage Page (Vendor Defined 0xFFF0)
    0x09, 0x40,        // Usage (0x40)
    0xA1, 0x01,        // Collection (Application)
    // Global items reset after End Collection above; re-declare them here
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)

    // Report ID 0xF0: Set Auth Payload (nonce from console)
    0x85, 0xF0,        //   Report ID (240)
    0x09, 0x47,        //   Usage (0x47)
    0x95, 0x3F,        //   Report Count (63)
    0xB1, 0x02,        //   Feature (Data,Var,Abs)

    // Report ID 0xF1: Get Signature Nonce (response to console)
    0x85, 0xF1,        //   Report ID (241)
    0x09, 0x48,        //   Usage (0x48)
    0x95, 0x3F,        //   Report Count (63)
    0xB1, 0x02,        //   Feature (Data,Var,Abs)

    // Report ID 0xF2: Get Signing State
    0x85, 0xF2,        //   Report ID (242)
    0x09, 0x49,        //   Usage (0x49)
    0x95, 0x0F,        //   Report Count (15)
    0xB1, 0x02,        //   Feature (Data,Var,Abs)

    // Report ID 0xF3: Reset Auth
    0x85, 0xF3,        //   Report ID (243)
    0x0A, 0x01, 0x47,  //   Usage (0x4701)
    0x95, 0x07,        //   Report Count (7)
    0xB1, 0x02,        //   Feature (Data,Var,Abs)

    0xC0,              // End Collection
};

// ============================================================================
// CONFIGURATION DESCRIPTOR
// ============================================================================

// Config descriptor length: Config(9) + Interface(9) + HID(9) + EP_IN(7) + EP_OUT(7) = 41
#define PS4_CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

static const uint8_t ps4_config_descriptor[] = {
    // Configuration descriptor
    0x09,                           // bLength
    TUSB_DESC_CONFIGURATION,        // bDescriptorType
    U16_TO_U8S_LE(PS4_CONFIG_TOTAL_LEN), // wTotalLength
    0x01,                           // bNumInterfaces
    0x01,                           // bConfigurationValue
    0x00,                           // iConfiguration
    0x80,                           // bmAttributes (bus powered)
    0x32,                           // bMaxPower (100mA)

    // Interface descriptor
    0x09,                           // bLength
    TUSB_DESC_INTERFACE,            // bDescriptorType
    0x00,                           // bInterfaceNumber
    0x00,                           // bAlternateSetting
    0x02,                           // bNumEndpoints (IN + OUT)
    TUSB_CLASS_HID,                 // bInterfaceClass
    0x00,                           // bInterfaceSubClass (No Boot)
    0x00,                           // bInterfaceProtocol
    0x00,                           // iInterface

    // HID descriptor
    0x09,                           // bLength
    HID_DESC_TYPE_HID,              // bDescriptorType
    U16_TO_U8S_LE(0x0111),          // bcdHID (1.11)
    0x00,                           // bCountryCode
    0x01,                           // bNumDescriptors
    HID_DESC_TYPE_REPORT,           // bDescriptorType[0]
    U16_TO_U8S_LE(sizeof(ps4_report_descriptor)), // wDescriptorLength[0]

    // Endpoint descriptor (IN)
    0x07,                           // bLength
    TUSB_DESC_ENDPOINT,             // bDescriptorType
    0x81,                           // bEndpointAddress (EP1 IN)
    TUSB_XFER_INTERRUPT,            // bmAttributes
    U16_TO_U8S_LE(PS4_ENDPOINT_SIZE), // wMaxPacketSize
    0x01,                           // bInterval (1ms)

    // Endpoint descriptor (OUT) - for rumble/LED output reports
    0x07,                           // bLength
    TUSB_DESC_ENDPOINT,             // bDescriptorType
    0x02,                           // bEndpointAddress (EP2 OUT)
    TUSB_XFER_INTERRUPT,            // bmAttributes
    U16_TO_U8S_LE(PS4_ENDPOINT_SIZE), // wMaxPacketSize
    0x01,                           // bInterval (1ms)
};

// ============================================================================
// AUTH REPORT IDS (for future passthrough)
// ============================================================================

#define PS4_REPORT_ID_INPUT         0x01
#define PS4_REPORT_ID_OUTPUT        0x05
#define PS4_REPORT_ID_FEATURE_03    0x03
#define PS4_REPORT_ID_AUTH_PAYLOAD  0xF0  // Console sends nonce
#define PS4_REPORT_ID_AUTH_RESPONSE 0xF1  // Controller sends signature
#define PS4_REPORT_ID_AUTH_STATUS   0xF2  // Signing state
#define PS4_REPORT_ID_AUTH_RESET    0xF3  // Reset auth

// ============================================================================
// FEATURE REPORT DATA (from GP2040-CE)
// ============================================================================

// Controller definition report (0x03) - 48 bytes
// Byte 4: 0x00 = PS4 controller, 0x07 = PS5 controller
static const uint8_t ps4_feature_03[] = {
    0x21, 0x27, 0x04, 0xcf, 0x00, 0x2c, 0x56,
    0x08, 0x00, 0x3d, 0x00, 0xe8, 0x03, 0x04, 0x00,
    0xff, 0x7f, 0x0d, 0x0d, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Auth reset report (0xF3) - nonce/response page sizes
// Byte 0: padding, Byte 1: nonce page size (0x38=56), Byte 2: response page size (0x38=56)
static const uint8_t ps4_feature_f3[] = { 0x00, 0x38, 0x38, 0x00, 0x00, 0x00, 0x00 };

#endif // PS4_DESCRIPTORS_H
