// sinput_descriptors.h - SInput USB HID descriptors
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// SInput protocol descriptors for SDL/Steam compatibility.
// Based on Handheld Legend's SInput HID specification.
//
// Features:
// - 32 buttons, 2 sticks (16-bit), 2 triggers (16-bit)
// - IMU (accelerometer + gyroscope)
// - Stereo haptic feedback
// - Player LED and RGB LED control
//
// Reference: https://docs.handheldlegend.com/s/sinput

#ifndef SINPUT_DESCRIPTORS_H
#define SINPUT_DESCRIPTORS_H

#include <stdint.h>
#include "tusb.h"

// ============================================================================
// SINPUT USB IDENTIFIERS
// ============================================================================

// Raspberry Pi commercial VID with SInput generic fallback PID
#define SINPUT_VID              0x2E8A  // Raspberry Pi
#define SINPUT_PID              0x10C6  // SInput generic
#define SINPUT_BCD_DEVICE       0x0100  // v1.0

// ============================================================================
// SINPUT BUTTON DEFINITIONS
// ============================================================================

// Button masks (32-bit across 4 bytes)
// Byte 0: Face buttons and D-pad
#define SINPUT_MASK_EAST        (1U <<  0)  // B2/Circle/B
#define SINPUT_MASK_SOUTH       (1U <<  1)  // B1/Cross/A
#define SINPUT_MASK_NORTH       (1U <<  2)  // B4/Triangle/Y
#define SINPUT_MASK_WEST        (1U <<  3)  // B3/Square/X
#define SINPUT_MASK_DU          (1U <<  4)  // D-pad Up
#define SINPUT_MASK_DD          (1U <<  5)  // D-pad Down
#define SINPUT_MASK_DL          (1U <<  6)  // D-pad Left
#define SINPUT_MASK_DR          (1U <<  7)  // D-pad Right

// Byte 1: Sticks, bumpers, triggers, paddles
#define SINPUT_MASK_L3          (1U <<  8)  // Left stick click
#define SINPUT_MASK_R3          (1U <<  9)  // Right stick click
#define SINPUT_MASK_L1          (1U << 10)  // Left bumper
#define SINPUT_MASK_R1          (1U << 11)  // Right bumper
#define SINPUT_MASK_L2          (1U << 12)  // Left trigger digital
#define SINPUT_MASK_R2          (1U << 13)  // Right trigger digital
#define SINPUT_MASK_L_PADDLE1   (1U << 14)  // Left paddle 1
#define SINPUT_MASK_R_PADDLE1   (1U << 15)  // Right paddle 1

// Byte 2: System buttons and more paddles
#define SINPUT_MASK_START       (1U << 16)  // Start/Options
#define SINPUT_MASK_BACK        (1U << 17)  // Back/Select
#define SINPUT_MASK_GUIDE       (1U << 18)  // Guide/Home
#define SINPUT_MASK_CAPTURE     (1U << 19)  // Capture/Share
#define SINPUT_MASK_L_PADDLE2   (1U << 20)  // Left paddle 2
#define SINPUT_MASK_R_PADDLE2   (1U << 21)  // Right paddle 2
#define SINPUT_MASK_TOUCHPAD1   (1U << 22)  // Touchpad 1 click
#define SINPUT_MASK_TOUCHPAD2   (1U << 23)  // Touchpad 2 click

// Byte 3: Power and misc
#define SINPUT_MASK_POWER       (1U << 24)  // Power button
#define SINPUT_MASK_MISC4       (1U << 25)
#define SINPUT_MASK_MISC5       (1U << 26)
#define SINPUT_MASK_MISC6       (1U << 27)
#define SINPUT_MASK_MISC7       (1U << 28)
#define SINPUT_MASK_MISC8       (1U << 29)
#define SINPUT_MASK_MISC9       (1U << 30)
#define SINPUT_MASK_MISC10      (1U << 31)

// ============================================================================
// SINPUT REPORT STRUCTURES
// ============================================================================

// Report IDs
#define SINPUT_REPORT_ID_INPUT    0x01
#define SINPUT_REPORT_ID_FEATURES 0x02
#define SINPUT_REPORT_ID_OUTPUT   0x03

// Output command types
#define SINPUT_CMD_HAPTIC         0x01
#define SINPUT_CMD_FEATURES       0x02
#define SINPUT_CMD_PLAYER_LED     0x03
#define SINPUT_CMD_RGB_LED        0x04

// Input Report (64 bytes including report ID)
typedef struct __attribute__((packed)) {
    uint8_t  report_id;         // 0x01
    uint8_t  plug_status;       // Plug/connection status
    uint8_t  charge_level;      // Battery charge level
    uint8_t  buttons[4];        // 32 buttons (little-endian)
    int16_t  lx;                // Left stick X (-32768 to 32767, 0 = center)
    int16_t  ly;                // Left stick Y
    int16_t  rx;                // Right stick X
    int16_t  ry;                // Right stick Y
    int16_t  lt;                // Left trigger (0 to 32767)
    int16_t  rt;                // Right trigger (0 to 32767)
    uint32_t imu_timestamp;     // IMU timestamp in microseconds
    int16_t  accel_x;           // Accelerometer X
    int16_t  accel_y;           // Accelerometer Y
    int16_t  accel_z;           // Accelerometer Z
    int16_t  gyro_x;            // Gyroscope X
    int16_t  gyro_y;            // Gyroscope Y
    int16_t  gyro_z;            // Gyroscope Z
    uint8_t  touchpad1[6];      // Touchpad 1: X(2), Y(2), Pressure(2)
    uint8_t  touchpad2[6];      // Touchpad 2: X(2), Y(2), Pressure(2)
    uint8_t  reserved[17];      // Padding to 64 bytes
} sinput_report_t;

_Static_assert(sizeof(sinput_report_t) == 64, "sinput_report_t must be 64 bytes");

// Output Report (48 bytes including report ID)
typedef struct __attribute__((packed)) {
    uint8_t  report_id;         // 0x03
    uint8_t  command;           // Command type (SINPUT_CMD_*)
    uint8_t  data[46];          // Command data
} sinput_output_t;

_Static_assert(sizeof(sinput_output_t) == 48, "sinput_output_t must be 48 bytes");

// Haptic command data (Type 2 - ERM simulation)
typedef struct __attribute__((packed)) {
    uint8_t  type;              // 2 = ERM type
    uint8_t  left_amplitude;    // Left motor amplitude (0-255)
    uint8_t  left_brake;        // Left motor brake (0 or 1)
    uint8_t  right_amplitude;   // Right motor amplitude (0-255)
    uint8_t  right_brake;       // Right motor brake (0 or 1)
} sinput_haptic_t;

// ============================================================================
// SINPUT USB DESCRIPTORS
// ============================================================================

// HID Report Descriptor for SInput
// Input: 64 bytes (Report ID 0x01)
// Feature Response: 24 bytes (Report ID 0x02)
// Output: 48 bytes (Report ID 0x03)
static const uint8_t sinput_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)

    // === Feature Response Report (12 bytes) ===
    // Sent as Input report in response to feature request command
    0x85, SINPUT_REPORT_ID_FEATURES,  // Report ID (2)
    0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined)
    0x09, 0x05,        //   Usage (Vendor Usage 5) - Feature Response
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x18,        //   Report Count (24)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // === Input Report (64 bytes) ===
    0x85, SINPUT_REPORT_ID_INPUT,  // Report ID (1)

    // Plug status and charge (2 bytes)
    0x05, 0x06,        //   Usage Page (Generic Device Controls)
    0x09, 0x20,        //   Usage (Battery Strength)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x02,        //   Report Count (2)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // 32 Buttons (4 bytes)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x20,        //   Usage Maximum (Button 32)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x20,        //   Report Count (32)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // Analog sticks (8 bytes) - 16-bit signed
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x30,        //   Usage (X) - Left X
    0x09, 0x31,        //   Usage (Y) - Left Y
    0x09, 0x32,        //   Usage (Z) - Right X
    0x09, 0x35,        //   Usage (Rz) - Right Y
    0x16, 0x00, 0x80,  //   Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,  //   Logical Maximum (32767)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x04,        //   Report Count (4)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // Triggers (4 bytes) - 16-bit signed
    0x09, 0x33,        //   Usage (Rx) - Left Trigger
    0x09, 0x34,        //   Usage (Ry) - Right Trigger
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x7F,  //   Logical Maximum (32767)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x02,        //   Report Count (2)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // IMU timestamp (4 bytes)
    0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined)
    0x09, 0x01,        //   Usage (Vendor Usage 1) - Timestamp
    0x15, 0x00,        //   Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0xFF, 0x7F,  // Logical Maximum (2147483647)
    0x75, 0x20,        //   Report Size (32)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // IMU data - Accelerometer and Gyroscope (12 bytes)
    0x05, 0x01,        //   Usage Page (Generic Desktop)
    0x09, 0x3D,        //   Usage (Vx) - Accel X
    0x09, 0x3E,        //   Usage (Vy) - Accel Y
    0x09, 0x3F,        //   Usage (Vz) - Accel Z
    0x09, 0x40,        //   Usage (Vbrx) - Gyro X
    0x09, 0x41,        //   Usage (Vbry) - Gyro Y
    0x09, 0x42,        //   Usage (Vbrz) - Gyro Z
    0x16, 0x00, 0x80,  //   Logical Minimum (-32768)
    0x26, 0xFF, 0x7F,  //   Logical Maximum (32767)
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x06,        //   Report Count (6)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // Touchpad data (12 bytes) + reserved (17 bytes) = 29 bytes vendor data
    0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined)
    0x09, 0x02,        //   Usage (Vendor Usage 2) - Touchpad/Reserved
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x1D,        //   Report Count (29) - touchpad + reserved
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // === Output Report (48 bytes) ===
    0x85, SINPUT_REPORT_ID_OUTPUT,  // Report ID (3)

    // Command byte
    0x06, 0x00, 0xFF,  //   Usage Page (Vendor Defined)
    0x09, 0x03,        //   Usage (Vendor Usage 3) - Command
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0x91, 0x02,        //   Output (Data,Var,Abs)

    // Command data (46 bytes)
    0x09, 0x04,        //   Usage (Vendor Usage 4) - Data
    0x95, 0x2E,        //   Report Count (46)
    0x91, 0x02,        //   Output (Data,Var,Abs)

    0xC0,              // End Collection
};

// ============================================================================
// SINPUT COMPOSITE: STANDALONE KEYBOARD REPORT DESCRIPTOR (no report ID)
// ============================================================================
// Standard 6-key rollover keyboard for separate HID interface

static const uint8_t sinput_keyboard_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)

    // Modifier keys (8 bits)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0xE0,        //   Usage Minimum (224 - Left Control)
    0x29, 0xE7,        //   Usage Maximum (231 - Right GUI)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)

    // Reserved byte
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Constant)

    // LED output report (for Caps/Num/Scroll Lock feedback)
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (1 - Num Lock)
    0x29, 0x05,        //   Usage Maximum (5 - Kana)
    0x91, 0x02,        //   Output (Data, Variable, Absolute)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x01,        //   Output (Constant) - padding

    // Keycodes (6 keys)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0x65,        //   Usage Maximum (101)
    0x81, 0x00,        //   Input (Data, Array)

    0xC0,              // End Collection
};

// ============================================================================
// SINPUT COMPOSITE: STANDALONE MOUSE REPORT DESCRIPTOR (no report ID)
// ============================================================================
// 5-button mouse with X, Y, wheel, and pan for separate HID interface

static const uint8_t sinput_mouse_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)

    // 5 Buttons
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (1)
    0x29, 0x05,        //     Usage Maximum (5)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x05,        //     Report Count (5)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data, Variable, Absolute)

    // 3 bits padding
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x03,        //     Report Size (3)
    0x81, 0x01,        //     Input (Constant)

    // X, Y movement (-127 to 127)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x02,        //     Report Count (2)
    0x81, 0x06,        //     Input (Data, Variable, Relative)

    // Vertical wheel (-127 to 127)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x06,        //     Input (Data, Variable, Relative)

    // Horizontal pan (-127 to 127)
    0x05, 0x0C,        //     Usage Page (Consumer)
    0x0A, 0x38, 0x02,  //     Usage (AC Pan)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x06,        //     Input (Data, Variable, Relative)

    0xC0,              //   End Collection (Physical)
    0xC0,              // End Collection (Mouse)
};

// Device descriptor
static const tusb_desc_device_t sinput_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,  // USB 2.0
#if CFG_TUD_CDC > 0
    // Use IAD for composite device with CDC
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
#else
    .bDeviceClass       = 0x00,    // Use class from interface
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
#endif
    .bMaxPacketSize0    = 64,
    .idVendor           = SINPUT_VID,
    .idProduct          = SINPUT_PID,
    .bcdDevice          = SINPUT_BCD_DEVICE,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

// String descriptors
#define SINPUT_MANUFACTURER  "Joypad"
#define SINPUT_PRODUCT       "Joypad (SInput)"

#endif // SINPUT_DESCRIPTORS_H
