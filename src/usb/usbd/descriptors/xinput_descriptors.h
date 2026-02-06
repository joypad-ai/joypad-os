// xinput_descriptors.h - XInput (Xbox 360) USB descriptors
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// XInput is Xbox 360's controller protocol. It uses vendor-specific USB class
// (0xFF/0x5D/0x01) with a proprietary descriptor type (0x21).
//
// Reference: GP2040-CE, OGX-Mini (MIT/BSD-3-Clause)

#ifndef XINPUT_DESCRIPTORS_H
#define XINPUT_DESCRIPTORS_H

#include <stdint.h>
#include "tusb.h"

// ============================================================================
// XINPUT USB IDENTIFIERS
// ============================================================================

#define XINPUT_VID              0x045E  // Microsoft
#define XINPUT_PID              0x028E  // Xbox 360 Controller
#define XINPUT_BCD_DEVICE       0x0114  // v1.14

// XInput Interface Class/Subclass/Protocol
#define XINPUT_INTERFACE_CLASS    0xFF
#define XINPUT_INTERFACE_SUBCLASS 0x5D
#define XINPUT_INTERFACE_PROTOCOL 0x01

// ============================================================================
// XINPUT BUTTON DEFINITIONS
// ============================================================================

// Buttons byte 0 (dpad + start/back + L3/R3)
#define XINPUT_BTN_DPAD_UP      (1U << 0)
#define XINPUT_BTN_DPAD_DOWN    (1U << 1)
#define XINPUT_BTN_DPAD_LEFT    (1U << 2)
#define XINPUT_BTN_DPAD_RIGHT   (1U << 3)
#define XINPUT_BTN_START        (1U << 4)
#define XINPUT_BTN_BACK         (1U << 5)
#define XINPUT_BTN_L3           (1U << 6)
#define XINPUT_BTN_R3           (1U << 7)

// Buttons byte 1 (bumpers + face buttons + guide)
#define XINPUT_BTN_LB           (1U << 0)
#define XINPUT_BTN_RB           (1U << 1)
#define XINPUT_BTN_GUIDE        (1U << 2)
// Bit 3 is unused
#define XINPUT_BTN_A            (1U << 4)
#define XINPUT_BTN_B            (1U << 5)
#define XINPUT_BTN_X            (1U << 6)
#define XINPUT_BTN_Y            (1U << 7)

// ============================================================================
// XINPUT REPORT STRUCTURES
// ============================================================================

// Input Report (gamepad state) - 20 bytes
typedef struct __attribute__((packed)) {
    uint8_t  report_id;      // Always 0x00
    uint8_t  report_size;    // Always 0x14 (20)
    uint8_t  buttons0;       // DPAD, Start, Back, L3, R3
    uint8_t  buttons1;       // LB, RB, Guide, A, B, X, Y
    uint8_t  trigger_l;      // Left trigger (0-255)
    uint8_t  trigger_r;      // Right trigger (0-255)
    int16_t  stick_lx;       // Left stick X (-32768 to 32767)
    int16_t  stick_ly;       // Left stick Y (-32768 to 32767)
    int16_t  stick_rx;       // Right stick X (-32768 to 32767)
    int16_t  stick_ry;       // Right stick Y (-32768 to 32767)
    uint8_t  reserved[6];    // Reserved/padding
} xinput_in_report_t;

_Static_assert(sizeof(xinput_in_report_t) == 20, "xinput_in_report_t must be 20 bytes");

// Output Report (rumble/LED) - 8 bytes
typedef struct __attribute__((packed)) {
    uint8_t  report_id;      // 0x00 = rumble, 0x01 = LED
    uint8_t  report_size;    // 0x08
    uint8_t  led;            // LED pattern (0x00 for rumble)
    uint8_t  rumble_l;       // Left motor (large, 0-255)
    uint8_t  rumble_r;       // Right motor (small, 0-255)
    uint8_t  reserved[3];    // Padding
} xinput_out_report_t;

_Static_assert(sizeof(xinput_out_report_t) == 8, "xinput_out_report_t must be 8 bytes");

// LED patterns for report_id 0x01
#define XINPUT_LED_OFF          0x00
#define XINPUT_LED_BLINK        0x01
#define XINPUT_LED_FLASH_1      0x02
#define XINPUT_LED_FLASH_2      0x03
#define XINPUT_LED_FLASH_3      0x04
#define XINPUT_LED_FLASH_4      0x05
#define XINPUT_LED_ON_1         0x06
#define XINPUT_LED_ON_2         0x07
#define XINPUT_LED_ON_3         0x08
#define XINPUT_LED_ON_4         0x09
#define XINPUT_LED_ROTATE       0x0A
#define XINPUT_LED_BLINK_SLOW   0x0B
#define XINPUT_LED_BLINK_SLOW_1 0x0C
#define XINPUT_LED_BLINK_SLOW_2 0x0D

// ============================================================================
// XINPUT USB DESCRIPTORS
// ============================================================================

// Device descriptor
static const tusb_desc_device_t xinput_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,  // USB 2.0
    .bDeviceClass       = 0xFF,    // Vendor Specific
    .bDeviceSubClass    = 0xFF,
    .bDeviceProtocol    = 0xFF,
    .bMaxPacketSize0    = 64,
    .idVendor           = XINPUT_VID,
    .idProduct          = XINPUT_PID,
    .bcdDevice          = XINPUT_BCD_DEVICE,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

// Xbox 360 security interface class/subclass/protocol
#define XINPUT_SEC_INTERFACE_CLASS     0xFF
#define XINPUT_SEC_INTERFACE_SUBCLASS  0xFD
#define XINPUT_SEC_INTERFACE_PROTOCOL  0x13

// XInput proprietary descriptor types
#define XINPUT_DESC_TYPE_VENDOR  0x21  // Gamepad/Audio/Plugin vendor descriptor
#define XINPUT_DESC_TYPE_SEC    0x41  // Security interface descriptor

// Full 4-interface Xbox 360 wired controller configuration descriptor (153 bytes)
// Matches a real Xbox 360 wired controller to pass console authentication.
// Reference: https://github.com/InvoxiPlayGames/libxsm3
//            https://github.com/OpenStickCommunity/GP2040-CE
#define XINPUT_CONFIG_TOTAL_LEN  153

static const uint8_t xinput_config_descriptor[] = {
    // Configuration descriptor (9 bytes)
    0x09, 0x02,                         // bLength, bDescriptorType
    U16_TO_U8S_LE(XINPUT_CONFIG_TOTAL_LEN), // wTotalLength (153)
    0x04,                               // bNumInterfaces
    0x01,                               // bConfigurationValue
    0x00,                               // iConfiguration
    0xA0,                               // bmAttributes (bus powered, remote wakeup)
    0xFA,                               // bMaxPower (500mA)

    // ---- Interface 0: Gamepad (SubClass 0x5D, Protocol 0x01) ----
    0x09, 0x04, 0x00, 0x00, 0x02, 0xFF, 0x5D, 0x01, 0x00,
    // Gamepad vendor descriptor (type 0x21, 17 bytes)
    0x11, 0x21, 0x00, 0x01, 0x01, 0x25, 0x81, 0x14,
    0x00, 0x00, 0x00, 0x00, 0x13, 0x02, 0x08, 0x00, 0x00,
    // EP 0x81 IN - Interrupt, 32 bytes, 4ms
    0x07, 0x05, 0x81, 0x03, 0x20, 0x00, 0x04,
    // EP 0x02 OUT - Interrupt, 32 bytes, 8ms
    0x07, 0x05, 0x02, 0x03, 0x20, 0x00, 0x08,

    // ---- Interface 1: Audio (SubClass 0x5D, Protocol 0x03) ----
    0x09, 0x04, 0x01, 0x00, 0x04, 0xFF, 0x5D, 0x03, 0x00,
    // Audio vendor descriptor (type 0x21, 27 bytes)
    0x1B, 0x21, 0x00, 0x01, 0x01, 0x01, 0x83, 0x40,
    0x01, 0x04, 0x20, 0x16, 0x85, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x16, 0x06, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00,
    // EP 0x83 IN - Interrupt, 32 bytes, 2ms
    0x07, 0x05, 0x83, 0x03, 0x20, 0x00, 0x02,
    // EP 0x04 OUT - Interrupt, 32 bytes, 4ms
    0x07, 0x05, 0x04, 0x03, 0x20, 0x00, 0x04,
    // EP 0x85 IN - Interrupt, 32 bytes, 64ms
    0x07, 0x05, 0x85, 0x03, 0x20, 0x00, 0x40,
    // EP 0x06 OUT - Interrupt, 32 bytes, 16ms
    0x07, 0x05, 0x06, 0x03, 0x20, 0x00, 0x10,

    // ---- Interface 2: Plugin Module (SubClass 0x5D, Protocol 0x02) ----
    0x09, 0x04, 0x02, 0x00, 0x01, 0xFF, 0x5D, 0x02, 0x00,
    // Plugin vendor descriptor (type 0x21, 9 bytes)
    0x09, 0x21, 0x00, 0x01, 0x01, 0x22, 0x86, 0x03, 0x00,
    // EP 0x86 IN - Interrupt, 32 bytes, 16ms
    0x07, 0x05, 0x86, 0x03, 0x20, 0x00, 0x10,

    // ---- Interface 3: Security (SubClass 0xFD, Protocol 0x13) ----
    // 0 endpoints, iInterface=4 (XSM3 security string)
    0x09, 0x04, 0x03, 0x00, 0x00, 0xFF, 0xFD, 0x13, 0x04,
    // Security descriptor (type 0x41, 6 bytes)
    0x06, 0x41, 0x00, 0x01, 0x01, 0x03,
};

_Static_assert(sizeof(xinput_config_descriptor) == XINPUT_CONFIG_TOTAL_LEN,
               "xinput_config_descriptor must be 153 bytes");

// XSM3 Security string for iInterface=4 (string descriptor index 4)
#define XINPUT_SECURITY_STRING  "Xbox Security Method 3, Version 1.00, " \
    "\xa9 2005 Microsoft Corporation. All rights reserved."

// String descriptors
#define XINPUT_MANUFACTURER  "Microsoft"
#define XINPUT_PRODUCT       "XInput STANDARD GAMEPAD"

#endif // XINPUT_DESCRIPTORS_H
