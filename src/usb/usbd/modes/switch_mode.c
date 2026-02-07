// switch_mode.c - Nintendo Switch USB device mode
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "tusb.h"
#include "../usbd_mode.h"
#include "../usbd.h"
#include "descriptors/switch_descriptors.h"
#include "core/buttons.h"
#include <string.h>

// ============================================================================
// STATE
// ============================================================================

static switch_in_report_t switch_report;

// ============================================================================
// CONVERSION HELPERS
// ============================================================================

// Convert Joypad dpad to HID hat switch
static uint8_t convert_dpad_to_hat(uint32_t buttons)
{
    uint8_t up = (buttons & JP_BUTTON_DU) ? 1 : 0;
    uint8_t down = (buttons & JP_BUTTON_DD) ? 1 : 0;
    uint8_t left = (buttons & JP_BUTTON_DL) ? 1 : 0;
    uint8_t right = (buttons & JP_BUTTON_DR) ? 1 : 0;

    if (up && right) return SWITCH_HAT_UP_RIGHT;
    if (up && left) return SWITCH_HAT_UP_LEFT;
    if (down && right) return SWITCH_HAT_DOWN_RIGHT;
    if (down && left) return SWITCH_HAT_DOWN_LEFT;
    if (up) return SWITCH_HAT_UP;
    if (down) return SWITCH_HAT_DOWN;
    if (left) return SWITCH_HAT_LEFT;
    if (right) return SWITCH_HAT_RIGHT;

    return SWITCH_HAT_CENTER;
}

// ============================================================================
// MODE INTERFACE IMPLEMENTATION
// ============================================================================

static void switch_mode_init(void)
{
    memset(&switch_report, 0, sizeof(switch_in_report_t));
    switch_report.hat = SWITCH_HAT_CENTER;
    switch_report.lx = SWITCH_JOYSTICK_MID;
    switch_report.ly = SWITCH_JOYSTICK_MID;
    switch_report.rx = SWITCH_JOYSTICK_MID;
    switch_report.ry = SWITCH_JOYSTICK_MID;
}

static bool switch_mode_is_ready(void)
{
    return tud_hid_ready();
}

static bool switch_mode_send_report(uint8_t player_index,
                                     const input_event_t* event,
                                     const profile_output_t* profile_out,
                                     uint32_t buttons)
{
    (void)player_index;
    (void)event;

    // Buttons (16-bit) - position-based mapping (matches GP2040-CE)
    switch_report.buttons = 0;
    if (buttons & JP_BUTTON_B1) switch_report.buttons |= SWITCH_MASK_B;     // B1 (bottom) -> B
    if (buttons & JP_BUTTON_B2) switch_report.buttons |= SWITCH_MASK_A;     // B2 (right)  -> A
    if (buttons & JP_BUTTON_B3) switch_report.buttons |= SWITCH_MASK_Y;     // B3 (left)   -> Y
    if (buttons & JP_BUTTON_B4) switch_report.buttons |= SWITCH_MASK_X;     // B4 (top)    -> X
    if (buttons & JP_BUTTON_L1) switch_report.buttons |= SWITCH_MASK_L;     // L
    if (buttons & JP_BUTTON_R1) switch_report.buttons |= SWITCH_MASK_R;     // R
    
    // --- SPECIAL POKKEN : ZL et ZR ---
    // On active si le bouton numérique est là... OU si la gâchette analogique est pressée (>30)
    if ((buttons & JP_BUTTON_L2) || (profile_out->l2_analog > 30)) {
        switch_report.buttons |= SWITCH_MASK_ZL;    // ZL
    }
    if ((buttons & JP_BUTTON_R2) || (profile_out->r2_analog > 30)) {
        switch_report.buttons |= SWITCH_MASK_ZR;    // ZR
    }

    if (buttons & JP_BUTTON_S1) switch_report.buttons |= SWITCH_MASK_MINUS; // Minus
    if (buttons & JP_BUTTON_S2) switch_report.buttons |= SWITCH_MASK_PLUS;  // Plus
    if (buttons & JP_BUTTON_L3) switch_report.buttons |= SWITCH_MASK_L3;
    if (buttons & JP_BUTTON_R3) switch_report.buttons |= SWITCH_MASK_R3;
    if (buttons & JP_BUTTON_A1) switch_report.buttons |= SWITCH_MASK_HOME;
    if (buttons & JP_BUTTON_A2) switch_report.buttons |= SWITCH_MASK_CAPTURE;

    // D-pad as hat switch
    switch_report.hat = convert_dpad_to_hat(buttons);

    // Analog sticks (HID convention: 0=up, 255=down - no inversion needed)
    switch_report.lx = profile_out->left_x;
    switch_report.ly = profile_out->left_y;
    switch_report.rx = profile_out->right_x;
    switch_report.ry = profile_out->right_y;

    switch_report.vendor = 0;

    return tud_hid_report(0, &switch_report, sizeof(switch_report));
}

static const uint8_t* switch_mode_get_device_descriptor(void)
{
    return (const uint8_t*)&switch_device_descriptor;
}

static const uint8_t* switch_mode_get_config_descriptor(void)
{
    return switch_config_descriptor;
}

static const uint8_t* switch_mode_get_report_descriptor(void)
{
    return switch_report_descriptor;
}

// ============================================================================
// MODE EXPORT
// ============================================================================

const usbd_mode_t switch_mode = {
    .name = "Switch",
    .mode = USB_OUTPUT_MODE_SWITCH,

    .get_device_descriptor = switch_mode_get_device_descriptor,
    .get_config_descriptor = switch_mode_get_config_descriptor,
    .get_report_descriptor = switch_mode_get_report_descriptor,

    .init = switch_mode_init,
    .send_report = switch_mode_send_report,
    .is_ready = switch_mode_is_ready,

    // No feedback support for Switch mode (basic HID)
    .handle_output = NULL,
    .get_rumble = NULL,
    .get_feedback = NULL,
    .get_report = NULL,
    .get_class_driver = NULL,
    .task = NULL,
};
