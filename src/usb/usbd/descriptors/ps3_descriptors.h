// ps3_descriptors.h - PlayStation 3 DualShock 3 USB descriptors
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Sony DualShock 3 controller emulation with full PS3 compatibility.
// Includes sixaxis data, pressure-sensitive buttons, and feature reports.
//
// Reference: OGX-Mini (BSD-3-Clause)

#ifndef PS3_DESCRIPTORS_H
#define PS3_DESCRIPTORS_H

#include <stdint.h>
#include <string.h>
#include "tusb.h"

// ============================================================================
// PS3 USB IDENTIFIERS
// ============================================================================
#if (USB_OUTPUT_PADS > 1)
    #define PS3_VID              0x10C4  /* Silicon Laboratories, Inc. */
    #define PS3_PID              0x82C0
    #define PS3_BCD_DEVICE       0x0100  /* v1.0 */
#else
    #define PS3_VID              0x054C  // Sony
    #define PS3_PID              0x0268  // DualShock 3
    #define PS3_BCD_DEVICE       0x0100  // v1.0
#endif

// ============================================================================
// PS3 CONSTANTS
// ============================================================================

#define PS3_JOYSTICK_MID     0x7F
#define PS3_SIXAXIS_MID      0x0200  // 512 - center for accelerometer/gyro (10-bit ADC)
#define PS3_SIXAXIS_MID_BE   0x0002  // Big-endian representation of 512

// Motion data byte offsets (relative to report start, after report ID)
// Motion data is big-endian 16-bit values
#define PS3_MOTION_OFFSET    40      // Start of motion data in 48-byte report (after report ID stripped)
#define PS3_ACCEL_X_OFFSET   40
#define PS3_ACCEL_Y_OFFSET   42
#define PS3_ACCEL_Z_OFFSET   44
#define PS3_GYRO_Z_OFFSET    46

// Report IDs
#define PS3_REPORT_ID_FEATURE_00 0x00  // Magic Handshake
#define PS3_REPORT_ID_INPUT      0x01
#define PS3_REPORT_ID_FEATURE_01 0x01
#define PS3_REPORT_ID_FEATURE_EF 0xEF
#define PS3_REPORT_ID_PAIRING    0xF2
#define PS3_REPORT_ID_FEATURE_F4 0xF4
#define PS3_REPORT_ID_FEATURE_F5 0xF5
#define PS3_REPORT_ID_FEATURE_F7 0xF7
#define PS3_REPORT_ID_FEATURE_F8 0xF8

// Plug/Power states
#define PS3_PLUGGED          0x02
#define PS3_UNPLUGGED        0x03
#define PS3_POWER_CHARGING   0xEE
#define PS3_POWER_NOT_CHARGING 0xF1
#define PS3_POWER_FULL       0x05

// Rumble states
#define PS3_RUMBLE_WIRED     0x10
#define PS3_RUMBLE_WIRED_OFF 0x12

// ============================================================================
// PS3 BUTTON DEFINITIONS
// ============================================================================

// HAT report (4 bits)
#define PS3_HAT_UP        0x00
#define PS3_HAT_UPRIGHT   0x01
#define PS3_HAT_RIGHT     0x02
#define PS3_HAT_DOWNRIGHT 0x03
#define PS3_HAT_DOWN      0x04
#define PS3_HAT_DOWNLEFT  0x05
#define PS3_HAT_LEFT      0x06
#define PS3_HAT_UPLEFT    0x07
#define PS3_HAT_NOTHING   0x08

// Buttons byte 0
#define PS3_BTN_SELECT       0x01
#define PS3_BTN_L3           0x02
#define PS3_BTN_R3           0x04
#define PS3_BTN_START        0x08
#define PS3_BTN_DPAD_UP      0x10
#define PS3_BTN_DPAD_RIGHT   0x20
#define PS3_BTN_DPAD_DOWN    0x40
#define PS3_BTN_DPAD_LEFT    0x80

// Buttons byte 1
#define PS3_BTN_L2           0x01
#define PS3_BTN_R2           0x02
#define PS3_BTN_L1           0x04
#define PS3_BTN_R1           0x08
#define PS3_BTN_TRIANGLE     0x10
#define PS3_BTN_CIRCLE       0x20
#define PS3_BTN_CROSS        0x40
#define PS3_BTN_SQUARE       0x80

// Buttons byte 2
#define PS3_BTN_PS           0x01
#define PS3_BTN_TP           0x02  // Not used on DS3

// ============================================================================
// PS3 REPORT STRUCTURES
// ============================================================================

// Input Report - 50 bytes (49 + 1 padding byte for WebHID motion alignment)
typedef struct __attribute__((packed)) {
    uint8_t  report_id;          // 0x01
    uint8_t  reserved0;          // 0x00

    uint8_t  buttons[3];         // Digital buttons
    uint8_t  reserved1;          // 0x00

    uint8_t  lx;                 // Left stick X (0x00-0xFF, 0x7F center)
    uint8_t  ly;                 // Left stick Y (0x00-0xFF, 0x7F center)
    uint8_t  rx;                 // Right stick X
    uint8_t  ry;                 // Right stick Y

    uint8_t  reserved2[2];       // 0x00
    uint8_t  power_status;       // Battery/power status
    uint8_t  reserved3;          // 0x00

    // Pressure-sensitive buttons (0x00 = released, 0xFF = fully pressed)
    uint8_t  pressure_up;
    uint8_t  pressure_right;
    uint8_t  pressure_down;
    uint8_t  pressure_left;

    uint8_t  pressure_l2;
    uint8_t  pressure_r2;
    uint8_t  pressure_l1;
    uint8_t  pressure_r1;

    uint8_t  pressure_triangle;
    uint8_t  pressure_circle;
    uint8_t  pressure_cross;
    uint8_t  pressure_square;

    uint8_t  reserved4[3];       // 0x00

    uint8_t  plugged;            // 0x02 = plugged, 0x03 = unplugged
    uint8_t  power;              // Power state
    uint8_t  rumble_status;      // 0x10 = wired rumble

    uint8_t  reserved5[10];      // 0x00 (extra byte to align motion at WebHID offset 41)

    // Sixaxis data (10-bit, big-endian)
    // After WebHID strips report_id, these are at offsets 41-48
    uint16_t accel_x;            // Accelerometer X
    uint16_t accel_y;            // Accelerometer Y
    uint16_t accel_z;            // Accelerometer Z
    uint16_t gyro_z;             // Gyroscope Z
} ps3_in_report_t;

_Static_assert(sizeof(ps3_in_report_t) == 50, "ps3_in_report_t must be 50 bytes");

typedef struct __attribute__((packed)) {
    uint8_t  buttons[3];         // Digital buttons

    uint8_t  lx;                 // Left stick X
    uint8_t  ly;                 // Left stick Y
    uint8_t  rx;                 // Right stick X
    uint8_t  ry;                 // Right stick Y

    uint8_t  pressure_right;
    uint8_t  pressure_left;
    uint8_t  pressure_up;
    uint8_t  pressure_down;

    uint8_t  pressure_square;
    uint8_t  pressure_cross;
    uint8_t  pressure_circle;
    uint8_t  pressure_triangle;

    uint8_t  pressure_l2;
    uint8_t  pressure_r2;
    uint8_t  pressure_l1;
    uint8_t  pressure_r1;

    uint16_t accel_x;            // Accelerometer X
    uint16_t accel_y;            // Accelerometer Y
    uint16_t accel_z;            // Accelerometer Z
    uint16_t gyro_z;             // Gyroscope Z
} ps3_in_report_alt_t;

_Static_assert(sizeof(ps3_in_report_alt_t) == 27, "ps3_in_report_alt_t must be 28 bytes");

// Output Report - 48 bytes (rumble and LEDs)
typedef struct __attribute__((packed)) {
    uint8_t  reserved0;
    uint8_t  rumble_right_duration;  // Right motor duration (0xFF = forever)
    uint8_t  rumble_right_on;        // Right motor on/off (0 or 1)
    uint8_t  rumble_left_duration;   // Left motor duration
    uint8_t  rumble_left_force;      // Left motor force (0-255)
    uint8_t  reserved1[4];
    uint8_t  leds_bitmap;            // LED bitmap: LED1=0x02, LED2=0x04, etc.
    // LED timing parameters (5 bytes each, 4 LEDs + 1 unused)
    uint8_t  led_data[25];
    uint8_t  reserved2[13];
} ps3_out_report_t;

_Static_assert(sizeof(ps3_out_report_t) == 48, "ps3_out_report_t must be 48 bytes");

// Bluetooth pairing info (Feature report 0xF2)
typedef struct __attribute__((packed)) {
    uint8_t  reserved0[2];
    uint8_t  device_address[7];  // Leading zero + 6-byte address
    uint8_t  host_address[7];    // Leading zero + 6-byte address
    uint8_t  reserved1;
} ps3_pairing_info_t;

_Static_assert(sizeof(ps3_pairing_info_t) == 17, "ps3_pairing_info_t must be 17 bytes");

// ============================================================================
// PS3 FEATURE REPORT DATA
// ============================================================================

// Feature report 0x01 response makes the PS button work
static const uint8_t ps3_feature_00[] = {
    0x21, 0x26, 0x01, 0x07, 0x00, 0x00, 0x00, 0x00
};

// Feature report 0x01 response
static const uint8_t ps3_feature_01[] = {
    0x01, 0x04, 0x00, 0x0b, 0x0c, 0x01, 0x02, 0x18,
    0x18, 0x18, 0x18, 0x09, 0x0a, 0x10, 0x11, 0x12,
    0x13, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x02,
    0x02, 0x02, 0x02, 0x00, 0x00, 0x00, 0x04, 0x04,
    0x04, 0x04, 0x00, 0x00, 0x04, 0x00, 0x01, 0x02,
    0x07, 0x00, 0x17, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Feature report 0xEF response (calibration data)
static const uint8_t ps3_feature_ef[] = {
    0xef, 0x04, 0x00, 0x0b, 0x03, 0x01, 0xa0, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0xff, 0x01, 0xff, 0x01, 0xff, 0x01, 0xff,
    0x01, 0xff, 0x01, 0xff, 0x01, 0xff, 0x01, 0xff,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06,
};

// Feature report 0xF7 response
static const uint8_t ps3_feature_f7[] = {
    0x02, 0x01, 0xf8, 0x02, 0xe2, 0x01, 0x05, 0xff,
    0x04, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// Feature report 0xF5 response (host pairing info, 64 bytes)
static const uint8_t ps3_feature_f5[] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// Feature report 0xF8 response
static const uint8_t ps3_feature_f8[] = {
    0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// ============================================================================
// PS3 USB DESCRIPTORS
// ============================================================================

// Device descriptor
static const tusb_desc_device_t ps3_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,  // USB 2.0
    .bDeviceClass       = 0x00,    // Use class from interface
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = 64,
    .idVendor           = PS3_VID,
    .idProduct          = PS3_PID,
    .bcdDevice          = PS3_BCD_DEVICE,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x00,
    .bNumConfigurations = 0x01
};

// HID Report Descriptor (148 bytes)
static const uint8_t ps3_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x04,        // Usage (Joystick)
    0xA1, 0x01,        // Collection (Physical)
    0xA1, 0x02,        //   Collection (Application)
    0x85, 0x01,        //     Report ID (1)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x15, 0x00,        //     Logical Minimum (0)
    0x26, 0xFF, 0x00,  //     Logical Maximum (255)
    0x81, 0x03,        //     Input (Const,Var,Abs) - reserved byte
    0x75, 0x01,        //     Report Size (1)
    0x95, 0x13,        //     Report Count (19)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x35, 0x00,        //     Physical Minimum (0)
    0x45, 0x01,        //     Physical Maximum (1)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (0x01)
    0x29, 0x13,        //     Usage Maximum (0x13)
    0x81, 0x02,        //     Input (Data,Var,Abs)
    0x75, 0x01,        //     Report Size (1)
    0x95, 0x0D,        //     Report Count (13)
    0x06, 0x00, 0xFF,  //     Usage Page (Vendor Defined)
    0x81, 0x03,        //     Input (Const,Var,Abs) - padding
    0x15, 0x00,        //     Logical Minimum (0)
    0x26, 0xFF, 0x00,  //     Logical Maximum (255)
    0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
    0x09, 0x01,        //     Usage (Pointer)
    0xA1, 0x00,        //     Collection (Undefined)
    0x75, 0x08,        //       Report Size (8)
    0x95, 0x04,        //       Report Count (4)
    0x35, 0x00,        //       Physical Minimum (0)
    0x46, 0xFF, 0x00,  //       Physical Maximum (255)
    0x09, 0x30,        //       Usage (X)
    0x09, 0x31,        //       Usage (Y)
    0x09, 0x32,        //       Usage (Z)
    0x09, 0x35,        //       Usage (Rz)
    0x81, 0x02,        //       Input (Data,Var,Abs) - 4 joystick axes
    0xC0,              //     End Collection
    0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x28,        //     Report Count (40) - includes padding for motion alignment
    0x09, 0x01,        //     Usage (Pointer)
    0x81, 0x02,        //     Input (Data,Var,Abs)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x30,        //     Report Count (48)
    0x09, 0x01,        //     Usage (Pointer)
    0x91, 0x02,        //     Output (Data,Var,Abs)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x30,        //     Report Count (48)
    0x09, 0x01,        //     Usage (Pointer)
    0xB1, 0x02,        //     Feature (Data,Var,Abs)
    0xC0,              //   End Collection
    0xA1, 0x02,        //   Collection (Application)
    0x85, 0x02,        //     Report ID (2)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x30,        //     Report Count (48)
    0x09, 0x01,        //     Usage (Pointer)
    0xB1, 0x02,        //     Feature (Data,Var,Abs)
    0xC0,              //   End Collection
    0xA1, 0x02,        //   Collection (Application)
    0x85, 0xEE,        //     Report ID (238)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x30,        //     Report Count (48)
    0x09, 0x01,        //     Usage (Pointer)
    0xB1, 0x02,        //     Feature (Data,Var,Abs)
    0xC0,              //   End Collection
    0xA1, 0x02,        //   Collection (Application)
    0x85, 0xEF,        //     Report ID (239)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x30,        //     Report Count (48)
    0x09, 0x01,        //     Usage (Pointer)
    0xB1, 0x02,        //     Feature (Data,Var,Abs)
    0xC0,              //   End Collection
    0xC0,              // End Collection
};

// HID Alternative Report Descriptor (157 bytes)
static const uint8_t ps3_alt_report_descriptor[] =
{
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0xA1, 0x02,        //   Collection (Logical)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x35, 0x00,        //     Physical Minimum (0)
    0x45, 0x01,        //     Physical Maximum (1)
    0x75, 0x01,        //     Report Size (1)
    0x95, 0x0D,        //     Report Count (13)
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (0x01)
    0x29, 0x0D,        //     Usage Maximum (0x0D)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x03,        //     Report Count (3)
    0x81, 0x01,        //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x05, 0x01,        //     Usage Page (Generic Desktop Ctrls)
    0x25, 0x07,        //     Logical Maximum (7)
    0x46, 0x3B, 0x01,  //     Physical Maximum (315)
    0x75, 0x04,        //     Report Size (4)
    0x95, 0x01,        //     Report Count (1)
    0x65, 0x14,        //     Unit (System: English Rotation, Length: Centimeter)
    0x09, 0x39,        //     Usage (Hat switch)
    0x81, 0x42,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,Null State)
    0x65, 0x00,        //     Unit (None)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x01,        //     Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x26, 0xFF, 0x00,  //     Logical Maximum (255)
    0x46, 0xFF, 0x00,  //     Physical Maximum (255)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x32,        //     Usage (Z)
    0x09, 0x35,        //     Usage (Rz)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x04,        //     Report Count (4)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x06, 0x00, 0xFF,  //     Usage Page (Vendor Defined 0xFF00)
    0x09, 0x20,        //     Usage (0x20)
    0x09, 0x21,        //     Usage (0x21)
    0x09, 0x22,        //     Usage (0x22)
    0x09, 0x23,        //     Usage (0x23)
    0x09, 0x24,        //     Usage (0x24)
    0x09, 0x25,        //     Usage (0x25)
    0x09, 0x26,        //     Usage (0x26)
    0x09, 0x27,        //     Usage (0x27)
    0x09, 0x28,        //     Usage (0x28)
    0x09, 0x29,        //     Usage (0x29)
    0x09, 0x2A,        //     Usage (0x2A)
    0x09, 0x2B,        //     Usage (0x2B)
    0x95, 0x0C,        //     Report Count (12)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x0A, 0x21, 0x26,  //     Usage (0x2621)
    0x95, 0x08,        //     Report Count (8)
    0xB1, 0x02,        //     Feature (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x0A, 0x21, 0x26,  //     Usage (0x2621)
    0x91, 0x02,        //     Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x26, 0xFF, 0x03,  //     Logical Maximum (1023)
    0x46, 0xFF, 0x03,  //     Physical Maximum (1023)
    0x09, 0x2C,        //     Usage (0x2C)
    0x09, 0x2D,        //     Usage (0x2D)
    0x09, 0x2E,        //     Usage (0x2E)
    0x09, 0x2F,        //     Usage (0x2F)
    0x75, 0x10,        //     Report Size (16)
    0x95, 0x04,        //     Report Count (4)
    0x81, 0x02,        //     Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,              //   End Collection
    0xA1, 0x02,        //   Collection (Logical)
    0x26, 0xFF, 0x00,  //     Logical Maximum (255)
    0x46, 0xFF, 0x00,  //     Physical Maximum (255)
    0x95, 0x07,        //     Report Count (7)
    0x75, 0x08,        //     Report Size (8)
    0x09, 0x03,        //     Usage (0x03)
    0x91, 0x02,        //     Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0xC0,              //   End Collection
    0xC0,              // End Collection
};

// Configuration descriptor (41 bytes)
#define PS3_CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + (TUD_HID_INOUT_DESC_LEN * USB_OUTPUT_PADS))
#if (USB_OUTPUT_PADS > 1)
    #define PS3_REPORT_DESC_SIZE  sizeof(ps3_alt_report_descriptor)
#else
    #define PS3_REPORT_DESC_SIZE  sizeof(ps3_report_descriptor)
#endif
#define PS3_INTERFACE_DESC(_itfnum) \
    /* Interface Descriptor */ \
    9, TUSB_DESC_INTERFACE, (_itfnum), 0, 2, TUSB_CLASS_HID, 0, 0, 0, \
    /* HID Descriptor */ \
    9, HID_DESC_TYPE_HID, U16_TO_U8S_LE(0x0111), 0, 1, HID_DESC_TYPE_REPORT, U16_TO_U8S_LE(PS3_REPORT_DESC_SIZE), \
    /* Endpoint OUT: 0x02, 0x03, 0x04... */ \
    7, TUSB_DESC_ENDPOINT, (uint8_t)((_itfnum) + 0x02), TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(64), 0x01, \
    /* Endpoint IN: 0x81, 0x82, 0x83... */ \
    7, TUSB_DESC_ENDPOINT, (uint8_t)((_itfnum) + 0x81), TUSB_XFER_INTERRUPT, U16_TO_U8S_LE(64), 0x01

static const uint8_t ps3_config_descriptor[] = {
    // Config descriptor
    TUD_CONFIG_DESCRIPTOR(1, USB_OUTPUT_PADS, 0, PS3_CONFIG_TOTAL_LEN, 0x80, 250),  // 500mA

    PS3_INTERFACE_DESC(0),

#if (USB_OUTPUT_PADS > 1)
    PS3_INTERFACE_DESC(1),
#endif
#if (USB_OUTPUT_PADS > 2)
    PS3_INTERFACE_DESC(2),
#endif
#if (USB_OUTPUT_PADS > 3)
    PS3_INTERFACE_DESC(3),
#endif
};

// String descriptors
#define PS3_MANUFACTURER  "Sony"
#define PS3_PRODUCT       "PLAYSTATION(R)3 Controller"

// ============================================================================
// PS3 HELPER FUNCTIONS
// ============================================================================

// Initialize PS3 input report to neutral state
static inline void ps3_init_report(ps3_in_report_t* report)
{
    memset(report, 0, sizeof(ps3_in_report_t));
    report->report_id = PS3_REPORT_ID_INPUT;
    report->lx = PS3_JOYSTICK_MID;
    report->ly = PS3_JOYSTICK_MID;
    report->rx = PS3_JOYSTICK_MID;
    report->ry = PS3_JOYSTICK_MID;
    report->plugged = PS3_PLUGGED;
    report->power = PS3_POWER_FULL;
    report->rumble_status = PS3_RUMBLE_WIRED;
    // Sixaxis neutral (big-endian 0x0200 = 512)
    report->accel_x = PS3_SIXAXIS_MID_BE;
    report->accel_y = PS3_SIXAXIS_MID_BE;
    report->accel_z = PS3_SIXAXIS_MID_BE;
    report->gyro_z = PS3_SIXAXIS_MID_BE;
}

static inline void ps3_init_report_alt(ps3_in_report_alt_t* report)
{
    // Use the size of the alternative structure
    memset(report, 0, sizeof(ps3_in_report_alt_t));
    
    // Set sticks to neutral (0x7F = 127)
    report->lx = PS3_JOYSTICK_MID;
    report->ly = PS3_JOYSTICK_MID;
    report->rx = PS3_JOYSTICK_MID;
    report->ry = PS3_JOYSTICK_MID;
    
    // Set D-Pad to neutral (8 = neutral for Hat Switch)
    report->buttons[2] = PS3_HAT_NOTHING;
    
    // Set Sixaxis to neutral (big-endian 0x0002 = 512)
    report->accel_x = PS3_SIXAXIS_MID_BE;
    report->accel_y = PS3_SIXAXIS_MID_BE;
    report->accel_z = PS3_SIXAXIS_MID_BE;
    report->gyro_z  = PS3_SIXAXIS_MID_BE;
}

#endif // PS3_DESCRIPTORS_H
