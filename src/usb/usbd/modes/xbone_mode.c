// xbone_mode.c - Xbox One USB device mode
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "tusb.h"
#include "../usbd_mode.h"
#include "../usbd.h"
#include "../drivers/tud_xbone.h"
#include "descriptors/xbone_descriptors.h"
#include "core/buttons.h"
#include <string.h>

// ============================================================================
// STATE
// ============================================================================

static gip_input_report_t xbone_report;

// Tracks Guide-bit edges so we can emit a separate GIP_VIRTUAL_KEYCODE
// packet on press and release. Console only opens the Guide overlay on
// receipt of that packet — the bit in the standard input report alone
// does nothing.
static bool prev_guide = false;

// ============================================================================
// CONVERSION HELPER
// ============================================================================

// Convert analog value from Joypad (0-255, center 128) to Xbox signed 16-bit
static int16_t convert_axis_to_s16(uint8_t value)
{
    int32_t scaled = ((int32_t)value - 128) * 256;
    if (scaled < -32768) scaled = -32768;
    if (scaled > 32767) scaled = 32767;
    return (int16_t)scaled;
}

// Convert and invert axis (for Y-axis where convention differs). Negating the
// converted value instead would turn -32768 (full travel) into -32768 again,
// the opposite direction, because +32768 does not fit in int16_t.
static int16_t convert_axis_to_s16_inverted(uint8_t value)
{
    int32_t scaled = -((int32_t)value - 128) * 256;
    if (scaled < -32768) scaled = -32768;
    if (scaled > 32767) scaled = 32767;
    return (int16_t)scaled;
}

// ============================================================================
// MODE INTERFACE IMPLEMENTATION
// ============================================================================

static void xbone_mode_init(void)
{
    memset(&xbone_report, 0, sizeof(gip_input_report_t));
}

static bool xbone_mode_is_ready(void)
{
    return xbone_is_powered_on() && tud_xbone_ready();
}

static bool xbone_mode_send_report(uint8_t player_index,
                                    const input_event_t* event,
                                    const profile_output_t* profile_out,
                                    uint32_t buttons)
{
    (void)player_index;
    (void)event;

    // Clear report
    memset(&xbone_report, 0, sizeof(gip_input_report_t));

    // Buttons
    xbone_report.a = (buttons & JP_BUTTON_B1) ? 1 : 0;
    xbone_report.b = (buttons & JP_BUTTON_B2) ? 1 : 0;
    xbone_report.x = (buttons & JP_BUTTON_B3) ? 1 : 0;
    xbone_report.y = (buttons & JP_BUTTON_B4) ? 1 : 0;

    xbone_report.left_shoulder = (buttons & JP_BUTTON_L1) ? 1 : 0;
    xbone_report.right_shoulder = (buttons & JP_BUTTON_R1) ? 1 : 0;

    xbone_report.back = (buttons & JP_BUTTON_S1) ? 1 : 0;
    xbone_report.start = (buttons & JP_BUTTON_S2) ? 1 : 0;

    xbone_report.guide = (buttons & JP_BUTTON_A1) ? 1 : 0;
    xbone_report.sync = (buttons & JP_BUTTON_A2) ? 1 : 0;

    // Edge-detect the Guide button so we can emit a paired virtual keycode
    // packet (press → release). Send before the input report so the console
    // sees the keycode on the same loop tick as the bit.
    bool now_guide = (buttons & JP_BUTTON_A1) != 0;
    if (now_guide != prev_guide) {
        tud_xbone_send_guide(now_guide);
        prev_guide = now_guide;
    }

    xbone_report.left_thumb = (buttons & JP_BUTTON_L3) ? 1 : 0;
    xbone_report.right_thumb = (buttons & JP_BUTTON_R3) ? 1 : 0;

    xbone_report.dpad_up = (buttons & JP_BUTTON_DU) ? 1 : 0;
    xbone_report.dpad_down = (buttons & JP_BUTTON_DD) ? 1 : 0;
    xbone_report.dpad_left = (buttons & JP_BUTTON_DL) ? 1 : 0;
    xbone_report.dpad_right = (buttons & JP_BUTTON_DR) ? 1 : 0;

    // Triggers (0-1023)
    // Map from profile analog (0-255) to Xbox One range (0-1023)
    xbone_report.left_trigger = (uint16_t)profile_out->l2_analog * 4;
    xbone_report.right_trigger = (uint16_t)profile_out->r2_analog * 4;

    // Fallback to digital if analog is 0 but button pressed
    if (xbone_report.left_trigger == 0 && (buttons & JP_BUTTON_L2))
        xbone_report.left_trigger = 1023;
    if (xbone_report.right_trigger == 0 && (buttons & JP_BUTTON_R2))
        xbone_report.right_trigger = 1023;

    // Analog sticks (signed 16-bit, -32768 to +32767)
    // Y-axis inverted: input 0=down, output positive=up
    xbone_report.left_stick_x = convert_axis_to_s16(profile_out->left_x);
    xbone_report.left_stick_y = convert_axis_to_s16_inverted(profile_out->left_y);
    xbone_report.right_stick_x = convert_axis_to_s16(profile_out->right_x);
    xbone_report.right_stick_y = convert_axis_to_s16_inverted(profile_out->right_y);

    return tud_xbone_send_report(&xbone_report);
}

static void xbone_mode_task(void)
{
    // Update Xbox One driver (handles GIP protocol state machine)
    tud_xbone_update();
}

static const usbd_class_driver_t* xbone_mode_get_class_driver(void)
{
    return tud_xbone_class_driver();
}

static const uint8_t* xbone_mode_get_device_descriptor(void)
{
    return (const uint8_t*)&xbone_device_descriptor;
}

static const uint8_t* xbone_mode_get_config_descriptor(void)
{
    return xbone_config_descriptor;
}

// ============================================================================
// MODE EXPORT
// ============================================================================

const usbd_mode_t xbone_mode = {
    .name = "Xbox One",
    .mode = USB_OUTPUT_MODE_XBONE,

    .get_device_descriptor = xbone_mode_get_device_descriptor,
    .get_config_descriptor = xbone_mode_get_config_descriptor,
    .get_report_descriptor = NULL,  // Xbox One uses GIP protocol, not HID

    .init = xbone_mode_init,
    .send_report = xbone_mode_send_report,
    .is_ready = xbone_mode_is_ready,

    // Xbox One rumble is handled via GIP protocol in tud_xbone driver
    .handle_output = NULL,
    .get_rumble = NULL,
    .get_feedback = NULL,
    .get_report = NULL,

    .get_class_driver = xbone_mode_get_class_driver,
    .task = xbone_mode_task,
};
