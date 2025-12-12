// xac_descriptors.h - Xbox Adaptive Controller compatible descriptors
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Xbox Adaptive Controller (XAC) compatible HID gamepad.
// Simple HID gamepad format that XAC recognizes as auxiliary input.
// Based on hid-remapper's xac_compat descriptor.

#ifndef XAC_DESCRIPTORS_H
#define XAC_DESCRIPTORS_H

#include <stdint.h>
#include "tusb.h"

// ============================================================================
// USB IDENTIFIERS
// ============================================================================

// Generic HID gamepad identifiers (XAC accepts standard HID gamepads)
#define XAC_VID           0x2563  // SHANWAN (same as HID mode)
#define XAC_PID           0x0576  // Different PID to distinguish from DInput
#define XAC_BCD           0x0100  // v1.00
#define XAC_MANUFACTURER  "Joypad"
#define XAC_PRODUCT       "USBR (XAC)"

#define XAC_ENDPOINT_SIZE 64

// ============================================================================
// BUTTON MASKS (12 buttons)
// ============================================================================

#define XAC_MASK_B1       (1U << 0)   // A
#define XAC_MASK_B2       (1U << 1)   // B
#define XAC_MASK_B3       (1U << 2)   // X
#define XAC_MASK_B4       (1U << 3)   // Y
#define XAC_MASK_L1       (1U << 4)   // LB
#define XAC_MASK_R1       (1U << 5)   // RB
#define XAC_MASK_L2       (1U << 6)   // LT (digital)
#define XAC_MASK_R2       (1U << 7)   // RT (digital)
#define XAC_MASK_S1       (1U << 8)   // Back/View
#define XAC_MASK_S2       (1U << 9)   // Start/Menu
#define XAC_MASK_L3       (1U << 10)  // LS
#define XAC_MASK_R3       (1U << 11)  // RS

// Hat switch values (same as standard HID)
#define XAC_HAT_UP          0
#define XAC_HAT_UP_RIGHT    1
#define XAC_HAT_RIGHT       2
#define XAC_HAT_DOWN_RIGHT  3
#define XAC_HAT_DOWN        4
#define XAC_HAT_DOWN_LEFT   5
#define XAC_HAT_LEFT        6
#define XAC_HAT_UP_LEFT     7
#define XAC_HAT_CENTER      8  // Null state

// Joystick center value
#define XAC_JOYSTICK_MID    0x80

// ============================================================================
// REPORT STRUCTURE (6 bytes)
// ============================================================================

typedef struct __attribute__((packed)) {
    uint8_t lx;        // Left stick X (0-255, 0x80 center)
    uint8_t ly;        // Left stick Y (0-255, 0x80 center)
    uint8_t rx;        // Right stick X (0-255, 0x80 center)
    uint8_t ry;        // Right stick Y (0-255, 0x80 center)
    uint8_t hat : 4;   // D-pad hat switch (0-7, 8=neutral)
    uint8_t buttons_lo : 4;  // Buttons 1-4 (A, B, X, Y)
    uint8_t buttons_hi;      // Buttons 5-12 (LB, RB, LT, RT, Back, Start, LS, RS)
} xac_in_report_t;

// Helper to initialize report to neutral state
static inline void xac_init_report(xac_in_report_t* report) {
    report->lx = XAC_JOYSTICK_MID;
    report->ly = XAC_JOYSTICK_MID;
    report->rx = XAC_JOYSTICK_MID;
    report->ry = XAC_JOYSTICK_MID;
    report->hat = XAC_HAT_CENTER;
    report->buttons_lo = 0;
    report->buttons_hi = 0;
}

// ============================================================================
// DEVICE DESCRIPTOR
// ============================================================================

static const tusb_desc_device_t xac_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,  // USB 2.0
    .bDeviceClass       = 0x00,    // Use class info in Interface Descriptors
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = 64,
    .idVendor           = XAC_VID,
    .idProduct          = XAC_PID,
    .bcdDevice          = XAC_BCD,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

// ============================================================================
// HID REPORT DESCRIPTOR
// ============================================================================

// XAC-compatible HID report descriptor (from hid-remapper)
// Simple format: 4 axes + hat switch + 12 buttons
static const uint8_t xac_report_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)

    // 4 analog axes (X, Y, Z, Rz) - 8 bits each
    0x09, 0x30,        //   Usage (X)
    0x09, 0x31,        //   Usage (Y)
    0x09, 0x32,        //   Usage (Z)
    0x09, 0x35,        //   Usage (Rz)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x04,        //   Report Count (4)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // Hat switch (D-pad) - 4 bits, values 0-7, null state
    0x09, 0x39,        //   Usage (Hat switch)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x07,        //   Logical Maximum (7)
    0x35, 0x00,        //   Physical Minimum (0)
    0x46, 0x3B, 0x01,  //   Physical Maximum (315)
    0x65, 0x14,        //   Unit (Degrees)
    0x75, 0x04,        //   Report Size (4)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x42,        //   Input (Data,Var,Abs,Null)

    // Reset unit
    0x65, 0x00,        //   Unit (None)
    0x45, 0x00,        //   Physical Maximum (0)

    // 12 buttons
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x0C,        //   Usage Maximum (Button 12)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x0C,        //   Report Count (12)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    0xC0,              // End Collection
};

// ============================================================================
// CONFIGURATION DESCRIPTOR
// ============================================================================

#define XAC_CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

static const uint8_t xac_config_descriptor[] = {
    // Configuration descriptor
    0x09,                           // bLength
    TUSB_DESC_CONFIGURATION,        // bDescriptorType
    U16_TO_U8S_LE(XAC_CONFIG_TOTAL_LEN), // wTotalLength
    0x01,                           // bNumInterfaces
    0x01,                           // bConfigurationValue
    0x00,                           // iConfiguration
    0xA0,                           // bmAttributes (Remote Wakeup)
    0x32,                           // bMaxPower (100mA)

    // Interface descriptor
    0x09,                           // bLength
    TUSB_DESC_INTERFACE,            // bDescriptorType
    0x00,                           // bInterfaceNumber
    0x00,                           // bAlternateSetting
    0x01,                           // bNumEndpoints
    TUSB_CLASS_HID,                 // bInterfaceClass
    0x00,                           // bInterfaceSubClass
    0x00,                           // bInterfaceProtocol
    0x00,                           // iInterface

    // HID descriptor
    0x09,                           // bLength
    HID_DESC_TYPE_HID,              // bDescriptorType
    U16_TO_U8S_LE(0x0111),          // bcdHID (1.11)
    0x00,                           // bCountryCode
    0x01,                           // bNumDescriptors
    HID_DESC_TYPE_REPORT,           // bDescriptorType[0]
    U16_TO_U8S_LE(sizeof(xac_report_descriptor)), // wDescriptorLength[0]

    // Endpoint descriptor (IN)
    0x07,                           // bLength
    TUSB_DESC_ENDPOINT,             // bDescriptorType
    0x81,                           // bEndpointAddress (EP1 IN)
    TUSB_XFER_INTERRUPT,            // bmAttributes
    U16_TO_U8S_LE(XAC_ENDPOINT_SIZE), // wMaxPacketSize
    0x01,                           // bInterval (1ms)
};

#endif // XAC_DESCRIPTORS_H
