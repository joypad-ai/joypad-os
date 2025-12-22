// hid_descriptors.h - Generic HID gamepad descriptors
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
// Standard HID gamepad (16 buttons, 4 axes, 2 triggers, dpad)

#ifndef HID_DESCRIPTORS_H
#define HID_DESCRIPTORS_H

#include <stdint.h>

// HID Report Descriptor for Generic Gamepad (GP2040-CE compatible, PS3 support)
// 18 buttons, 4 axes (2 sticks), 1 dpad (hat switch), 12 pressure axes (PS3)
static const uint8_t hid_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x35, 0x00,        //   Physical Minimum (0)
    0x45, 0x01,        //   Physical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x12,        //   Report Count (18)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x12,        //   Usage Maximum (Button 18)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x95, 0x06,        //   Report Count (6)
    0x81, 0x01,        //   Input (Const,Ary,Abs) - 6 bit padding to byte align

    // D-pad (Hat Switch)
    0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
    0x25, 0x07,        //   Logical Maximum (7)
    0x46, 0x3B, 0x01,  //   Physical Maximum (315)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x65, 0x14,        //   Unit (Eng Rot:Angular Pos)
    0x09, 0x39,        //   Usage (Hat switch)
    0x81, 0x42,        //   Input (Data,Var,Abs,Null)
    0x65, 0x00,        //   Unit (None)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x01,        //   Input (Const,Ary,Abs) - 4 bit padding

    // Analog sticks (4 axes: X, Y, Z, Rz)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x46, 0xFF, 0x00,  //   Physical Maximum (255)
    0x09, 0x30,        //   Usage (X)  - Left Stick X
    0x09, 0x31,        //   Usage (Y)  - Left Stick Y
    0x09, 0x32,        //   Usage (Z)  - Right Stick X
    0x09, 0x35,        //   Usage (Rz) - Right Stick Y
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x04,        //   Report Count (4)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // PS3 pressure axes (Vendor Specific) - 12 bytes
    // D-pad: right, left, up, down
    // Buttons: triangle, circle, cross, square, L1, R1, L2, R2
    0x06, 0x00, 0xFF,  //   Usage Page (Vendor Specific 0xFF00)
    0x09, 0x20,        //   Usage (0x20) - D-pad Right pressure
    0x09, 0x21,        //   Usage (0x21) - D-pad Left pressure
    0x09, 0x22,        //   Usage (0x22) - D-pad Up pressure
    0x09, 0x23,        //   Usage (0x23) - D-pad Down pressure
    0x09, 0x24,        //   Usage (0x24) - Triangle pressure
    0x09, 0x25,        //   Usage (0x25) - Circle pressure
    0x09, 0x26,        //   Usage (0x26) - Cross pressure
    0x09, 0x27,        //   Usage (0x27) - Square pressure
    0x09, 0x28,        //   Usage (0x28) - L1 pressure
    0x09, 0x29,        //   Usage (0x29) - R1 pressure
    0x09, 0x2A,        //   Usage (0x2A) - L2 pressure
    0x09, 0x2B,        //   Usage (0x2B) - R2 pressure
    0x95, 0x0C,        //   Report Count (12)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    0xC0,              // End Collection
};

// HID Report structure (matches descriptor above)
// 20 bytes total: buttons + hat + sticks + PS3 pressure
typedef struct __attribute__((packed)) {
    uint8_t  buttons_lo;    // Buttons 1-8: B3,B1,B2,B4,L1,R1,L2,R2
    uint8_t  buttons_mid;   // Buttons 9-16: S1,S2,L3,R3,A1,A2,A3,A4
    uint8_t  buttons_hi;    // Buttons 17-18: L4,R4 + 6 padding bits
    uint8_t  hat;           // D-pad: low 4 bits = hat (0-7, 8=center), high 4 bits = padding
    uint8_t  lx;            // Left stick X (0-255, 128 = center)
    uint8_t  ly;            // Left stick Y (0-255, 128 = center)
    uint8_t  rx;            // Right stick X (0-255, 128 = center)
    uint8_t  ry;            // Right stick Y (0-255, 128 = center)
    // PS3 pressure axes (vendor specific)
    uint8_t  pressure_dpad_right;
    uint8_t  pressure_dpad_left;
    uint8_t  pressure_dpad_up;
    uint8_t  pressure_dpad_down;
    uint8_t  pressure_triangle;  // B4
    uint8_t  pressure_circle;    // B2
    uint8_t  pressure_cross;     // B1
    uint8_t  pressure_square;    // B3
    uint8_t  pressure_l1;
    uint8_t  pressure_r1;
    uint8_t  pressure_l2;
    uint8_t  pressure_r2;
} joypad_hid_report_t;

// D-pad / Hat Switch values
#define HID_HAT_UP          0
#define HID_HAT_UP_RIGHT    1
#define HID_HAT_RIGHT       2
#define HID_HAT_DOWN_RIGHT  3
#define HID_HAT_DOWN        4
#define HID_HAT_DOWN_LEFT   5
#define HID_HAT_LEFT        6
#define HID_HAT_UP_LEFT     7
#define HID_HAT_CENTER      8

#endif // HID_DESCRIPTORS_H
