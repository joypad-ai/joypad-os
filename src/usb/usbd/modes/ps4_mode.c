// ps4_mode.c - PlayStation 4 DualShock 4 USB device mode
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "tusb.h"
#include "../usbd_mode.h"
#include "../usbd.h"
#include "descriptors/ps4_descriptors.h"
#include "core/buttons.h"
#include <string.h>

#ifndef DISABLE_USB_HOST
#include "usb/usbh/hid/devices/vendors/sony/sony_ds4.h"
#endif

extern const unsigned char key_pem_start[] asm("_binary_key_pem_start");
extern const unsigned char key_pem_end[] asm("_binary_key_pem_end");
extern const unsigned char ps4_serial_start[] asm("_binary_serial_txt_start");
extern const unsigned char ps4_serial_end[] asm("_binary_serial_txt_end");
extern const unsigned char ps4_signature_start[] asm("_binary_sig_bin_start");
extern const unsigned char ps4_signature_end[] asm("_binary_sig_bin_end");

// 计算大小的宏
#define KEY_PEM_SIZE (key_pem_end - key_pem_start)
#define PS4_SERIAL_SIZE (ps4_serial_end - ps4_serial_start)
#define PS4_SIGNATURE_SIZE (ps4_signature_end - ps4_signature_start)

// ============================================================================
// STATE
// ============================================================================

// Using raw byte buffer approach to avoid struct bitfield packing issues
static uint8_t ps4_report_buffer[64];
static ps4_out_report_t ps4_output;
static bool ps4_output_available = false;
static uint8_t ps4_report_counter = 0;

// ============================================================================
// MODE INTERFACE IMPLEMENTATION
// ============================================================================

static void ps4_mode_init(void)
{
    // Initialize PS4 report to neutral state (raw buffer approach)
    memset(ps4_report_buffer, 0, sizeof(ps4_report_buffer));
    ps4_report_buffer[0] = 0x01;  // Report ID
    ps4_report_buffer[1] = 0x80;  // LX center
    ps4_report_buffer[2] = 0x80;  // LY center
    ps4_report_buffer[3] = 0x80;  // RX center
    ps4_report_buffer[4] = 0x80;  // RY center
    ps4_report_buffer[5] = PS4_HAT_NOTHING;  // D-pad neutral (0x0F), no buttons
    // Bytes 6-9: buttons and triggers already 0
    // Set touchpad fingers to unpressed
    ps4_report_buffer[35] = 0x80;  // touchpad p1 unpressed
    ps4_report_buffer[39] = 0x80;  // touchpad p2 unpressed
    memset(&ps4_output, 0, sizeof(ps4_out_report_t));
    ps4_report_counter = 0;
}

static bool ps4_mode_is_ready(void)
{
    return tud_hid_ready();
}

// Send PS4 report (PlayStation 4 DualShock 4 mode)
// Uses raw byte array approach to avoid struct bitfield packing issues
//
// PS4 Report Layout (64 bytes):
//   Byte 0:    Report ID (0x01)
//   Byte 1:    Left stick X (0x00-0xFF, 0x80 center)
//   Byte 2:    Left stick Y (0x00-0xFF, 0x80 center)
//   Byte 3:    Right stick X (0x00-0xFF, 0x80 center)
//   Byte 4:    Right stick Y (0x00-0xFF, 0x80 center)
//   Byte 5:    D-pad (bits 0-3) + Square/Cross/Circle/Triangle (bits 4-7)
//   Byte 6:    L1/R1/L2/R2/Share/Options/L3/R3 (bits 0-7)
//   Byte 7:    PS (bit 0) + Touchpad (bit 1) + Counter (bits 2-7)
//   Byte 8:    Left trigger analog (0x00-0xFF)
//   Byte 9:    Right trigger analog (0x00-0xFF)
//   Bytes 10-63: Timestamp, sensor data, touchpad data, padding
static bool ps4_mode_send_report(uint8_t player_index,
                                  const input_event_t* event,
                                  const profile_output_t* profile_out,
                                  uint32_t buttons)
{
    (void)player_index;
    (void)event;

    // Byte 0: Report ID
    ps4_report_buffer[0] = 0x01;

    // Bytes 1-4: Analog sticks (HID convention: 0=up, 255=down - no inversion needed)
    ps4_report_buffer[1] = profile_out->left_x;          // LX
    ps4_report_buffer[2] = profile_out->left_y;          // LY
    ps4_report_buffer[3] = profile_out->right_x;         // RX
    ps4_report_buffer[4] = profile_out->right_y;         // RY

    // Byte 5: D-pad (bits 0-3) + face buttons (bits 4-7)
    uint8_t up = (buttons & JP_BUTTON_DU) ? 1 : 0;
    uint8_t down = (buttons & JP_BUTTON_DD) ? 1 : 0;
    uint8_t left = (buttons & JP_BUTTON_DL) ? 1 : 0;
    uint8_t right = (buttons & JP_BUTTON_DR) ? 1 : 0;

    uint8_t dpad;
    if (up && right)        dpad = PS4_HAT_UP_RIGHT;
    else if (up && left)    dpad = PS4_HAT_UP_LEFT;
    else if (down && right) dpad = PS4_HAT_DOWN_RIGHT;
    else if (down && left)  dpad = PS4_HAT_DOWN_LEFT;
    else if (up)            dpad = PS4_HAT_UP;
    else if (down)          dpad = PS4_HAT_DOWN;
    else if (left)          dpad = PS4_HAT_LEFT;
    else if (right)         dpad = PS4_HAT_RIGHT;
    else                    dpad = PS4_HAT_NOTHING;

    uint8_t face_buttons = 0;
    if (buttons & JP_BUTTON_B3) face_buttons |= 0x10;  // Square
    if (buttons & JP_BUTTON_B1) face_buttons |= 0x20;  // Cross
    if (buttons & JP_BUTTON_B2) face_buttons |= 0x40;  // Circle
    if (buttons & JP_BUTTON_B4) face_buttons |= 0x80;  // Triangle

    ps4_report_buffer[5] = dpad | face_buttons;

    // Byte 6: Shoulder buttons + other buttons
    uint8_t byte6 = 0;
    if (buttons & JP_BUTTON_L1) byte6 |= 0x01;  // L1
    if (buttons & JP_BUTTON_R1) byte6 |= 0x02;  // R1
    if (buttons & JP_BUTTON_L2) byte6 |= 0x04;  // L2 (digital)
    if (buttons & JP_BUTTON_R2) byte6 |= 0x08;  // R2 (digital)
    if (buttons & JP_BUTTON_S1) byte6 |= 0x10;  // Share
    if (buttons & JP_BUTTON_S2) byte6 |= 0x20;  // Options
    if (buttons & JP_BUTTON_L3) byte6 |= 0x40;  // L3
    if (buttons & JP_BUTTON_R3) byte6 |= 0x80;  // R3
    ps4_report_buffer[6] = byte6;

    // Byte 7: PS + Touchpad + Counter (6-bit)
    uint8_t byte7 = 0;
    if (buttons & JP_BUTTON_A1) byte7 |= 0x01;  // PS button
    if (buttons & JP_BUTTON_A2) byte7 |= 0x02;  // Touchpad click
    byte7 |= ((ps4_report_counter++ & 0x3F) << 2);       // Counter in bits 2-7
    ps4_report_buffer[7] = byte7;

    // Bytes 8-9: Analog triggers
    ps4_report_buffer[8] = profile_out->l2_analog;  // Left trigger
    ps4_report_buffer[9] = profile_out->r2_analog;  // Right trigger

    // Bytes 10-11: Timestamp (we can just increment)
    // Bytes 12-63: Leave as initialized (sensor data, touchpad, padding)

    // Send with report_id=0x01, letting TinyUSB prepend it
    // Skip byte 0 of buffer (our report_id) and send 63 bytes of data
    return tud_hid_report(0x01, &ps4_report_buffer[1], 63);
}

static void ps4_mode_handle_output(uint8_t report_id, const uint8_t* data, uint16_t len)
{
    // PS4 output report (rumble/LED) - Report ID 5
    if (report_id == PS4_REPORT_ID_OUTPUT && len >= sizeof(ps4_out_report_t)) {
        memcpy(&ps4_output, data, sizeof(ps4_out_report_t));
        ps4_output_available = true;
    }

    // PS4 auth feature reports (set)
#ifndef DISABLE_USB_HOST
    // Note: Feature reports are typically handled via tud_hid_set_report_cb
    // This handle_output is for interrupt OUT endpoint reports
#endif
}

static uint8_t ps4_mode_get_rumble(void)
{
    // PS4 has motor_left (large) and motor_right (small) 8-bit values
    return (ps4_output.motor_left > ps4_output.motor_right)
           ? ps4_output.motor_left : ps4_output.motor_right;
}

static bool ps4_mode_get_feedback(output_feedback_t* fb)
{
    if (!ps4_output_available) return false;

    // PS4 has two 8-bit motors and RGB lightbar
    fb->rumble_left = ps4_output.motor_left;
    fb->rumble_right = ps4_output.motor_right;
    fb->led_r = ps4_output.lightbar_red;
    fb->led_g = ps4_output.lightbar_green;
    fb->led_b = ps4_output.lightbar_blue;

    fb->dirty = true;
    ps4_output_available = false;
    return true;
}

static uint16_t ps4_mode_get_report(uint8_t report_id, hid_report_type_t report_type,
                                     uint8_t* buffer, uint16_t reqlen)
{
    if (report_type != HID_REPORT_TYPE_FEATURE) {
        return 0;
    }

    uint16_t len = 0;
    switch (report_id) {
        case PS4_REPORT_ID_FEATURE_03:
            // Controller definition report - return GP2040-CE compatible data
            len = sizeof(ps4_feature_03);
            if (reqlen < len) len = reqlen;
            memcpy(buffer, ps4_feature_03, len);
            return len;

        case PS4_REPORT_ID_AUTH_RESPONSE:   // 0xF1 - Signature from DS4
            // Get next signature page from DS4 passthrough (auto-incrementing)
            len = 64;
            if (reqlen < len) len = reqlen;
#ifndef DISABLE_USB_HOST
            if (ds4_auth_is_available()) {
                return ds4_auth_get_next_signature(buffer, len);
            }
#endif
            memset(buffer, 0, len);
            return len;

        case PS4_REPORT_ID_AUTH_STATUS:     // 0xF2 - Signing status
            // Get auth status from DS4 passthrough
            len = 16;
            if (reqlen < len) len = reqlen;
#ifndef DISABLE_USB_HOST
            if (ds4_auth_is_available()) {
                return ds4_auth_get_status(buffer, len);
            }
#endif
            // Return "signing" status if no DS4 available
            memset(buffer, 0, len);
            buffer[1] = 0x10;  // 16 = signing/not ready
            return len;

        case PS4_REPORT_ID_AUTH_PAYLOAD:    // 0xF0 - handled in set_report
            len = 64;
            if (reqlen < len) len = reqlen;
            memset(buffer, 0, len);
            return len;

        case PS4_REPORT_ID_AUTH_RESET:      // 0xF3 - Return page size info
#ifndef DISABLE_USB_HOST
            // Reset auth state when console requests 0xF3 (per hid-remapper)
            // This ensures signature_ready is false for new auth cycle
            ds4_auth_reset();
#endif
            len = sizeof(ps4_feature_f3);
            if (reqlen < len) len = reqlen;
            memcpy(buffer, ps4_feature_f3, len);
            return len;

        default:
            return 0;
    }
}

// Handle PS4 auth SET_REPORT (nonce from console, etc.)
// This is called from usbd.c's tud_hid_set_report_cb for feature reports
void ps4_mode_set_feature_report(uint8_t report_id, const uint8_t* buffer, uint16_t bufsize)
{
#ifndef DISABLE_USB_HOST
    switch (report_id) {
        case PS4_REPORT_ID_AUTH_PAYLOAD:    // 0xF0 - Nonce from console
            // Forward nonce to connected DS4
            if (ds4_auth_is_available()) {
                ds4_auth_send_nonce(buffer, bufsize);
            }
            break;

        case PS4_REPORT_ID_AUTH_RESET:      // 0xF3 - Reset auth
            ds4_auth_reset();
            break;

        default:
            break;
    }
#else
    (void)report_id;
    (void)buffer;
    (void)bufsize;
#endif
}

static const uint8_t* ps4_mode_get_device_descriptor(void)
{
    return (const uint8_t*)&ps4_device_descriptor;
}

static const uint8_t* ps4_mode_get_config_descriptor(void)
{
    return ps4_config_descriptor;
}

static const uint8_t* ps4_mode_get_report_descriptor(void)
{
    return ps4_report_descriptor;
}

// ============================================================================
// MODE EXPORT
// ============================================================================

const usbd_mode_t ps4_mode = {
    .name = "PS4",
    .mode = USB_OUTPUT_MODE_PS4,

    .get_device_descriptor = ps4_mode_get_device_descriptor,
    .get_config_descriptor = ps4_mode_get_config_descriptor,
    .get_report_descriptor = ps4_mode_get_report_descriptor,

    .init = ps4_mode_init,
    .send_report = ps4_mode_send_report,
    .is_ready = ps4_mode_is_ready,

    .handle_output = ps4_mode_handle_output,
    .get_rumble = ps4_mode_get_rumble,
    .get_feedback = ps4_mode_get_feedback,
    .get_report = ps4_mode_get_report,
    .get_class_driver = NULL,
    .task = NULL,
};
