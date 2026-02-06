// tud_xinput.h - TinyUSB XInput class driver for Xbox 360
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Custom USB device class driver implementing Xbox 360 XInput protocol.
// XInput uses vendor class 0xFF, subclass 0x5D, protocol 0x01.
//
// Reference: GP2040-CE, OGX-Mini (MIT/BSD-3-Clause)

#ifndef TUD_XINPUT_H
#define TUD_XINPUT_H

#include <stdint.h>
#include <stdbool.h>
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "descriptors/xinput_descriptors.h"

// ============================================================================
// XINPUT CONFIGURATION
// ============================================================================

#ifndef CFG_TUD_XINPUT
#define CFG_TUD_XINPUT 0
#endif

#ifndef CFG_TUD_XINPUT_EP_BUFSIZE
#define CFG_TUD_XINPUT_EP_BUFSIZE 32
#endif

// ============================================================================
// XSM3 AUTH TYPES
// ============================================================================

// Xbox 360 security vendor requests (bRequest values)
typedef enum {
    XSM3_REQ_GET_SERIAL     = 0x81,  // IN:  Get controller serial/ID (29 bytes)
    XSM3_REQ_INIT_AUTH      = 0x82,  // OUT: Console sends challenge init (34 bytes)
    XSM3_REQ_RESPOND        = 0x83,  // IN:  Get challenge response (46 or 22 bytes)
    XSM3_REQ_KEEPALIVE      = 0x84,  // IN:  Keepalive (0 bytes)
    XSM3_REQ_STATE          = 0x86,  // IN:  Get auth state (2 bytes)
    XSM3_REQ_VERIFY         = 0x87,  // IN:  Console sends verify challenge (22 bytes)
} xsm3_request_t;

// Auth state machine
typedef enum {
    XSM3_AUTH_IDLE           = 0,   // Waiting for console
    XSM3_AUTH_INIT_RECEIVED  = 1,   // 0x82 received, processing
    XSM3_AUTH_RESPONDED      = 2,   // Challenge response ready
    XSM3_AUTH_VERIFY_RECEIVED = 3,  // 0x87 received, processing
    XSM3_AUTH_AUTHENTICATED  = 4,   // Auth complete
} xsm3_auth_state_t;

// Auth data packet sizes
#define XSM3_SERIAL_LEN         29  // 0x1D
#define XSM3_INIT_PACKET_LEN    34  // 0x22
#define XSM3_RESPONSE_INIT_LEN  46  // 0x2E (challenge_response for init: 0x30 trimmed)
#define XSM3_VERIFY_PACKET_LEN  22  // 0x16
#define XSM3_RESPONSE_VERIFY_LEN 22 // 0x16 (challenge_response for verify)

// ============================================================================
// XINPUT API
// ============================================================================

// Check if XInput device is ready to send a report
bool tud_xinput_ready(void);

// Send gamepad input report (20 bytes)
// Returns true if transfer was queued successfully
bool tud_xinput_send_report(const xinput_in_report_t* report);

// Get rumble/LED output report (8 bytes)
// Call this to retrieve the latest rumble/LED values from host
// Returns true if output data is available
bool tud_xinput_get_output(xinput_out_report_t* output);

// Initialize XSM3 authentication state
void tud_xinput_xsm3_init(void);

// Process pending XSM3 auth (call from mode task loop)
void tud_xinput_xsm3_process(void);

// ============================================================================
// CLASS DRIVER (internal)
// ============================================================================

// Get the XInput class driver for registration
const usbd_class_driver_t* tud_xinput_class_driver(void);

#endif // TUD_XINPUT_H
