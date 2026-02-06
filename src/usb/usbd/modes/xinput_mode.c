// xinput_mode.c - Xbox 360 XInput USB device mode
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "tusb.h"
#include "../usbd_mode.h"
#include "../usbd.h"
#include "../drivers/tud_xinput.h"
#include "descriptors/xinput_descriptors.h"
#include "core/buttons.h"
#include <string.h>

#if CFG_TUD_XINPUT

// ============================================================================
// STATE
// ============================================================================

static xinput_in_report_t xinput_report;
static xinput_out_report_t xinput_output;
static bool xinput_output_available = false;

// ============================================================================
// CONVERSION HELPERS
// ============================================================================

// Convert analog value from Joypad (0-255, center 128) to signed 16-bit
static int16_t convert_axis_to_s16(uint8_t value)
{
    int32_t scaled = ((int32_t)value - 128) * 256;
    if (scaled < -32768) scaled = -32768;
    if (scaled > 32767) scaled = 32767;
    return (int16_t)scaled;
}

// Convert and invert axis (for Y-axis where convention differs)
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

static void xinput_mode_init(void)
{
    memset(&xinput_report, 0, sizeof(xinput_in_report_t));
    xinput_report.report_id = 0x00;
    xinput_report.report_size = sizeof(xinput_in_report_t);
    memset(&xinput_output, 0, sizeof(xinput_out_report_t));
    xinput_output_available = false;

    // Initialize XSM3 auth for Xbox 360 console compatibility
    tud_xinput_xsm3_init();
}

static bool xinput_mode_is_ready(void)
{
    return tud_xinput_ready();
}

static bool xinput_mode_send_report(uint8_t player_index,
                                     const input_event_t* event,
                                     const profile_output_t* profile_out,
                                     uint32_t buttons)
{
    (void)player_index;
    (void)event;

    // Digital buttons byte 0 (DPAD, Start, Back, L3, R3)
    xinput_report.buttons0 = 0;
    if (buttons & JP_BUTTON_DU) xinput_report.buttons0 |= XINPUT_BTN_DPAD_UP;
    if (buttons & JP_BUTTON_DD) xinput_report.buttons0 |= XINPUT_BTN_DPAD_DOWN;
    if (buttons & JP_BUTTON_DL) xinput_report.buttons0 |= XINPUT_BTN_DPAD_LEFT;
    if (buttons & JP_BUTTON_DR) xinput_report.buttons0 |= XINPUT_BTN_DPAD_RIGHT;
    if (buttons & JP_BUTTON_S2) xinput_report.buttons0 |= XINPUT_BTN_START;
    if (buttons & JP_BUTTON_S1) xinput_report.buttons0 |= XINPUT_BTN_BACK;
    if (buttons & JP_BUTTON_L3) xinput_report.buttons0 |= XINPUT_BTN_L3;
    if (buttons & JP_BUTTON_R3) xinput_report.buttons0 |= XINPUT_BTN_R3;

    // Digital buttons byte 1 (LB, RB, Guide, A, B, X, Y)
    xinput_report.buttons1 = 0;
    if (buttons & JP_BUTTON_L1) xinput_report.buttons1 |= XINPUT_BTN_LB;
    if (buttons & JP_BUTTON_R1) xinput_report.buttons1 |= XINPUT_BTN_RB;
    if (buttons & JP_BUTTON_A1) xinput_report.buttons1 |= XINPUT_BTN_GUIDE;
    if (buttons & JP_BUTTON_B1) xinput_report.buttons1 |= XINPUT_BTN_A;
    if (buttons & JP_BUTTON_B2) xinput_report.buttons1 |= XINPUT_BTN_B;
    if (buttons & JP_BUTTON_B3) xinput_report.buttons1 |= XINPUT_BTN_X;
    if (buttons & JP_BUTTON_B4) xinput_report.buttons1 |= XINPUT_BTN_Y;

    // Analog triggers (0-255)
    xinput_report.trigger_l = profile_out->l2_analog;
    xinput_report.trigger_r = profile_out->r2_analog;
    if (xinput_report.trigger_l == 0 && (buttons & JP_BUTTON_L2)) xinput_report.trigger_l = 0xFF;
    if (xinput_report.trigger_r == 0 && (buttons & JP_BUTTON_R2)) xinput_report.trigger_r = 0xFF;

    // Analog sticks (signed 16-bit, -32768 to +32767)
    // Y-axis inverted: input 0=down, XInput convention positive=up
    xinput_report.stick_lx = convert_axis_to_s16(profile_out->left_x);
    xinput_report.stick_ly = convert_axis_to_s16_inverted(profile_out->left_y);
    xinput_report.stick_rx = convert_axis_to_s16(profile_out->right_x);
    xinput_report.stick_ry = convert_axis_to_s16_inverted(profile_out->right_y);

    return tud_xinput_send_report(&xinput_report);
}

static void xinput_mode_task(void)
{
    // Process XSM3 auth state machine (Xbox 360 console authentication)
    tud_xinput_xsm3_process();

    // Check for rumble output from host
    if (tud_xinput_get_output(&xinput_output)) {
        xinput_output_available = true;
    }
}

static uint8_t xinput_mode_get_rumble(void)
{
    // Return the stronger of the two motors
    return (xinput_output.rumble_l > xinput_output.rumble_r)
           ? xinput_output.rumble_l : xinput_output.rumble_r;
}

static bool xinput_mode_get_feedback(output_feedback_t* fb)
{
    if (!xinput_output_available) return false;

    fb->rumble_left = xinput_output.rumble_l;
    fb->rumble_right = xinput_output.rumble_r;
    fb->dirty = true;
    xinput_output_available = false;
    return true;
}

static const uint8_t* xinput_mode_get_device_descriptor(void)
{
    return (const uint8_t*)&xinput_device_descriptor;
}

static const uint8_t* xinput_mode_get_config_descriptor(void)
{
    return xinput_config_descriptor;
}

static const usbd_class_driver_t* xinput_mode_get_class_driver(void)
{
    return tud_xinput_class_driver();
}

// ============================================================================
// MODE EXPORT
// ============================================================================

const usbd_mode_t xinput_mode = {
    .name = "XInput",
    .mode = USB_OUTPUT_MODE_XINPUT,

    .get_device_descriptor = xinput_mode_get_device_descriptor,
    .get_config_descriptor = xinput_mode_get_config_descriptor,
    .get_report_descriptor = NULL,  // XInput doesn't use HID

    .init = xinput_mode_init,
    .send_report = xinput_mode_send_report,
    .is_ready = xinput_mode_is_ready,

    .handle_output = NULL,  // Output handled via tud_xinput_get_output
    .get_rumble = xinput_mode_get_rumble,
    .get_feedback = xinput_mode_get_feedback,
    .get_report = NULL,
    .get_class_driver = xinput_mode_get_class_driver,
    .task = xinput_mode_task,
};

#endif // CFG_TUD_XINPUT
