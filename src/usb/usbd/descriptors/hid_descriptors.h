// hid_descriptors.h - Generic HID gamepad descriptors
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
// Standard HID gamepad (16 buttons, 4 axes, 2 triggers, dpad)

#ifndef HID_DESCRIPTORS_H
#define HID_DESCRIPTORS_H

#include <stdint.h>

// HID Report Descriptor for Generic Gamepad (GP2040-CE compatible)
// 14 buttons, 4 axes (2 sticks), 1 dpad (hat switch)
static const uint8_t hid_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x35, 0x00,        //   Physical Minimum (0)
    0x45, 0x01,        //   Physical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x0E,        //   Report Count (14)
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x0E,        //   Usage Maximum (Button 14)
    0x81, 0x02,        //   Input (Data,Var,Abs)
    0x95, 0x02,        //   Report Count (2)
    0x81, 0x01,        //   Input (Const,Ary,Abs) - 2 bit padding

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

    0xC0,              // End Collection
};

// HID Report structure (matches descriptor above)
// 7 bytes total: 14 buttons + 2 padding + 4-bit hat + 4-bit padding + 4 axes
typedef struct __attribute__((packed)) {
    uint16_t buttons;   // 14 buttons + 2 padding bits (B1-B4, L1-R1, L2-R2, S1-S2, L3-R3, A1-A2)
    uint8_t  hat;       // D-pad: low 4 bits = hat (0-7, 8=center), high 4 bits = padding
    uint8_t  lx;        // Left stick X (0-255, 128 = center)
    uint8_t  ly;        // Left stick Y (0-255, 128 = center)
    uint8_t  rx;        // Right stick X (0-255, 128 = center)
    uint8_t  ry;        // Right stick Y (0-255, 128 = center)
} usbretro_hid_report_t;

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
