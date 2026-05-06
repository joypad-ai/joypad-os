// tud_xbone.h - Xbox One TinyUSB device driver
// SPDX-License-Identifier: MIT
// Based on GP2040-CE implementation (gp2040-ce.info)

#ifndef TUD_XBONE_H
#define TUD_XBONE_H

#include "tusb.h"
#include "device/usbd_pvt.h"
#include "../descriptors/xbone_descriptors.h"
#include "xgip_protocol.h"

// Xbox One driver state machine
typedef enum {
    XBONE_STATE_IDLE = 0,
    XBONE_STATE_READY_ANNOUNCE,
    XBONE_STATE_WAIT_DESCRIPTOR_REQUEST,
    XBONE_STATE_SEND_DESCRIPTOR,
    XBONE_STATE_SETUP_AUTH
} xbone_driver_state_t;

// Xbox One auth passthrough state
typedef enum {
    XBONE_AUTH_IDLE = 0,
    XBONE_AUTH_SEND_CONSOLE_TO_DONGLE,  // Forwarding console auth to dongle
    XBONE_AUTH_WAIT_CONSOLE_TO_DONGLE,  // Waiting for dongle response
    XBONE_AUTH_SEND_DONGLE_TO_CONSOLE,  // Sending dongle response to console
    XBONE_AUTH_WAIT_DONGLE_TO_CONSOLE   // Waiting for console ACK
} xbone_auth_state_t;

// Xbox One auth data
typedef struct {
    xbone_auth_state_t state;
    uint8_t buffer[XGIP_MAX_DATA_SIZE];
    uint16_t length;
    uint8_t sequence;
    uint8_t auth_type;
    bool auth_completed;
} xbone_auth_t;

// Get class driver for registration
const usbd_class_driver_t* tud_xbone_class_driver(void);

// Check if Xbox One driver is ready
bool tud_xbone_ready(void);

// Send input report to console
bool tud_xbone_send_report(gip_input_report_t* report);

// Send a GIP_VIRTUAL_KEYCODE press/release. Real Xbox controllers raise the
// Guide button this way (keycode 0x5B); the bit in the standard input report
// alone does NOT open the Guide overlay on console.
bool tud_xbone_send_virtual_keycode(uint8_t keycode, bool pressed);

// Convenience wrapper for the Guide button (keycode 0x5B).
bool tud_xbone_send_guide(bool pressed);

// Update driver (called from task loop)
void tud_xbone_update(void);

// Vendor control transfer callback (for Windows OS descriptors)
bool tud_xbone_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                       tusb_control_request_t const* request);

// ============================================================================
// AUTH PASSTHROUGH API
// ============================================================================

// Get auth state
xbone_auth_state_t xbone_auth_get_state(void);

// Set auth data (from dongle to send to console)
void xbone_auth_set_data(uint8_t* data, uint16_t len, uint8_t seq,
                         uint8_t type, xbone_auth_state_t new_state);

// Get auth buffer
uint8_t* xbone_auth_get_buffer(void);

// Get auth data length
uint16_t xbone_auth_get_length(void);

// Get auth sequence
uint8_t xbone_auth_get_sequence(void);

// Get auth type
uint8_t xbone_auth_get_type(void);

// Check if auth is completed
bool xbone_auth_is_completed(void);

// Set auth completed flag
void xbone_auth_set_completed(bool completed);

// Is Xbox One powered on?
bool xbone_is_powered_on(void);

#endif // TUD_XBONE_H
