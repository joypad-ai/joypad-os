// pcemini_mode.c - PC Engine Mini USB device mode
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "tusb.h"
#include "../usbd_mode.h"
#include "../usbd.h"
#include "descriptors/pcemini_descriptors.h"
#include "core/buttons.h"
#include <string.h>
#include "platform/platform.h"

// ============================================================================
// STATE
// ============================================================================

static pcemini_in_report_t pcemini_report;

// Last state for turbo resend
static uint32_t last_buttons;
static uint8_t  last_lx;
static uint8_t  last_ly;

// Turbo state: track press start time per button
static uint32_t turbo_b3_start;
static bool     turbo_b3_held;
static uint32_t turbo_b4_start;
static bool     turbo_b4_held;

// Turbo speed: 3 levels matching 8BitDo PCE 2.4G convention
// Toggle periods: 50ms (10 Hz), 33ms (15 Hz), 25ms (20 Hz)
#define TURBO_SPEED_COUNT 3
static const uint32_t turbo_periods[TURBO_SPEED_COUNT] = { 50, 33, 25 };
static uint8_t turbo_speed_index;  // 0=10Hz, 1=15Hz, 2=20Hz

// L1/R1 edge detection for speed toggle
static bool l1_prev;
static bool r1_prev;

// ============================================================================
// MODE INTERFACE IMPLEMENTATION
// ============================================================================

static void pcemini_mode_init(void)
{
    pcemini_init_report(&pcemini_report);
    last_buttons = 0;
    last_lx = 128;
    last_ly = 128;
    turbo_b3_held = false;
    turbo_b4_held = false;
    turbo_speed_index = 1;  // Default: 15 Hz (middle speed)
    l1_prev = false;
    r1_prev = false;
}

static bool pcemini_mode_is_ready(void)
{
    return tud_hid_ready();
}

// Left stick deadzone threshold (0-255 range, 128=center)
#define PCEMINI_STICK_DEADZONE 64

// Build report from buttons and send
static bool pcemini_build_and_send(uint32_t buttons, uint8_t lx, uint8_t ly)
{
    pcemini_report.buttons = 0;

    uint32_t now = platform_time_ms();
    uint32_t period = turbo_periods[turbo_speed_index];

    // L1/R1 toggle turbo speed (edge-triggered)
    bool l1_now = (buttons & JP_BUTTON_L1) != 0;
    bool r1_now = (buttons & JP_BUTTON_R1) != 0;
    if (l1_now && !l1_prev && turbo_speed_index > 0) {
        turbo_speed_index--;
    }
    if (r1_now && !r1_prev && turbo_speed_index < TURBO_SPEED_COUNT - 1) {
        turbo_speed_index++;
    }
    l1_prev = l1_now;
    r1_prev = r1_now;

    // Turbo B3 → II: first phase always ON, then alternate at current speed
    if (buttons & JP_BUTTON_B3) {
        if (!turbo_b3_held) { turbo_b3_start = now; turbo_b3_held = true; }
        if (((now - turbo_b3_start) / period) % 2 == 0)
            pcemini_report.buttons |= PCEMINI_MASK_II;
    } else {
        turbo_b3_held = false;
    }

    // Turbo B4 → I: first phase always ON, then alternate at current speed
    if (buttons & JP_BUTTON_B4) {
        if (!turbo_b4_held) { turbo_b4_start = now; turbo_b4_held = true; }
        if (((now - turbo_b4_start) / period) % 2 == 0)
            pcemini_report.buttons |= PCEMINI_MASK_I;
    } else {
        turbo_b4_held = false;
    }

    // Face buttons
    // PCE II = south face button (JP_BUTTON_B1), PCE I = east face button (JP_BUTTON_B2)
    pcemini_report.buttons |=
          (buttons & JP_BUTTON_B1 ? PCEMINI_MASK_II     : 0)
        | (buttons & JP_BUTTON_B2 ? PCEMINI_MASK_I      : 0)
        | (buttons & JP_BUTTON_S1 ? PCEMINI_MASK_SELECT : 0)
        | (buttons & JP_BUTTON_S2 ? PCEMINI_MASK_RUN    : 0)
        | (buttons & JP_BUTTON_A1 ? (PCEMINI_MASK_SELECT | PCEMINI_MASK_RUN) : 0);

    // D-pad hat switch (8-way encoding)
    // Merge digital d-pad with left analog stick
    uint8_t up    = (buttons & JP_BUTTON_DU) || (ly < (128 - PCEMINI_STICK_DEADZONE)) ? 1 : 0;
    uint8_t down  = (buttons & JP_BUTTON_DD) || (ly > (128 + PCEMINI_STICK_DEADZONE)) ? 1 : 0;
    uint8_t left  = (buttons & JP_BUTTON_DL) || (lx < (128 - PCEMINI_STICK_DEADZONE)) ? 1 : 0;
    uint8_t right = (buttons & JP_BUTTON_DR) || (lx > (128 + PCEMINI_STICK_DEADZONE)) ? 1 : 0;

    if (up && right)        pcemini_report.hat = PCEMINI_HAT_UPRIGHT;
    else if (up && left)    pcemini_report.hat = PCEMINI_HAT_UPLEFT;
    else if (down && right) pcemini_report.hat = PCEMINI_HAT_DOWNRIGHT;
    else if (down && left)  pcemini_report.hat = PCEMINI_HAT_DOWNLEFT;
    else if (up)            pcemini_report.hat = PCEMINI_HAT_UP;
    else if (down)          pcemini_report.hat = PCEMINI_HAT_DOWN;
    else if (left)          pcemini_report.hat = PCEMINI_HAT_LEFT;
    else if (right)         pcemini_report.hat = PCEMINI_HAT_RIGHT;
    else                    pcemini_report.hat = PCEMINI_HAT_NOTHING;

    return tud_hid_report(0, &pcemini_report, sizeof(pcemini_report));
}

static bool pcemini_mode_send_report(uint8_t player_index,
                                      const input_event_t* event,
                                      const profile_output_t* profile_out,
                                      uint32_t buttons)
{
    (void)player_index;
    (void)event;

    last_buttons = buttons;
    last_lx = profile_out ? profile_out->left_x : 128;
    last_ly = profile_out ? profile_out->left_y : 128;
    return pcemini_build_and_send(buttons, last_lx, last_ly);
}

// Continuous resend while turbo is active
static void pcemini_mode_task(void)
{
    if ((turbo_b3_held || turbo_b4_held) && tud_hid_ready()) {
        pcemini_build_and_send(last_buttons, last_lx, last_ly);
    }
}

static const uint8_t* pcemini_mode_get_device_descriptor(void)
{
    return (const uint8_t*)&pcemini_device_descriptor;
}

static const uint8_t* pcemini_mode_get_config_descriptor(void)
{
    return pcemini_config_descriptor;
}

static const uint8_t* pcemini_mode_get_report_descriptor(void)
{
    return pcemini_report_descriptor;
}

// ============================================================================
// MODE EXPORT
// ============================================================================

const usbd_mode_t pcemini_mode = {
    .name = "PCEMini",
    .mode = USB_OUTPUT_MODE_PCEMINI,

    .get_device_descriptor = pcemini_mode_get_device_descriptor,
    .get_config_descriptor = pcemini_mode_get_config_descriptor,
    .get_report_descriptor = pcemini_mode_get_report_descriptor,

    .init = pcemini_mode_init,
    .send_report = pcemini_mode_send_report,
    .is_ready = pcemini_mode_is_ready,

    // No feedback support for PC Engine Mini
    .handle_output = NULL,
    .get_rumble = NULL,
    .get_feedback = NULL,
    .get_report = NULL,
    .get_class_driver = NULL,
    .task = pcemini_mode_task,
};
