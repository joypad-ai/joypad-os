// xbone_descriptors.h - Xbox One USB device descriptors
// SPDX-License-Identifier: MIT
// Based on GP2040-CE implementation (gp2040-ce.info)
//
// Xbox One uses a vendor-specific class (0xFF) with GIP protocol.
// Subclass 0x47, Protocol 0xD0 indicates Xbox One controller.

#ifndef XBONE_DESCRIPTORS_H
#define XBONE_DESCRIPTORS_H

#include "tusb.h"
#include <stdint.h>

// Endpoint size for Xbox One
#define XBONE_ENDPOINT_SIZE 64

// Xbox One VID/PID (SuperPDP Gamepad - commonly used for third-party)
#define XBONE_VID           0x0E6F
#define XBONE_PID           0x02A4
#define XBONE_BCD           0x0101  // Device version 1.01

// String descriptors
#define XBONE_MANUFACTURER  "Joypad"
#define XBONE_PRODUCT       "Joypad (Xbox One)"

// GIP Protocol Commands
typedef enum {
    GIP_ACK_RESPONSE             = 0x01,  // Acknowledge
    GIP_ANNOUNCE                 = 0x02,  // Controller announcement
    GIP_KEEPALIVE                = 0x03,  // Keep-alive ping
    GIP_DEVICE_DESCRIPTOR        = 0x04,  // Device descriptor request/response
    GIP_POWER_MODE_DEVICE_CONFIG = 0x05,  // Power mode configuration
    GIP_AUTH                     = 0x06,  // Authentication
    GIP_VIRTUAL_KEYCODE          = 0x07,  // Guide button pressed
    GIP_CMD_RUMBLE               = 0x09,  // Rumble command
    GIP_CMD_WAKEUP               = 0x0A,  // Wake-up command
    GIP_FINAL_AUTH               = 0x1E,  // Final authentication
    GIP_INPUT_REPORT             = 0x20,  // Input report
    GIP_HID_REPORT               = 0x21,  // HID report
} gip_command_t;

// Maximum chunk size for GIP chunked transfers
#define GIP_MAX_CHUNK_SIZE 0x3A  // 58 bytes

// GIP Protocol Header (4 bytes)
typedef struct TU_ATTR_PACKED {
    uint8_t command;
    uint8_t client     : 4;  // Client ID
    uint8_t needs_ack  : 1;  // Requires acknowledgement
    uint8_t internal   : 1;  // Internal command
    uint8_t chunk_start: 1;  // Start of chunked data
    uint8_t chunked    : 1;  // Data is chunked
    uint8_t sequence;        // Sequence number
    uint8_t length;          // Data length (or chunk info)
} gip_header_t;

// GIP Input Report (follows header)
typedef struct TU_ATTR_PACKED {
    gip_header_t header;

    uint8_t sync            : 1;
    uint8_t guide           : 1;
    uint8_t start           : 1;  // Menu
    uint8_t back            : 1;  // View

    uint8_t a               : 1;
    uint8_t b               : 1;
    uint8_t x               : 1;
    uint8_t y               : 1;

    uint8_t dpad_up         : 1;
    uint8_t dpad_down       : 1;
    uint8_t dpad_left       : 1;
    uint8_t dpad_right      : 1;

    uint8_t left_shoulder   : 1;
    uint8_t right_shoulder  : 1;
    uint8_t left_thumb      : 1;
    uint8_t right_thumb     : 1;

    uint16_t left_trigger;   // 0-1023
    uint16_t right_trigger;  // 0-1023

    int16_t left_stick_x;    // -32768 to 32767
    int16_t left_stick_y;
    int16_t right_stick_x;
    int16_t right_stick_y;
} gip_input_report_t;

// Xbox One Device Descriptor
static const tusb_desc_device_t xbone_device_descriptor = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,  // USB 2.0
    .bDeviceClass       = 0xFF,    // Vendor-specific
    .bDeviceSubClass    = 0xFF,
    .bDeviceProtocol    = 0xFF,
    .bMaxPacketSize0    = 64,
    .idVendor           = XBONE_VID,
    .idProduct          = XBONE_PID,
    .bcdDevice          = XBONE_BCD,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

// Xbox One Configuration Descriptor
// Single interface with vendor class 0xFF, subclass 0x47, protocol 0xD0
// Two interrupt endpoints (IN and OUT)
static const uint8_t xbone_config_descriptor[] = {
    // Configuration Descriptor
    0x09,        // bLength
    0x02,        // bDescriptorType (Configuration)
    0x20, 0x00,  // wTotalLength = 32 bytes
    0x01,        // bNumInterfaces = 1
    0x01,        // bConfigurationValue
    0x00,        // iConfiguration (String Index)
    0xA0,        // bmAttributes (Bus Powered, Remote Wakeup)
    0xFA,        // bMaxPower = 500mA

    // Interface Descriptor
    0x09,        // bLength
    0x04,        // bDescriptorType (Interface)
    0x00,        // bInterfaceNumber = 0
    0x00,        // bAlternateSetting
    0x02,        // bNumEndpoints = 2
    0xFF,        // bInterfaceClass (Vendor Specific)
    0x47,        // bInterfaceSubClass (Xbox One)
    0xD0,        // bInterfaceProtocol (Xbox One)
    0x00,        // iInterface (String Index)

    // Endpoint Descriptor (IN)
    0x07,        // bLength
    0x05,        // bDescriptorType (Endpoint)
    0x81,        // bEndpointAddress (IN, Endpoint 1)
    0x03,        // bmAttributes (Interrupt)
    0x40, 0x00,  // wMaxPacketSize = 64
    0x01,        // bInterval = 1ms

    // Endpoint Descriptor (OUT)
    0x07,        // bLength
    0x05,        // bDescriptorType (Endpoint)
    0x02,        // bEndpointAddress (OUT, Endpoint 2)
    0x03,        // bmAttributes (Interrupt)
    0x40, 0x00,  // wMaxPacketSize = 64
    0x01,        // bInterval = 1ms
};

// Xbox One Announce Packet (sent after USB enumeration)
// This tells the console "I am an Xbox One controller"
static const uint8_t xbone_announce_packet[] = {
    0x00, 0x2a, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00,
    0xdf, 0x33, 0x14, 0x00, 0x01, 0x00, 0x01, 0x00,
    0x17, 0x01, 0x02, 0x00, 0x01, 0x00, 0x01, 0x00,
    0x01, 0x00, 0x01, 0x00
};

// Xbox One Device Descriptor (GIP Descriptor, not USB descriptor)
// Sent in response to GIP_DEVICE_DESCRIPTOR command
static const uint8_t xbone_gip_descriptor[] = {
    0x10, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xCA, 0x00,
    0x8B, 0x00, 0x16, 0x00, 0x1F, 0x00, 0x20, 0x00,
    0x27, 0x00, 0x2D, 0x00, 0x4A, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x01,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0x06, 0x01, 0x02, 0x03, 0x04, 0x06, 0x07, 0x05,
    0x01, 0x04, 0x05, 0x06, 0x0A, 0x01, 0x1A, 0x00,
    0x57, 0x69, 0x6E, 0x64, 0x6F, 0x77, 0x73, 0x2E,
    0x58, 0x62, 0x6F, 0x78, 0x2E, 0x49, 0x6E, 0x70,
    0x75, 0x74, 0x2E, 0x47, 0x61, 0x6D, 0x65, 0x70,
    0x61, 0x64, 0x04, 0x56, 0xFF, 0x76, 0x97, 0xFD,
    0x9B, 0x81, 0x45, 0xAD, 0x45, 0xB6, 0x45, 0xBB,
    0xA5, 0x26, 0xD6, 0x2C, 0x40, 0x2E, 0x08, 0xDF,
    0x07, 0xE1, 0x45, 0xA5, 0xAB, 0xA3, 0x12, 0x7A,
    0xF1, 0x97, 0xB5, 0xE7, 0x1F, 0xF3, 0xB8, 0x86,
    0x73, 0xE9, 0x40, 0xA9, 0xF8, 0x2F, 0x21, 0x26,
    0x3A, 0xCF, 0xB7, 0xFE, 0xD2, 0xDD, 0xEC, 0x87,
    0xD3, 0x94, 0x42, 0xBD, 0x96, 0x1A, 0x71, 0x2E,
    0x3D, 0xC7, 0x7D, 0x02, 0x17, 0x00, 0x20, 0x20,
    0x00, 0x01, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x17, 0x00, 0x09, 0x3C, 0x00,
    0x01, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00
};

// Windows OS Descriptor (for XGIP10 compatibility)
typedef struct TU_ATTR_PACKED {
    uint32_t total_length;
    uint16_t version;
    uint16_t index;
    uint8_t  total_sections;
    uint8_t  reserved[7];
    uint8_t  first_interface_number;
    uint8_t  reserved2;
    uint8_t  compatible_id[8];
    uint8_t  sub_compatible_id[8];
    uint8_t  reserved3[6];
} os_compatible_id_descriptor_t;

static const os_compatible_id_descriptor_t xbone_os_compat_descriptor = {
    .total_length = sizeof(os_compatible_id_descriptor_t),
    .version = 0x0100,
    .index = 0x0004,  // Extended compatible ID descriptor
    .total_sections = 1,
    .reserved = {0},
    .first_interface_number = 0,
    .reserved2 = 0x01,
    .compatible_id = {'X', 'G', 'I', 'P', '1', '0', 0, 0},
    .sub_compatible_id = {0},
    .reserved3 = {0}
};

// Xbox Security Method string (string index 4)
static const char xbone_security_method[] =
    "Xbox Security Method 3, Version 1.00, \xa9 2005 Microsoft Corporation. All rights reserved.";

// Microsoft OS String Descriptor (string index 0xEE)
static const uint8_t xbone_ms_os_descriptor[] = "MSFT100\x20\x00";

#endif // XBONE_DESCRIPTORS_H
