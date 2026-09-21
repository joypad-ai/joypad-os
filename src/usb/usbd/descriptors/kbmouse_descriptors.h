// kbmouse_descriptors.h - Keyboard + Mouse composite HID descriptors
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
// Composite HID device with keyboard and mouse functionality using report IDs

#ifndef KBMOUSE_DESCRIPTORS_H
#define KBMOUSE_DESCRIPTORS_H

#include <stdint.h>
#include "tusb.h"

// ============================================================================
// USB IDENTIFIERS
// ============================================================================

#define USB_KBMOUSE_VID          0x2563  // SHANWAN (same vendor as HID mode)
#define USB_KBMOUSE_PID          0x0576  // Keyboard/Mouse composite
#define USB_KBMOUSE_BCD          0x0100  // v1.0
#define USB_KBMOUSE_MANUFACTURER "Joypad"
#define USB_KBMOUSE_PRODUCT      "JoypadOS (KB/Mouse)"

// ============================================================================
// REPORT IDs
// ============================================================================

#define KBMOUSE_REPORT_ID_KEYBOARD  1
#define KBMOUSE_REPORT_ID_MOUSE     2

// ============================================================================
// DEVICE DESCRIPTOR
// ============================================================================

static const tusb_desc_device_t kbmouse_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,               // USB 2.0
    .bDeviceClass       = 0x00,                 // Per interface
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_KBMOUSE_VID,
    .idProduct          = USB_KBMOUSE_PID,
    .bcdDevice          = USB_KBMOUSE_BCD,
    .iManufacturer      = 1,
    .iProduct           = 2,
    .iSerialNumber      = 3,
    .bNumConfigurations = 1
};

// ============================================================================
// HID REPORT DESCRIPTOR
// ============================================================================
// Composite report descriptor with keyboard (ID 1) and mouse (ID 2)

static const uint8_t kbmouse_report_descriptor[] = {
    // Keyboard Report (ID 1)
    // Standard 6-key rollover keyboard
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, KBMOUSE_REPORT_ID_KEYBOARD, // Report ID (1)

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
    0x25, 0x65,        //   Logical Maximum (101) - Standard keyboard keys
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0x65,        //   Usage Maximum (101)
    0x81, 0x00,        //   Input (Data, Array)

    0xC0,              // End Collection (Keyboard)

    // Mouse Report (ID 2)
    // 5-button mouse with X, Y, wheel, and pan
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, KBMOUSE_REPORT_ID_MOUSE, // Report ID (2)
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

// ============================================================================
// CONFIGURATION DESCRIPTOR
// ============================================================================

// Interface numbers
enum {
    KBMOUSE_ITF_HID = 0,
    KBMOUSE_ITF_TOTAL
};

// Endpoint numbers
#define KBMOUSE_EPNUM_HID  0x81  // IN endpoint 1

// Configuration descriptor total length
#define KBMOUSE_CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)

static const uint8_t kbmouse_config_descriptor[] = {
    // Configuration descriptor
    TUD_CONFIG_DESCRIPTOR(1, KBMOUSE_ITF_TOTAL, 0, KBMOUSE_CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // HID interface with composite keyboard+mouse
    TUD_HID_DESCRIPTOR(KBMOUSE_ITF_HID, 0, HID_ITF_PROTOCOL_NONE,
                       sizeof(kbmouse_report_descriptor), KBMOUSE_EPNUM_HID,
                       CFG_TUD_HID_EP_BUFSIZE, 1),
};

#endif // KBMOUSE_DESCRIPTORS_H
