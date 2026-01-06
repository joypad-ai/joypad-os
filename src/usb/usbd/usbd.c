// usbd.c - USB device output
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Implements USB device mode for Joypad, enabling the adapter to emulate
// a gamepad for USB-capable consoles. Uses TinyUSB device stack.
//
// Supports multiple output modes:
// - HID (DInput/PS3-compatible) - default
// - Xbox Original (XID protocol)
// - Future: XInput, PS4, Switch, etc.
//
// Mode is stored in flash and can be changed via CDC commands.
// Mode changes require USB re-enumeration (device reset).

#include "usbd.h"
#include "descriptors/hid_descriptors.h"
#include "descriptors/xbox_og_descriptors.h"
#include "descriptors/xinput_descriptors.h"
#include "descriptors/switch_descriptors.h"
#include "descriptors/ps3_descriptors.h"
#include "descriptors/psclassic_descriptors.h"
#include "descriptors/ps4_descriptors.h"
#include "descriptors/xbone_descriptors.h"
#include "descriptors/xac_descriptors.h"
#include "tud_xid.h"
#include "tud_xinput.h"
#include "tud_xbone.h"
#include "cdc/cdc.h"
#include "cdc/cdc_commands.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include "core/services/storage/flash.h"
#include "core/services/button/button.h"
#include "core/services/profiles/profile.h"
#ifndef DISABLE_USB_HOST
#include "usb/usbh/hid/devices/vendors/sony/sony_ds4.h"
#endif
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "pico/unique_id.h"
#include "hardware/watchdog.h"
#include "hardware/gpio.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// STATE
// ============================================================================

// Current HID report (for HID mode)
static joypad_hid_report_t hid_report;

// Current XID report (for Xbox OG mode)
static xbox_og_in_report_t xid_report;
static xbox_og_out_report_t xid_rumble;
static bool xid_rumble_available = false;

// Current XInput report (for Xbox 360 mode)
static xinput_in_report_t xinput_report;
static xinput_out_report_t xinput_output;
static bool xinput_output_available = false;

// Current Switch report (for Nintendo Switch mode)
static switch_in_report_t switch_report;

// Current PS3 report (for PlayStation 3 mode)
static ps3_in_report_t ps3_report;
static ps3_out_report_t ps3_output;
static bool ps3_output_available = false;

// Current PS Classic report (for PlayStation Classic mode)
static psclassic_in_report_t psclassic_report;

// Current PS4 report (for PlayStation 4 mode)
// Using raw byte array to avoid bitfield packing issues across compilers
static uint8_t ps4_report_buffer[64];
static ps4_out_report_t ps4_output;
static bool ps4_output_available = false;
static uint8_t ps4_report_counter = 0;

// Current Xbox One report (for Xbox One mode)
static gip_input_report_t xbone_report;

// Current XAC report (for Xbox Adaptive Controller compatible mode)
static xac_in_report_t xac_report;

// ============================================================================
// EVENT-DRIVEN OUTPUT STATE
// ============================================================================

// Pending input events (queued by tap callback, sent when USB ready)
#define USB_MAX_PLAYERS 4
static input_event_t pending_events[USB_MAX_PLAYERS];
static bool pending_flags[USB_MAX_PLAYERS] = {false};

// Serial number from board unique ID (12 hex chars + null)
#define USB_SERIAL_LEN 12
static char usb_serial_str[USB_SERIAL_LEN + 1];

// Current output mode (persisted to flash)
static usb_output_mode_t output_mode = USB_OUTPUT_MODE_HID;
static flash_t flash_settings;

// Mode names for display
static const char* mode_names[] = {
    [USB_OUTPUT_MODE_HID] = "DInput",
    [USB_OUTPUT_MODE_XBOX_ORIGINAL] = "Xbox Original (XID)",
    [USB_OUTPUT_MODE_XINPUT] = "XInput",
    [USB_OUTPUT_MODE_PS3] = "PS3",
    [USB_OUTPUT_MODE_PS4] = "PS4",
    [USB_OUTPUT_MODE_SWITCH] = "Switch",
    [USB_OUTPUT_MODE_PSCLASSIC] = "PS Classic",
    [USB_OUTPUT_MODE_XBONE] = "Xbox One",
    [USB_OUTPUT_MODE_XAC] = "XAC Compat",
};

// ============================================================================
// PROFILE PROCESSING
// ============================================================================

// Apply profile mapping (combos, button remaps) to input event
// Returns the processed buttons; analog values are updated in-place in profile_out
static uint32_t apply_usbd_profile(const input_event_t* event, profile_output_t* profile_out)
{
    const profile_t* profile = profile_get_active(OUTPUT_TARGET_USB_DEVICE);

    profile_apply(profile,
                  event->buttons,
                  event->analog[ANALOG_LX], event->analog[ANALOG_LY],
                  event->analog[ANALOG_RX], event->analog[ANALOG_RY],
                  event->analog[ANALOG_L2], event->analog[ANALOG_R2],
                  profile_out);

    // If no built-in profile, apply custom profile button mapping (if active)
    // Custom profiles work alongside built-in profiles: built-in first, then custom
    // This allows usb2usb (no built-in profiles) to use custom profiles exclusively
    if (!profile) {
        const custom_profile_t* custom = flash_get_active_custom_profile();
        if (custom) {
            // Apply custom profile button mapping
            uint32_t original_buttons = profile_out->buttons;
            profile_out->buttons = custom_profile_apply_buttons(custom, profile_out->buttons);

            // Debug: log button remapping (only when buttons change)
            static uint32_t last_logged = 0;
            if (original_buttons != profile_out->buttons && original_buttons != last_logged) {
                printf("[usbd] Custom profile applied: 0x%08lX -> 0x%08lX\n",
                       (unsigned long)original_buttons, (unsigned long)profile_out->buttons);
                last_logged = original_buttons;
            }

            // Apply stick sensitivity
            if (custom->left_stick_sens != 100) {
                float sens = custom->left_stick_sens / 100.0f;
                int16_t rel_x = (int16_t)profile_out->left_x - 128;
                int16_t rel_y = (int16_t)profile_out->left_y - 128;
                profile_out->left_x = (uint8_t)(128 + (int16_t)(rel_x * sens));
                profile_out->left_y = (uint8_t)(128 + (int16_t)(rel_y * sens));
            }
            if (custom->right_stick_sens != 100) {
                float sens = custom->right_stick_sens / 100.0f;
                int16_t rel_x = (int16_t)profile_out->right_x - 128;
                int16_t rel_y = (int16_t)profile_out->right_y - 128;
                profile_out->right_x = (uint8_t)(128 + (int16_t)(rel_x * sens));
                profile_out->right_y = (uint8_t)(128 + (int16_t)(rel_y * sens));
            }

            // Apply profile flags
            if (custom->flags & PROFILE_FLAG_SWAP_STICKS) {
                uint8_t tmp_x = profile_out->left_x;
                uint8_t tmp_y = profile_out->left_y;
                profile_out->left_x = profile_out->right_x;
                profile_out->left_y = profile_out->right_y;
                profile_out->right_x = tmp_x;
                profile_out->right_y = tmp_y;
            }
            if (custom->flags & PROFILE_FLAG_INVERT_LY) {
                profile_out->left_y = 255 - profile_out->left_y;
            }
            if (custom->flags & PROFILE_FLAG_INVERT_RY) {
                profile_out->right_y = 255 - profile_out->right_y;
            }
        }
    }

    // Copy motion data through (no remapping)
    profile_out->has_motion = event->has_motion;
    if (event->has_motion) {
        profile_out->accel[0] = event->accel[0];
        profile_out->accel[1] = event->accel[1];
        profile_out->accel[2] = event->accel[2];
        profile_out->gyro[0] = event->gyro[0];
        profile_out->gyro[1] = event->gyro[1];
        profile_out->gyro[2] = event->gyro[2];
    }

    // Copy pressure data through (no remapping)
    profile_out->has_pressure = event->has_pressure;
    if (event->has_pressure) {
        for (int i = 0; i < 12; i++) {
            profile_out->pressure[i] = event->pressure[i];
        }
    }

    // Stream output to CDC for web config (if enabled)
    // This shows the processed output values after profile mapping
    uint8_t output_axes[6] = {
        profile_out->left_x, profile_out->left_y,
        profile_out->right_x, profile_out->right_y,
        profile_out->l2_analog, profile_out->r2_analog
    };
    cdc_commands_send_output_event(profile_out->buttons, output_axes);

    return profile_out->buttons;
}

// ============================================================================
// CONVERSION HELPERS
// ============================================================================

// Convert input_event buttons to HID gamepad buttons (18 buttons)
static uint32_t convert_buttons(uint32_t buttons)
{
    uint32_t hid_buttons = 0;

    // Joypad uses active-high (1 = pressed), HID uses active-high (1 = pressed)
    // No inversion needed

    if (buttons & JP_BUTTON_B1) hid_buttons |= USB_GAMEPAD_MASK_B1;
    if (buttons & JP_BUTTON_B2) hid_buttons |= USB_GAMEPAD_MASK_B2;
    if (buttons & JP_BUTTON_B3) hid_buttons |= USB_GAMEPAD_MASK_B3;
    if (buttons & JP_BUTTON_B4) hid_buttons |= USB_GAMEPAD_MASK_B4;
    if (buttons & JP_BUTTON_L1) hid_buttons |= USB_GAMEPAD_MASK_L1;
    if (buttons & JP_BUTTON_R1) hid_buttons |= USB_GAMEPAD_MASK_R1;
    if (buttons & JP_BUTTON_L2) hid_buttons |= USB_GAMEPAD_MASK_L2;
    if (buttons & JP_BUTTON_R2) hid_buttons |= USB_GAMEPAD_MASK_R2;
    if (buttons & JP_BUTTON_S1) hid_buttons |= USB_GAMEPAD_MASK_S1;
    if (buttons & JP_BUTTON_S2) hid_buttons |= USB_GAMEPAD_MASK_S2;
    if (buttons & JP_BUTTON_L3) hid_buttons |= USB_GAMEPAD_MASK_L3;
    if (buttons & JP_BUTTON_R3) hid_buttons |= USB_GAMEPAD_MASK_R3;
    if (buttons & JP_BUTTON_A1) hid_buttons |= USB_GAMEPAD_MASK_A1;
    if (buttons & JP_BUTTON_A2) hid_buttons |= USB_GAMEPAD_MASK_A2;
    if (buttons & JP_BUTTON_A3) hid_buttons |= USB_GAMEPAD_MASK_A3;
    if (buttons & JP_BUTTON_A4) hid_buttons |= USB_GAMEPAD_MASK_A4;
    if (buttons & JP_BUTTON_L4) hid_buttons |= USB_GAMEPAD_MASK_L4;
    if (buttons & JP_BUTTON_R4) hid_buttons |= USB_GAMEPAD_MASK_R4;

    return hid_buttons;
}

// Convert input_event dpad to HID hat switch
static uint8_t convert_dpad_to_hat(uint32_t buttons)
{
    // Joypad uses active-high (1 = pressed)
    uint8_t up = (buttons & JP_BUTTON_DU) ? 1 : 0;
    uint8_t down = (buttons & JP_BUTTON_DD) ? 1 : 0;
    uint8_t left = (buttons & JP_BUTTON_DL) ? 1 : 0;
    uint8_t right = (buttons & JP_BUTTON_DR) ? 1 : 0;

    if (up && right) return HID_HAT_UP_RIGHT;
    if (up && left) return HID_HAT_UP_LEFT;
    if (down && right) return HID_HAT_DOWN_RIGHT;
    if (down && left) return HID_HAT_DOWN_LEFT;
    if (up) return HID_HAT_UP;
    if (down) return HID_HAT_DOWN;
    if (left) return HID_HAT_LEFT;
    if (right) return HID_HAT_RIGHT;

    return HID_HAT_CENTER;
}

// ============================================================================
// XID CONVERSION HELPERS (Xbox Original mode)
// ============================================================================

// Convert Joypad buttons to Xbox OG digital buttons (byte 2)
static uint8_t convert_xid_digital_buttons(uint32_t buttons)
{
    uint8_t xog_buttons = 0;

    if (buttons & JP_BUTTON_DU) xog_buttons |= XBOX_OG_BTN_DPAD_UP;
    if (buttons & JP_BUTTON_DD) xog_buttons |= XBOX_OG_BTN_DPAD_DOWN;
    if (buttons & JP_BUTTON_DL) xog_buttons |= XBOX_OG_BTN_DPAD_LEFT;
    if (buttons & JP_BUTTON_DR) xog_buttons |= XBOX_OG_BTN_DPAD_RIGHT;
    if (buttons & JP_BUTTON_S2) xog_buttons |= XBOX_OG_BTN_START;
    if (buttons & JP_BUTTON_S1) xog_buttons |= XBOX_OG_BTN_BACK;
    if (buttons & JP_BUTTON_L3) xog_buttons |= XBOX_OG_BTN_L3;
    if (buttons & JP_BUTTON_R3) xog_buttons |= XBOX_OG_BTN_R3;

    return xog_buttons;
}

// Convert analog value from Joypad (0-255, center 128) to Xbox OG signed 16-bit
static int16_t convert_axis_to_s16(uint8_t value)
{
    int32_t scaled = ((int32_t)value - 128) * 256;
    if (scaled < -32768) scaled = -32768;
    if (scaled > 32767) scaled = 32767;
    return (int16_t)scaled;
}

// Convert and invert axis (for Y-axis where convention differs)
// Uses 32-bit math to avoid -32768 negation overflow
static int16_t convert_axis_to_s16_inverted(uint8_t value)
{
    int32_t scaled = -((int32_t)value - 128) * 256;
    if (scaled < -32768) scaled = -32768;
    if (scaled > 32767) scaled = 32767;
    return (int16_t)scaled;
}

// ============================================================================
// MODE SELECTION API
// ============================================================================

usb_output_mode_t usbd_get_mode(void)
{
    return output_mode;
}

// Helper to flush debug output over CDC
static void flush_debug_output(void)
{
    tud_task();
    sleep_ms(20);
    tud_task();
}

bool usbd_set_mode(usb_output_mode_t mode)
{
    if (mode >= USB_OUTPUT_MODE_COUNT) {
        return false;
    }

    // Supported modes: HID, Xbox OG, XInput, PS3, PS4, Switch, PS Classic, Xbox One, XAC
    if (mode != USB_OUTPUT_MODE_HID &&
        mode != USB_OUTPUT_MODE_XBOX_ORIGINAL &&
        mode != USB_OUTPUT_MODE_XINPUT &&
        mode != USB_OUTPUT_MODE_PS3 &&
        mode != USB_OUTPUT_MODE_PS4 &&
        mode != USB_OUTPUT_MODE_SWITCH &&
        mode != USB_OUTPUT_MODE_PSCLASSIC &&
        mode != USB_OUTPUT_MODE_XBONE &&
        mode != USB_OUTPUT_MODE_XAC) {
        printf("[usbd] Mode %d not yet supported\n", mode);
        return false;
    }

    if (mode == output_mode) {
        return false;  // Same mode, no change needed
    }

    printf("[usbd] Changing mode from %s to %s\n",
           mode_names[output_mode], mode_names[mode]);
    flush_debug_output();

    // Save mode to flash immediately (we're about to reset)
    printf("[usbd] Setting flash_settings.usb_output_mode = %d\n", mode);
    flush_debug_output();
    flash_settings.usb_output_mode = (uint8_t)mode;
    printf("[usbd] Calling flash_save_now...\n");
    flush_debug_output();
    flash_save_now(&flash_settings);
    printf("[usbd] Mode saved to flash (mode=%d)\n", flash_settings.usb_output_mode);
    flush_debug_output();

    // Verify the write by reading it back
    flash_t verify_settings;
    if (flash_load(&verify_settings)) {
        printf("[usbd] Verify: mode=%d (expected %d)\n",
               verify_settings.usb_output_mode, mode);
    } else {
        printf("[usbd] Verify FAILED: flash_load returned false!\n");
    }
    flush_debug_output();

    output_mode = mode;

    // Brief delay to allow flash write to complete
    sleep_ms(50);

    // Trigger device reset to re-enumerate with new descriptors
    printf("[usbd] Resetting device for re-enumeration...\n");
    flush_debug_output();
    watchdog_enable(100, false);  // Reset in 100ms
    while(1);  // Wait for watchdog reset

    return true;  // Never reached
}

const char* usbd_get_mode_name(usb_output_mode_t mode)
{
    if (mode < USB_OUTPUT_MODE_COUNT) {
        return mode_names[mode];
    }
    return "Unknown";
}

usb_output_mode_t usbd_get_next_mode(void)
{
    // Cycle through common modes: HID → XInput → PS3 → PS4 → Switch → HID
    // (Skip less common: PS Classic, Xbox Original, Xbox One, XAC)
    switch (output_mode) {
        case USB_OUTPUT_MODE_HID:
            return USB_OUTPUT_MODE_XINPUT;
        case USB_OUTPUT_MODE_XINPUT:
            return USB_OUTPUT_MODE_PS3;
        case USB_OUTPUT_MODE_PS3:
            return USB_OUTPUT_MODE_PS4;
        case USB_OUTPUT_MODE_PS4:
            return USB_OUTPUT_MODE_SWITCH;
        case USB_OUTPUT_MODE_SWITCH:
        default:
            return USB_OUTPUT_MODE_HID;
    }
}

bool usbd_reset_to_hid(void)
{
    if (output_mode != USB_OUTPUT_MODE_HID) {
        usbd_set_mode(USB_OUTPUT_MODE_HID);
        return true;
    }
    return false;
}

// ============================================================================
// EVENT-DRIVEN TAP CALLBACK
// ============================================================================

// Called by router immediately when input arrives (push-based notification)
static void usbd_on_input(output_target_t output, uint8_t player_index, const input_event_t* event)
{
    (void)output;  // Always USB_DEVICE

    if (player_index >= USB_MAX_PLAYERS || !event) {
        return;
    }

    // Check for profile switch combo (SELECT + D-pad Up/Down after 2s hold)
    // This enables hotkey profile cycling for both built-in and custom profiles
    if (player_index == 0) {
        profile_check_switch_combo(event->buttons);
    }

    // Queue the event for sending when USB is ready
    pending_events[player_index] = *event;
    pending_flags[player_index] = true;
}

// ============================================================================
// PUBLIC API
// ============================================================================

void usbd_init(void)
{
    printf("[usbd] Initializing USB device output\n");

    // Initialize and load settings from flash
    flash_init();
    printf("[usbd] Loading settings from flash...\n");
    if (flash_load(&flash_settings)) {
        printf("[usbd] Flash load success! usb_output_mode=%d, active_profile=%d\n",
               flash_settings.usb_output_mode, flash_settings.active_profile_index);
        // Validate loaded mode
        if (flash_settings.usb_output_mode < USB_OUTPUT_MODE_COUNT) {
            // Only accept supported modes
            if (flash_settings.usb_output_mode == USB_OUTPUT_MODE_HID ||
                flash_settings.usb_output_mode == USB_OUTPUT_MODE_XBOX_ORIGINAL ||
                flash_settings.usb_output_mode == USB_OUTPUT_MODE_XINPUT ||
                flash_settings.usb_output_mode == USB_OUTPUT_MODE_PS3 ||
                flash_settings.usb_output_mode == USB_OUTPUT_MODE_PS4 ||
                flash_settings.usb_output_mode == USB_OUTPUT_MODE_SWITCH ||
                flash_settings.usb_output_mode == USB_OUTPUT_MODE_PSCLASSIC ||
                flash_settings.usb_output_mode == USB_OUTPUT_MODE_XBONE ||
                flash_settings.usb_output_mode == USB_OUTPUT_MODE_XAC) {
                output_mode = (usb_output_mode_t)flash_settings.usb_output_mode;
                printf("[usbd] Loaded mode from flash: %s\n", mode_names[output_mode]);
            } else {
                printf("[usbd] Unsupported mode %d in flash, using default\n",
                       flash_settings.usb_output_mode);
            }
        }
    } else {
        printf("[usbd] No valid flash settings (magic mismatch), using defaults\n");
        memset(&flash_settings, 0, sizeof(flash_settings));
    }

    printf("[usbd] Mode: %s\n", mode_names[output_mode]);

    // Get unique board ID for USB serial number (first 12 chars)
    char full_id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];
    pico_get_unique_board_id_string(full_id, sizeof(full_id));
    memcpy(usb_serial_str, full_id, USB_SERIAL_LEN);
    usb_serial_str[USB_SERIAL_LEN] = '\0';
    printf("[usbd] Serial: %s\n", usb_serial_str);

    // Initialize TinyUSB device stack
    tusb_rhport_init_t dev_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = (output_mode == USB_OUTPUT_MODE_XBOX_ORIGINAL)
                 ? TUSB_SPEED_FULL  // Xbox OG is USB 1.1
                 : TUSB_SPEED_AUTO
    };
    tusb_init(0, &dev_init);

    // Initialize reports based on mode
    switch (output_mode) {
        case USB_OUTPUT_MODE_XBOX_ORIGINAL:
            // Initialize XID report to neutral state
            memset(&xid_report, 0, sizeof(xbox_og_in_report_t));
            xid_report.reserved1 = 0x00;
            xid_report.report_len = sizeof(xbox_og_in_report_t);
            memset(&xid_rumble, 0, sizeof(xbox_og_out_report_t));
            break;

        case USB_OUTPUT_MODE_XINPUT:
            // Initialize XInput report to neutral state
            memset(&xinput_report, 0, sizeof(xinput_in_report_t));
            xinput_report.report_id = 0x00;
            xinput_report.report_size = sizeof(xinput_in_report_t);
            memset(&xinput_output, 0, sizeof(xinput_out_report_t));
            break;

        case USB_OUTPUT_MODE_SWITCH:
            // Initialize Switch report to neutral state
            memset(&switch_report, 0, sizeof(switch_in_report_t));
            switch_report.hat = SWITCH_HAT_CENTER;
            switch_report.lx = SWITCH_JOYSTICK_MID;
            switch_report.ly = SWITCH_JOYSTICK_MID;
            switch_report.rx = SWITCH_JOYSTICK_MID;
            switch_report.ry = SWITCH_JOYSTICK_MID;
            break;

        case USB_OUTPUT_MODE_PS3:
            // Initialize PS3 report to neutral state
            ps3_init_report(&ps3_report);
            memset(&ps3_output, 0, sizeof(ps3_out_report_t));
            break;

        case USB_OUTPUT_MODE_PSCLASSIC:
            // Initialize PS Classic report to neutral state
            psclassic_init_report(&psclassic_report);
            break;

        case USB_OUTPUT_MODE_PS4:
            // Initialize PS4 report to neutral state (raw buffer approach)
            memset(ps4_report_buffer, 0, sizeof(ps4_report_buffer));
            ps4_report_buffer[0] = 0x01;  // Report ID
            ps4_report_buffer[1] = 0x80;  // LX center
            ps4_report_buffer[2] = 0x80;  // LY center
            ps4_report_buffer[3] = 0x80;  // RX center
            ps4_report_buffer[4] = 0x80;  // RY center
            ps4_report_buffer[5] = PS4_HAT_NOTHING;  // D-pad neutral (0x0F), no buttons
            // Bytes 6-7: no buttons pressed, counter 0
            // Bytes 8-9: triggers at 0
            // Touchpad fingers unpressed: byte 35 bit 7 = 1, byte 39 bit 7 = 1
            ps4_report_buffer[35] = 0x80;  // touchpad p1 unpressed
            ps4_report_buffer[39] = 0x80;  // touchpad p2 unpressed
            memset(&ps4_output, 0, sizeof(ps4_out_report_t));
            ps4_report_counter = 0;
            break;

        case USB_OUTPUT_MODE_XBONE:
            // Initialize Xbox One report to neutral state
            memset(&xbone_report, 0, sizeof(gip_input_report_t));
            break;

        case USB_OUTPUT_MODE_XAC:
            // Initialize XAC report to neutral state
            xac_init_report(&xac_report);
            break;

        case USB_OUTPUT_MODE_HID:
        default:
            // Initialize HID report to neutral state
            memset(&hid_report, 0, sizeof(joypad_hid_report_t));
            hid_report.lx = 128;  // Center
            hid_report.ly = 128;
            hid_report.rx = 128;
            hid_report.ry = 128;
            hid_report.hat = HID_HAT_CENTER;
            break;
    }

    // Initialize CDC subsystem (only for HID and Switch modes)
    if (output_mode == USB_OUTPUT_MODE_HID || output_mode == USB_OUTPUT_MODE_SWITCH) {
        cdc_init();
    }

    // Register tap callback for event-driven input (push-based notification)
    router_set_tap(OUTPUT_TARGET_USB_DEVICE, usbd_on_input);

    printf("[usbd] Initialization complete\n");
}

void usbd_task(void)
{
    // TinyUSB device task - runs from core0 main loop
    tud_task();

    switch (output_mode) {
        case USB_OUTPUT_MODE_XBOX_ORIGINAL:
            // Xbox OG mode: check for rumble updates
            if (tud_xid_get_rumble(&xid_rumble)) {
                xid_rumble_available = true;
            }
            // Send XID report if ready
            if (tud_xid_ready()) {
                usbd_send_report(0);
            }
            break;

#if CFG_TUD_XINPUT
        case USB_OUTPUT_MODE_XINPUT:
            // XInput mode: check for rumble/LED updates
            if (tud_xinput_get_output(&xinput_output)) {
                xinput_output_available = true;
            }
            // Send XInput report if ready
            if (tud_xinput_ready()) {
                usbd_send_report(0);
            }
            break;
#endif

        case USB_OUTPUT_MODE_SWITCH:
            // Switch mode: process CDC tasks, send HID report
            cdc_task();
            if (tud_hid_ready()) {
                usbd_send_report(0);
            }
            break;

        case USB_OUTPUT_MODE_PS3:
            // PS3 mode: send HID report (no CDC - PS3 doesn't use it)
            if (tud_hid_ready()) {
                usbd_send_report(0);
            }
            break;

        case USB_OUTPUT_MODE_PSCLASSIC:
            // PS Classic mode: send HID report (no CDC)
            if (tud_hid_ready()) {
                usbd_send_report(0);
            }
            break;

        case USB_OUTPUT_MODE_PS4:
            // PS4 mode: send HID report (no CDC)
            if (tud_hid_ready()) {
                usbd_send_report(0);
            }
            break;

        case USB_OUTPUT_MODE_XBONE:
            // Xbox One mode: update driver and send report
            tud_xbone_update();
            if (xbone_is_powered_on() && tud_xbone_ready()) {
                usbd_send_report(0);
            }
            break;

        case USB_OUTPUT_MODE_XAC:
            // XAC mode: send HID report (no CDC)
            if (tud_hid_ready()) {
                usbd_send_report(0);
            }
            break;

        case USB_OUTPUT_MODE_HID:
        default:
            // HID mode: process CDC tasks
            cdc_task();
            // Send HID report if device is ready
            if (tud_hid_ready()) {
                usbd_send_report(0);
            }
            break;
    }
}

// Send XID report (Xbox Original mode)
static bool usbd_send_xid_report(uint8_t player_index)
{
    if (!tud_xid_ready()) {
        return false;
    }

    // Check for pending event (event-driven from tap callback)
    if (player_index >= USB_MAX_PLAYERS || !pending_flags[player_index]) {
        return false;
    }

    const input_event_t* event = &pending_events[player_index];
    pending_flags[player_index] = false;  // Clear after consumption

    // Apply profile (combos, button remaps)
    profile_output_t profile_out;
    uint32_t buttons = apply_usbd_profile(event, &profile_out);

    // Digital buttons (DPAD, Start, Back, L3, R3)
    xid_report.buttons = convert_xid_digital_buttons(buttons);

    // Analog face buttons (0 = not pressed, 255 = fully pressed)
    xid_report.a     = (buttons & JP_BUTTON_B1) ? 0xFF : 0x00;
    xid_report.b     = (buttons & JP_BUTTON_B2) ? 0xFF : 0x00;
    xid_report.x     = (buttons & JP_BUTTON_B3) ? 0xFF : 0x00;
    xid_report.y     = (buttons & JP_BUTTON_B4) ? 0xFF : 0x00;
    xid_report.black = (buttons & JP_BUTTON_L1) ? 0xFF : 0x00;  // L1 -> Black
    xid_report.white = (buttons & JP_BUTTON_R1) ? 0xFF : 0x00;  // R1 -> White

    // Analog triggers (0-255)
    // Use profile analog values, fall back to digital if analog is 0 but button pressed
    xid_report.trigger_l = profile_out.l2_analog;
    xid_report.trigger_r = profile_out.r2_analog;
    if (xid_report.trigger_l == 0 && (buttons & JP_BUTTON_L2)) xid_report.trigger_l = 0xFF;
    if (xid_report.trigger_r == 0 && (buttons & JP_BUTTON_R2)) xid_report.trigger_r = 0xFF;

    // Analog sticks (signed 16-bit, -32768 to +32767)
    xid_report.stick_lx = convert_axis_to_s16(profile_out.left_x);
    xid_report.stick_ly = convert_axis_to_s16(profile_out.left_y);
    xid_report.stick_rx = convert_axis_to_s16(profile_out.right_x);
    xid_report.stick_ry = convert_axis_to_s16(profile_out.right_y);

    return tud_xid_send_report(&xid_report);
}

// Send HID report (DInput mode)
static bool usbd_send_hid_report(uint8_t player_index)
{
    if (!tud_hid_ready()) {
        return false;
    }

    // Check for pending event (event-driven from tap callback)
    if (player_index >= USB_MAX_PLAYERS || !pending_flags[player_index]) {
        return false;
    }

    const input_event_t* event = &pending_events[player_index];
    pending_flags[player_index] = false;  // Clear after consumption

    // Apply profile (combos, button remaps)
    profile_output_t profile_out;
    uint32_t processed_buttons = apply_usbd_profile(event, &profile_out);

    // Convert processed buttons to HID report (18 buttons across 3 bytes)
    uint32_t buttons = convert_buttons(processed_buttons);
    hid_report.buttons_lo = buttons & 0xFF;           // Buttons 1-8
    hid_report.buttons_mid = (buttons >> 8) & 0xFF;   // Buttons 9-16
    hid_report.buttons_hi = (buttons >> 16) & 0x03;   // Buttons 17-18 (L4, R4)
    hid_report.hat = convert_dpad_to_hat(processed_buttons);

    // Analog sticks (HID convention: 0=up, 255=down - no inversion needed)
    hid_report.lx = profile_out.left_x;
    hid_report.ly = profile_out.left_y;
    hid_report.rx = profile_out.right_x;
    hid_report.ry = profile_out.right_y;

    // PS3 pressure axes (0x00 = released, 0xFF = fully pressed)
    hid_report.pressure_dpad_right = (processed_buttons & JP_BUTTON_DR) ? 0xFF : 0x00;
    hid_report.pressure_dpad_left  = (processed_buttons & JP_BUTTON_DL) ? 0xFF : 0x00;
    hid_report.pressure_dpad_up    = (processed_buttons & JP_BUTTON_DU) ? 0xFF : 0x00;
    hid_report.pressure_dpad_down  = (processed_buttons & JP_BUTTON_DD) ? 0xFF : 0x00;
    hid_report.pressure_triangle   = (buttons & USB_GAMEPAD_MASK_B4) ? 0xFF : 0x00;
    hid_report.pressure_circle     = (buttons & USB_GAMEPAD_MASK_B2) ? 0xFF : 0x00;
    hid_report.pressure_cross      = (buttons & USB_GAMEPAD_MASK_B1) ? 0xFF : 0x00;
    hid_report.pressure_square     = (buttons & USB_GAMEPAD_MASK_B3) ? 0xFF : 0x00;
    hid_report.pressure_l1         = (buttons & USB_GAMEPAD_MASK_L1) ? 0xFF : 0x00;
    hid_report.pressure_r1         = (buttons & USB_GAMEPAD_MASK_R1) ? 0xFF : 0x00;
    // Use analog values for L2/R2 triggers
    hid_report.pressure_l2         = profile_out.l2_analog;
    hid_report.pressure_r2         = profile_out.r2_analog;

    return tud_hid_report(0, &hid_report, sizeof(hid_report));
}

#if CFG_TUD_XINPUT
// Send XInput report (Xbox 360 mode)
static bool usbd_send_xinput_report(uint8_t player_index)
{
    if (!tud_xinput_ready()) {
        return false;
    }

    // Check for pending event (event-driven from tap callback)
    if (player_index >= USB_MAX_PLAYERS || !pending_flags[player_index]) {
        return false;
    }

    const input_event_t* event = &pending_events[player_index];
    pending_flags[player_index] = false;  // Clear after consumption

    // Apply profile (combos, button remaps)
    profile_output_t profile_out;
    uint32_t buttons = apply_usbd_profile(event, &profile_out);

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
    // Use analog values, fall back to digital if analog is 0 but button pressed
    xinput_report.trigger_l = profile_out.l2_analog;
    xinput_report.trigger_r = profile_out.r2_analog;
    if (xinput_report.trigger_l == 0 && (buttons & JP_BUTTON_L2)) xinput_report.trigger_l = 0xFF;
    if (xinput_report.trigger_r == 0 && (buttons & JP_BUTTON_R2)) xinput_report.trigger_r = 0xFF;

    // Analog sticks (signed 16-bit, -32768 to +32767)
    // Y-axis inverted: input 0=down, XInput convention positive=up
    xinput_report.stick_lx = convert_axis_to_s16(profile_out.left_x);
    xinput_report.stick_ly = convert_axis_to_s16_inverted(profile_out.left_y);
    xinput_report.stick_rx = convert_axis_to_s16(profile_out.right_x);
    xinput_report.stick_ry = convert_axis_to_s16_inverted(profile_out.right_y);

    return tud_xinput_send_report(&xinput_report);
}
#endif

// Send Switch report (Nintendo Switch mode)
static bool usbd_send_switch_report(uint8_t player_index)
{
    if (!tud_hid_ready()) {
        return false;
    }

    // Check for pending event (event-driven from tap callback)
    if (player_index >= USB_MAX_PLAYERS || !pending_flags[player_index]) {
        return false;
    }

    const input_event_t* event = &pending_events[player_index];
    pending_flags[player_index] = false;  // Clear after consumption

    // Apply profile (combos, button remaps)
    profile_output_t profile_out;
    uint32_t buttons = apply_usbd_profile(event, &profile_out);

    // Buttons (16-bit) - position-based mapping (matches GP2040-CE)
    switch_report.buttons = 0;
    if (buttons & JP_BUTTON_B1) switch_report.buttons |= SWITCH_MASK_B;  // B1 (bottom) → B
    if (buttons & JP_BUTTON_B2) switch_report.buttons |= SWITCH_MASK_A;  // B2 (right)  → A
    if (buttons & JP_BUTTON_B3) switch_report.buttons |= SWITCH_MASK_Y;  // B3 (left)   → Y
    if (buttons & JP_BUTTON_B4) switch_report.buttons |= SWITCH_MASK_X;  // B4 (top)    → X
    if (buttons & JP_BUTTON_L1) switch_report.buttons |= SWITCH_MASK_L;  // L
    if (buttons & JP_BUTTON_R1) switch_report.buttons |= SWITCH_MASK_R;  // R
    if (buttons & JP_BUTTON_L2) switch_report.buttons |= SWITCH_MASK_ZL; // ZL
    if (buttons & JP_BUTTON_R2) switch_report.buttons |= SWITCH_MASK_ZR; // ZR
    if (buttons & JP_BUTTON_S1) switch_report.buttons |= SWITCH_MASK_MINUS;  // Minus
    if (buttons & JP_BUTTON_S2) switch_report.buttons |= SWITCH_MASK_PLUS;   // Plus
    if (buttons & JP_BUTTON_L3) switch_report.buttons |= SWITCH_MASK_L3;
    if (buttons & JP_BUTTON_R3) switch_report.buttons |= SWITCH_MASK_R3;
    if (buttons & JP_BUTTON_A1) switch_report.buttons |= SWITCH_MASK_HOME;
    if (buttons & JP_BUTTON_A2) switch_report.buttons |= SWITCH_MASK_CAPTURE;

    // D-pad as hat switch
    switch_report.hat = convert_dpad_to_hat(buttons);

    // Analog sticks (HID convention: 0=up, 255=down - no inversion needed)
    switch_report.lx = profile_out.left_x;
    switch_report.ly = profile_out.left_y;
    switch_report.rx = profile_out.right_x;
    switch_report.ry = profile_out.right_y;

    switch_report.vendor = 0;

    return tud_hid_report(0, &switch_report, sizeof(switch_report));
}

// Send PS3 report (PlayStation 3 DualShock 3 mode)
static bool usbd_send_ps3_report(uint8_t player_index)
{
    if (!tud_hid_ready()) {
        return false;
    }

    // Check for pending event (event-driven from tap callback)
    if (player_index >= USB_MAX_PLAYERS || !pending_flags[player_index]) {
        return false;
    }

    const input_event_t* event = &pending_events[player_index];
    pending_flags[player_index] = false;  // Clear after consumption

    // Apply profile (combos, button remaps)
    profile_output_t profile_out;
    uint32_t buttons = apply_usbd_profile(event, &profile_out);

    // Digital buttons byte 0
    ps3_report.buttons[0] = 0;
    if (buttons & JP_BUTTON_S1) ps3_report.buttons[0] |= PS3_BTN_SELECT;
    if (buttons & JP_BUTTON_L3) ps3_report.buttons[0] |= PS3_BTN_L3;
    if (buttons & JP_BUTTON_R3) ps3_report.buttons[0] |= PS3_BTN_R3;
    if (buttons & JP_BUTTON_S2) ps3_report.buttons[0] |= PS3_BTN_START;
    if (buttons & JP_BUTTON_DU) ps3_report.buttons[0] |= PS3_BTN_DPAD_UP;
    if (buttons & JP_BUTTON_DR) ps3_report.buttons[0] |= PS3_BTN_DPAD_RIGHT;
    if (buttons & JP_BUTTON_DD) ps3_report.buttons[0] |= PS3_BTN_DPAD_DOWN;
    if (buttons & JP_BUTTON_DL) ps3_report.buttons[0] |= PS3_BTN_DPAD_LEFT;

    // Digital buttons byte 1
    ps3_report.buttons[1] = 0;
    if (buttons & JP_BUTTON_L2) ps3_report.buttons[1] |= PS3_BTN_L2;
    if (buttons & JP_BUTTON_R2) ps3_report.buttons[1] |= PS3_BTN_R2;
    if (buttons & JP_BUTTON_L1) ps3_report.buttons[1] |= PS3_BTN_L1;
    if (buttons & JP_BUTTON_R1) ps3_report.buttons[1] |= PS3_BTN_R1;
    if (buttons & JP_BUTTON_B4) ps3_report.buttons[1] |= PS3_BTN_TRIANGLE;
    if (buttons & JP_BUTTON_B2) ps3_report.buttons[1] |= PS3_BTN_CIRCLE;
    if (buttons & JP_BUTTON_B1) ps3_report.buttons[1] |= PS3_BTN_CROSS;
    if (buttons & JP_BUTTON_B3) ps3_report.buttons[1] |= PS3_BTN_SQUARE;

    // Digital buttons byte 2 (PS button)
    ps3_report.buttons[2] = 0;
    if (buttons & JP_BUTTON_A1) ps3_report.buttons[2] |= PS3_BTN_PS;

    // Analog sticks (HID convention: 0=up, 255=down - no inversion needed)
    ps3_report.lx = profile_out.left_x;
    ps3_report.ly = profile_out.left_y;
    ps3_report.rx = profile_out.right_x;
    ps3_report.ry = profile_out.right_y;

    // Pressure-sensitive buttons - use actual pressure data if available
    if (profile_out.has_pressure) {
        // D-pad pressure
        ps3_report.pressure_up    = profile_out.pressure[0];
        ps3_report.pressure_right = profile_out.pressure[1];
        ps3_report.pressure_down  = profile_out.pressure[2];
        ps3_report.pressure_left  = profile_out.pressure[3];
        // Triggers/bumpers pressure
        ps3_report.pressure_l2    = profile_out.pressure[4];
        ps3_report.pressure_r2    = profile_out.pressure[5];
        ps3_report.pressure_l1    = profile_out.pressure[6];
        ps3_report.pressure_r1    = profile_out.pressure[7];
        // Face buttons pressure
        ps3_report.pressure_triangle = profile_out.pressure[8];
        ps3_report.pressure_circle   = profile_out.pressure[9];
        ps3_report.pressure_cross    = profile_out.pressure[10];
        ps3_report.pressure_square   = profile_out.pressure[11];
    } else {
        // Fall back to digital (0xFF pressed, 0x00 released)
        ps3_report.pressure_up    = (buttons & JP_BUTTON_DU) ? 0xFF : 0x00;
        ps3_report.pressure_right = (buttons & JP_BUTTON_DR) ? 0xFF : 0x00;
        ps3_report.pressure_down  = (buttons & JP_BUTTON_DD) ? 0xFF : 0x00;
        ps3_report.pressure_left  = (buttons & JP_BUTTON_DL) ? 0xFF : 0x00;
        ps3_report.pressure_l2    = profile_out.l2_analog;
        ps3_report.pressure_r2    = profile_out.r2_analog;
        ps3_report.pressure_l1    = (buttons & JP_BUTTON_L1) ? 0xFF : 0x00;
        ps3_report.pressure_r1    = (buttons & JP_BUTTON_R1) ? 0xFF : 0x00;
        ps3_report.pressure_triangle = (buttons & JP_BUTTON_B4) ? 0xFF : 0x00;
        ps3_report.pressure_circle   = (buttons & JP_BUTTON_B2) ? 0xFF : 0x00;
        ps3_report.pressure_cross    = (buttons & JP_BUTTON_B1) ? 0xFF : 0x00;
        ps3_report.pressure_square   = (buttons & JP_BUTTON_B3) ? 0xFF : 0x00;
    }

    // Motion data (SIXAXIS) - big-endian 16-bit values
    if (event->has_motion) {
        ps3_report.accel_x = __builtin_bswap16((uint16_t)event->accel[0]);
        ps3_report.accel_y = __builtin_bswap16((uint16_t)event->accel[1]);
        ps3_report.accel_z = __builtin_bswap16((uint16_t)event->accel[2]);
        ps3_report.gyro_z  = __builtin_bswap16((uint16_t)event->gyro[2]);
    } else {
        // Neutral motion (center at 512 = 0x0200, big-endian = 0x0002)
        ps3_report.accel_x = PS3_SIXAXIS_MID_BE;
        ps3_report.accel_y = PS3_SIXAXIS_MID_BE;
        ps3_report.accel_z = PS3_SIXAXIS_MID_BE;
        ps3_report.gyro_z  = PS3_SIXAXIS_MID_BE;
    }

    // Send full report including report_id
    return tud_hid_report(0, &ps3_report, sizeof(ps3_report));
}

// Send PS Classic report (PlayStation Classic mode)
// GP2040-CE compatible 2-byte format:
// Bits 0-9: 10 buttons
// Bits 10-13: D-pad encoded
// Bits 14-15: Padding
static bool usbd_send_psclassic_report(uint8_t player_index)
{
    if (!tud_hid_ready()) {
        return false;
    }

    // Check for pending event (event-driven from tap callback)
    if (player_index >= USB_MAX_PLAYERS || !pending_flags[player_index]) {
        return false;
    }

    const input_event_t* event = &pending_events[player_index];
    pending_flags[player_index] = false;  // Clear after consumption

    // Apply profile (combos, button remaps)
    profile_output_t profile_out;
    uint32_t buttons = apply_usbd_profile(event, &profile_out);

    // Start with D-pad centered
    psclassic_report.buttons = PSCLASSIC_DPAD_CENTER;

    // D-pad encoding (bits 10-13)
    uint8_t up = (buttons & JP_BUTTON_DU) ? 1 : 0;
    uint8_t down = (buttons & JP_BUTTON_DD) ? 1 : 0;
    uint8_t left = (buttons & JP_BUTTON_DL) ? 1 : 0;
    uint8_t right = (buttons & JP_BUTTON_DR) ? 1 : 0;

    if (up && right)        psclassic_report.buttons = PSCLASSIC_DPAD_UP_RIGHT;
    else if (up && left)    psclassic_report.buttons = PSCLASSIC_DPAD_UP_LEFT;
    else if (down && right) psclassic_report.buttons = PSCLASSIC_DPAD_DOWN_RIGHT;
    else if (down && left)  psclassic_report.buttons = PSCLASSIC_DPAD_DOWN_LEFT;
    else if (up)            psclassic_report.buttons = PSCLASSIC_DPAD_UP;
    else if (down)          psclassic_report.buttons = PSCLASSIC_DPAD_DOWN;
    else if (left)          psclassic_report.buttons = PSCLASSIC_DPAD_LEFT;
    else if (right)         psclassic_report.buttons = PSCLASSIC_DPAD_RIGHT;

    // Face buttons and shoulders (bits 0-9)
    psclassic_report.buttons |=
          (buttons & JP_BUTTON_B4 ? PSCLASSIC_MASK_TRIANGLE : 0)
        | (buttons & JP_BUTTON_B2 ? PSCLASSIC_MASK_CIRCLE   : 0)
        | (buttons & JP_BUTTON_B1 ? PSCLASSIC_MASK_CROSS    : 0)
        | (buttons & JP_BUTTON_B3 ? PSCLASSIC_MASK_SQUARE   : 0)
        | (buttons & JP_BUTTON_L1 ? PSCLASSIC_MASK_L1       : 0)
        | (buttons & JP_BUTTON_R1 ? PSCLASSIC_MASK_R1       : 0)
        | (buttons & JP_BUTTON_L2 ? PSCLASSIC_MASK_L2       : 0)
        | (buttons & JP_BUTTON_R2 ? PSCLASSIC_MASK_R2       : 0)
        | (buttons & JP_BUTTON_S1 ? PSCLASSIC_MASK_SELECT   : 0)
        | (buttons & JP_BUTTON_S2 ? PSCLASSIC_MASK_START    : 0);

    return tud_hid_report(0, &psclassic_report, sizeof(psclassic_report));
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
static bool usbd_send_ps4_report(uint8_t player_index)
{
    if (!tud_hid_ready()) {
        return false;
    }

    // Check for pending event (event-driven from tap callback)
    if (player_index >= USB_MAX_PLAYERS || !pending_flags[player_index]) {
        return false;
    }

    const input_event_t* event = &pending_events[player_index];
    pending_flags[player_index] = false;  // Clear after consumption

    // Apply profile (combos, button remaps)
    profile_output_t profile_out;
    uint32_t buttons = apply_usbd_profile(event, &profile_out);

    // Byte 0: Report ID
    ps4_report_buffer[0] = 0x01;

    // Bytes 1-4: Analog sticks (HID convention: 0=up, 255=down - no inversion needed)
    ps4_report_buffer[1] = profile_out.left_x;          // LX
    ps4_report_buffer[2] = profile_out.left_y;          // LY
    ps4_report_buffer[3] = profile_out.right_x;         // RX
    ps4_report_buffer[4] = profile_out.right_y;         // RY

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
    ps4_report_buffer[8] = profile_out.l2_analog;  // Left trigger
    ps4_report_buffer[9] = profile_out.r2_analog;  // Right trigger

    // Bytes 10-11: Timestamp (we can just increment)
    // Bytes 12-63: Leave as initialized (sensor data, touchpad, padding)

    // Send with report_id=0x01, letting TinyUSB prepend it
    // Skip byte 0 of buffer (our report_id) and send 63 bytes of data
    return tud_hid_report(0x01, &ps4_report_buffer[1], 63);
}

// Send Xbox One report (GIP protocol)
static bool usbd_send_xbone_report(uint8_t player_index)
{
    if (!tud_xbone_ready()) {
        return false;
    }

    // Check for pending event (event-driven from tap callback)
    if (player_index >= USB_MAX_PLAYERS || !pending_flags[player_index]) {
        return false;
    }

    const input_event_t* event = &pending_events[player_index];
    pending_flags[player_index] = false;  // Clear after consumption

    // Clear report
    memset(&xbone_report, 0, sizeof(gip_input_report_t));

    // Apply profile (combos, button remaps)
    profile_output_t profile_out;
    uint32_t buttons = apply_usbd_profile(event, &profile_out);

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

    xbone_report.left_thumb = (buttons & JP_BUTTON_L3) ? 1 : 0;
    xbone_report.right_thumb = (buttons & JP_BUTTON_R3) ? 1 : 0;

    xbone_report.dpad_up = (buttons & JP_BUTTON_DU) ? 1 : 0;
    xbone_report.dpad_down = (buttons & JP_BUTTON_DD) ? 1 : 0;
    xbone_report.dpad_left = (buttons & JP_BUTTON_DL) ? 1 : 0;
    xbone_report.dpad_right = (buttons & JP_BUTTON_DR) ? 1 : 0;

    // Triggers (0-1023)
    // Map from profile analog (0-255) to Xbox One range (0-1023)
    xbone_report.left_trigger = (uint16_t)profile_out.l2_analog * 4;
    xbone_report.right_trigger = (uint16_t)profile_out.r2_analog * 4;

    // Fallback to digital if analog is 0 but button pressed
    if (xbone_report.left_trigger == 0 && (buttons & JP_BUTTON_L2))
        xbone_report.left_trigger = 1023;
    if (xbone_report.right_trigger == 0 && (buttons & JP_BUTTON_R2))
        xbone_report.right_trigger = 1023;

    // Analog sticks (signed 16-bit, -32768 to +32767)
    // Y-axis inverted: input 0=down, output positive=up
    xbone_report.left_stick_x = convert_axis_to_s16(profile_out.left_x);
    xbone_report.left_stick_y = -convert_axis_to_s16(profile_out.left_y);
    xbone_report.right_stick_x = convert_axis_to_s16(profile_out.right_x);
    xbone_report.right_stick_y = -convert_axis_to_s16(profile_out.right_y);

    return tud_xbone_send_report(&xbone_report);
}

// Send XAC report (Xbox Adaptive Controller compatible mode)
static bool usbd_send_xac_report(uint8_t player_index)
{
    if (!tud_hid_ready()) {
        return false;
    }

    // Check for pending event (event-driven from tap callback)
    if (player_index >= USB_MAX_PLAYERS || !pending_flags[player_index]) {
        return false;
    }

    const input_event_t* event = &pending_events[player_index];
    pending_flags[player_index] = false;  // Clear after consumption

    // Apply profile (combos, button remaps)
    profile_output_t profile_out;
    uint32_t buttons = apply_usbd_profile(event, &profile_out);

    // Analog sticks (HID convention: 0=up, 255=down - no inversion needed)
    xac_report.lx = profile_out.left_x;
    xac_report.ly = profile_out.left_y;
    xac_report.rx = profile_out.right_x;
    xac_report.ry = profile_out.right_y;

    // D-pad as hat switch
    xac_report.hat = convert_dpad_to_hat(buttons);

    // Buttons (12 total, split into low 4 bits and high 8 bits)
    uint16_t xac_buttons = 0;
    if (buttons & JP_BUTTON_B1) xac_buttons |= XAC_MASK_B1;  // A
    if (buttons & JP_BUTTON_B2) xac_buttons |= XAC_MASK_B2;  // B
    if (buttons & JP_BUTTON_B3) xac_buttons |= XAC_MASK_B3;  // X
    if (buttons & JP_BUTTON_B4) xac_buttons |= XAC_MASK_B4;  // Y
    if (buttons & JP_BUTTON_L1) xac_buttons |= XAC_MASK_L1;  // LB
    if (buttons & JP_BUTTON_R1) xac_buttons |= XAC_MASK_R1;  // RB
    if (buttons & JP_BUTTON_L2) xac_buttons |= XAC_MASK_L2;  // LT (digital)
    if (buttons & JP_BUTTON_R2) xac_buttons |= XAC_MASK_R2;  // RT (digital)
    if (buttons & JP_BUTTON_S1) xac_buttons |= XAC_MASK_S1;  // Back
    if (buttons & JP_BUTTON_S2) xac_buttons |= XAC_MASK_S2;  // Start
    if (buttons & JP_BUTTON_L3) xac_buttons |= XAC_MASK_L3;  // LS
    if (buttons & JP_BUTTON_R3) xac_buttons |= XAC_MASK_R3;  // RS

    xac_report.buttons_lo = xac_buttons & 0x0F;
    xac_report.buttons_hi = (xac_buttons >> 4) & 0xFF;

    return tud_hid_report(0, &xac_report, sizeof(xac_report));
}

bool usbd_send_report(uint8_t player_index)
{
    switch (output_mode) {
        case USB_OUTPUT_MODE_XBOX_ORIGINAL:
            return usbd_send_xid_report(player_index);
#if CFG_TUD_XINPUT
        case USB_OUTPUT_MODE_XINPUT:
            return usbd_send_xinput_report(player_index);
#endif
        case USB_OUTPUT_MODE_SWITCH:
            return usbd_send_switch_report(player_index);
        case USB_OUTPUT_MODE_PS3:
            return usbd_send_ps3_report(player_index);
        case USB_OUTPUT_MODE_PSCLASSIC:
            return usbd_send_psclassic_report(player_index);
        case USB_OUTPUT_MODE_PS4:
            return usbd_send_ps4_report(player_index);
        case USB_OUTPUT_MODE_XBONE:
            return usbd_send_xbone_report(player_index);
        case USB_OUTPUT_MODE_XAC:
            return usbd_send_xac_report(player_index);
        case USB_OUTPUT_MODE_HID:
        default:
            return usbd_send_hid_report(player_index);
    }
}

// Get rumble value from USB host (for feedback to input controllers)
static uint8_t usbd_get_rumble(void)
{
    switch (output_mode) {
        case USB_OUTPUT_MODE_XBOX_ORIGINAL: {
            // Xbox OG has two 16-bit motors - combine to single 8-bit value
            uint16_t max_rumble = (xid_rumble.rumble_l > xid_rumble.rumble_r)
                                  ? xid_rumble.rumble_l : xid_rumble.rumble_r;
            return (uint8_t)(max_rumble >> 8);  // Scale 0-65535 to 0-255
        }
#if CFG_TUD_XINPUT
        case USB_OUTPUT_MODE_XINPUT: {
            // XInput has two 8-bit motors - take the stronger one
            return (xinput_output.rumble_l > xinput_output.rumble_r)
                   ? xinput_output.rumble_l : xinput_output.rumble_r;
        }
#endif
        case USB_OUTPUT_MODE_PS3: {
            // PS3 has left (large) and right (small, on/off only) motors
            return (ps3_output.rumble_left_force > 0) ? ps3_output.rumble_left_force :
                   (ps3_output.rumble_right_on > 0) ? 0xFF : 0x00;
        }
        case USB_OUTPUT_MODE_PS4: {
            // PS4 has motor_left (large) and motor_right (small) 8-bit values
            return (ps4_output.motor_left > ps4_output.motor_right)
                   ? ps4_output.motor_left : ps4_output.motor_right;
        }
        default:
            // HID/Switch modes: no standard rumble protocol
            return 0;
    }
}

// ============================================================================
// OUTPUT INTERFACE
// ============================================================================

// Get feedback state with separate left/right rumble and LED data
static bool usbd_get_feedback(output_feedback_t* fb)
{
    if (!fb) return false;

    fb->rumble_left = 0;
    fb->rumble_right = 0;
    fb->led_player = 0;
    fb->led_r = fb->led_g = fb->led_b = 0;
    fb->dirty = false;

    switch (output_mode) {
        case USB_OUTPUT_MODE_XBOX_ORIGINAL:
            // Xbox OG has two 16-bit motors
            fb->rumble_left = (uint8_t)(xid_rumble.rumble_l >> 8);
            fb->rumble_right = (uint8_t)(xid_rumble.rumble_r >> 8);
            fb->dirty = true;
            return true;

#if CFG_TUD_XINPUT
        case USB_OUTPUT_MODE_XINPUT:
            // XInput has two 8-bit motors
            fb->rumble_left = xinput_output.rumble_l;
            fb->rumble_right = xinput_output.rumble_r;
            fb->dirty = true;
            return true;
#endif

        case USB_OUTPUT_MODE_PS3:
            if (!ps3_output_available) return false;
            // PS3: left is variable force, right is on/off only
            fb->rumble_left = ps3_output.rumble_left_force;
            fb->rumble_right = ps3_output.rumble_right_on ? 0xFF : 0x00;
            // PS3 LEDs: bitmap in leds_bitmap (LED_1=0x02, LED_2=0x04, etc.)
            if (ps3_output.leds_bitmap & 0x02) fb->led_player = 1;
            else if (ps3_output.leds_bitmap & 0x04) fb->led_player = 2;
            else if (ps3_output.leds_bitmap & 0x08) fb->led_player = 3;
            else if (ps3_output.leds_bitmap & 0x10) fb->led_player = 4;
            fb->dirty = true;
            return true;

        case USB_OUTPUT_MODE_PS4:
            if (!ps4_output_available) return false;
            // PS4 has two 8-bit motors and RGB lightbar
            fb->rumble_left = ps4_output.motor_left;
            fb->rumble_right = ps4_output.motor_right;
            fb->led_r = ps4_output.lightbar_red;
            fb->led_g = ps4_output.lightbar_green;
            fb->led_b = ps4_output.lightbar_blue;
            fb->dirty = true;
            return true;

        default:
            return false;
    }
}

const OutputInterface usbd_output_interface = {
    .name = "USB",
    .target = OUTPUT_TARGET_USB_DEVICE,
    .init = usbd_init,
    .task = usbd_task,
    .core1_task = NULL,  // Runs from core0 task - doesn't need dedicated core
    .get_feedback = usbd_get_feedback,
    .get_rumble = usbd_get_rumble,
    .get_player_led = NULL,
    .get_profile_count = NULL,
    .get_active_profile = NULL,
    .set_active_profile = NULL,
    .get_profile_name = NULL,
    .get_trigger_threshold = NULL,
};

// ============================================================================
// TINYUSB DEVICE CALLBACKS
// ============================================================================

// ============================================================================
// INTERFACE AND ENDPOINT NUMBERS
// ============================================================================

// Interface numbers
enum {
    ITF_NUM_HID = 0,
#if CFG_TUD_CDC >= 1
    ITF_NUM_CDC_0,        // CDC 0 control interface (data port)
    ITF_NUM_CDC_0_DATA,   // CDC 0 data interface
#endif
#if CFG_TUD_CDC >= 2
    ITF_NUM_CDC_1,        // CDC 1 control interface (debug port)
    ITF_NUM_CDC_1_DATA,   // CDC 1 data interface
#endif
    ITF_NUM_TOTAL
};

// Endpoint numbers
#define EPNUM_HID           0x81

#if CFG_TUD_CDC >= 1
#define EPNUM_CDC_0_NOTIF   0x82
#define EPNUM_CDC_0_OUT     0x03
#define EPNUM_CDC_0_IN      0x83
#endif

#if CFG_TUD_CDC >= 2
#define EPNUM_CDC_1_NOTIF   0x84
#define EPNUM_CDC_1_OUT     0x05
#define EPNUM_CDC_1_IN      0x85
#endif

// ============================================================================
// DEVICE DESCRIPTOR
// ============================================================================

// HID mode device descriptor (PS3-compatible DInput)
static const tusb_desc_device_t desc_device_hid = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,  // USB 2.0
#if CFG_TUD_CDC > 0
    // Use IAD for composite device with CDC
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,
#else
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
#endif
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_HID_VID,
    .idProduct          = USB_HID_PID,
    .bcdDevice          = USB_HID_BCD,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01
};

uint8_t const *tud_descriptor_device_cb(void)
{
    switch (output_mode) {
        case USB_OUTPUT_MODE_XBOX_ORIGINAL:
            return (uint8_t const *)&xbox_og_device_descriptor;
        case USB_OUTPUT_MODE_XINPUT:
            return (uint8_t const *)&xinput_device_descriptor;
        case USB_OUTPUT_MODE_SWITCH:
            return (uint8_t const *)&switch_device_descriptor;
        case USB_OUTPUT_MODE_PS3:
            return (uint8_t const *)&ps3_device_descriptor;
        case USB_OUTPUT_MODE_PSCLASSIC:
            return (uint8_t const *)&psclassic_device_descriptor;
        case USB_OUTPUT_MODE_PS4:
            return (uint8_t const *)&ps4_device_descriptor;
        case USB_OUTPUT_MODE_XBONE:
            return (uint8_t const *)&xbone_device_descriptor;
        case USB_OUTPUT_MODE_XAC:
            return (uint8_t const *)&xac_device_descriptor;
        case USB_OUTPUT_MODE_HID:
        default:
            return (uint8_t const *)&desc_device_hid;
    }
}

// ============================================================================
// CONFIGURATION DESCRIPTOR
// ============================================================================

// HID mode configuration descriptor
#define CONFIG_TOTAL_LEN_HID (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + (CFG_TUD_CDC * TUD_CDC_DESC_LEN))

static const uint8_t desc_configuration_hid[] = {
    // Config: bus powered, max 100mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN_HID, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface 0: HID gamepad
    TUD_HID_DESCRIPTOR(ITF_NUM_HID, 0, HID_ITF_PROTOCOL_NONE, sizeof(hid_report_descriptor), EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE, 1),

#if CFG_TUD_CDC >= 1
    // CDC 0: Data port (commands, config)
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_0, 4, EPNUM_CDC_0_NOTIF, 8, EPNUM_CDC_0_OUT, EPNUM_CDC_0_IN, 64),
#endif

#if CFG_TUD_CDC >= 2
    // CDC 1: Debug port (logging)
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_1, 5, EPNUM_CDC_1_NOTIF, 8, EPNUM_CDC_1_OUT, EPNUM_CDC_1_IN, 64),
#endif
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index)
{
    (void)index;
    switch (output_mode) {
        case USB_OUTPUT_MODE_XBOX_ORIGINAL:
            return xbox_og_config_descriptor;
        case USB_OUTPUT_MODE_XINPUT:
            return xinput_config_descriptor;
        case USB_OUTPUT_MODE_SWITCH:
            return switch_config_descriptor;
        case USB_OUTPUT_MODE_PS3:
            return ps3_config_descriptor;
        case USB_OUTPUT_MODE_PSCLASSIC:
            return psclassic_config_descriptor;
        case USB_OUTPUT_MODE_PS4:
            return ps4_config_descriptor;
        case USB_OUTPUT_MODE_XBONE:
            return xbone_config_descriptor;
        case USB_OUTPUT_MODE_XAC:
            return xac_config_descriptor;
        case USB_OUTPUT_MODE_HID:
        default:
            return desc_configuration_hid;
    }
}

// ============================================================================
// STRING DESCRIPTORS
// ============================================================================

// String descriptor indices
enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
#if CFG_TUD_CDC >= 1
    STRID_CDC_DATA,
#endif
#if CFG_TUD_CDC >= 2
    STRID_CDC_DEBUG,
#endif
    STRID_COUNT
};

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid)
{
    (void)langid;

    // Xbox OG has no string descriptors
    if (output_mode == USB_OUTPUT_MODE_XBOX_ORIGINAL) {
        return NULL;
    }

    // Xbox One uses custom string handling via vendor control requests
    if (output_mode == USB_OUTPUT_MODE_XBONE) {
        // Return basic descriptors - specialized ones handled in vendor callback
        static uint16_t _xbone_str[32];
        uint8_t xbone_chr_count;
        const char* xbone_str = NULL;

        switch (index) {
            case 0:  // Language ID
                _xbone_str[1] = 0x0409;
                xbone_chr_count = 1;
                break;
            case 1:  // Manufacturer
                xbone_str = XBONE_MANUFACTURER;
                break;
            case 2:  // Product
                xbone_str = XBONE_PRODUCT;
                break;
            case 3:  // Serial
                xbone_str = usb_serial_str;
                break;
            default:
                return NULL;
        }

        if (xbone_str) {
            xbone_chr_count = strlen(xbone_str);
            if (xbone_chr_count > 31) xbone_chr_count = 31;
            for (uint8_t i = 0; i < xbone_chr_count; i++) {
                _xbone_str[1 + i] = xbone_str[i];
            }
        }
        _xbone_str[0] = (TUSB_DESC_STRING << 8) | (2 * xbone_chr_count + 2);
        return _xbone_str;
    }

    static uint16_t _desc_str[32];
    const char *str = NULL;
    uint8_t chr_count;

    switch (index) {
        case STRID_LANGID:
            _desc_str[1] = 0x0409;  // English
            chr_count = 1;
            break;
        case STRID_MANUFACTURER:
            // Mode-specific manufacturer
            if (output_mode == USB_OUTPUT_MODE_XINPUT) {
                str = XINPUT_MANUFACTURER;
            } else if (output_mode == USB_OUTPUT_MODE_SWITCH) {
                str = SWITCH_MANUFACTURER;
            } else if (output_mode == USB_OUTPUT_MODE_PS3) {
                str = PS3_MANUFACTURER;
            } else if (output_mode == USB_OUTPUT_MODE_PSCLASSIC) {
                str = PSCLASSIC_MANUFACTURER;
            } else if (output_mode == USB_OUTPUT_MODE_PS4) {
                str = PS4_MANUFACTURER;
            } else if (output_mode == USB_OUTPUT_MODE_XAC) {
                str = XAC_MANUFACTURER;
            } else {
                str = USB_HID_MANUFACTURER;
            }
            break;
        case STRID_PRODUCT:
            // Mode-specific product
            if (output_mode == USB_OUTPUT_MODE_XINPUT) {
                str = XINPUT_PRODUCT;
            } else if (output_mode == USB_OUTPUT_MODE_SWITCH) {
                str = SWITCH_PRODUCT;
            } else if (output_mode == USB_OUTPUT_MODE_PS3) {
                str = PS3_PRODUCT;
            } else if (output_mode == USB_OUTPUT_MODE_PSCLASSIC) {
                str = PSCLASSIC_PRODUCT;
            } else if (output_mode == USB_OUTPUT_MODE_PS4) {
                str = PS4_PRODUCT;
            } else if (output_mode == USB_OUTPUT_MODE_XAC) {
                str = XAC_PRODUCT;
            } else {
                str = USB_HID_PRODUCT;
            }
            break;
        case STRID_SERIAL:
            str = usb_serial_str;  // Dynamic from board unique ID
            break;
#if CFG_TUD_CDC >= 1
        case STRID_CDC_DATA:
            str = "Joypad Data";
            break;
#endif
#if CFG_TUD_CDC >= 2
        case STRID_CDC_DEBUG:
            str = "Joypad Debug";
            break;
#endif
        default:
            return NULL;
    }

    if (str) {
        chr_count = strlen(str);
        if (chr_count > 31) chr_count = 31;
        for (uint8_t i = 0; i < chr_count; i++) {
            _desc_str[1 + i] = str[i];
        }
    }

    // First byte is length (in bytes), second byte is descriptor type
    _desc_str[0] = (TUSB_DESC_STRING << 8) | (2 * chr_count + 2);
    return _desc_str;
}

// HID Callbacks
uint8_t const *tud_hid_descriptor_report_cb(uint8_t itf)
{
    (void)itf;
    // Return mode-specific HID report descriptor
    if (output_mode == USB_OUTPUT_MODE_SWITCH) {
        return switch_report_descriptor;
    }
    if (output_mode == USB_OUTPUT_MODE_PS3) {
        return ps3_report_descriptor;
    }
    if (output_mode == USB_OUTPUT_MODE_PSCLASSIC) {
        return psclassic_report_descriptor;
    }
    if (output_mode == USB_OUTPUT_MODE_PS4) {
        return ps4_report_descriptor;
    }
    if (output_mode == USB_OUTPUT_MODE_XAC) {
        return xac_report_descriptor;
    }
    return hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    (void)itf;

    // PS3 feature reports
    if (output_mode == USB_OUTPUT_MODE_PS3 && report_type == HID_REPORT_TYPE_FEATURE) {
        uint16_t len = 0;
        switch (report_id) {
            case PS3_REPORT_ID_FEATURE_01:
                len = sizeof(ps3_feature_01);
                if (reqlen < len) len = reqlen;
                memcpy(buffer, ps3_feature_01, len);
                return len;
            case PS3_REPORT_ID_PAIRING: {
                // Pairing info (0xF2) - return dummy BT addresses
                static ps3_pairing_info_t pairing = {0};
                len = sizeof(pairing);
                if (reqlen < len) len = reqlen;
                memcpy(buffer, &pairing, len);
                return len;
            }
            case PS3_REPORT_ID_FEATURE_EF:
                len = sizeof(ps3_feature_ef);
                if (reqlen < len) len = reqlen;
                memcpy(buffer, ps3_feature_ef, len);
                return len;
            case PS3_REPORT_ID_FEATURE_F7:
                len = sizeof(ps3_feature_f7);
                if (reqlen < len) len = reqlen;
                memcpy(buffer, ps3_feature_f7, len);
                return len;
            case PS3_REPORT_ID_FEATURE_F8:
                len = sizeof(ps3_feature_f8);
                if (reqlen < len) len = reqlen;
                memcpy(buffer, ps3_feature_f8, len);
                return len;
            default:
                break;
        }
    }

    // PS4 feature reports (auth passthrough to connected DS4)
    if (output_mode == USB_OUTPUT_MODE_PS4 && report_type == HID_REPORT_TYPE_FEATURE) {
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
                printf("[USBD] PS4 GET_REPORT 0xF1 (signature)\n");
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
                printf("[USBD] PS4 GET_REPORT 0xF2 (status)\n");
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
                printf("[USBD] PS4 GET_REPORT 0xF3 (reset)\n");
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
                break;
        }
    }

    // Default: return current input report
    (void)report_id;
    (void)report_type;
    uint16_t len = sizeof(joypad_hid_report_t);
    if (reqlen < len) len = reqlen;
    memcpy(buffer, &hid_report, len);
    return len;
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
    (void)itf;
    (void)report_type;

    // PS3 output report (rumble/LED)
    // Note: Some hosts (like WebHID) may include report ID in buffer, some don't
    // Check if buffer starts with report ID 0x01 and skip it if so
    if (output_mode == USB_OUTPUT_MODE_PS3) {
        const uint8_t* data = buffer;
        uint16_t len = bufsize;

        // If buffer is 49 bytes and starts with 0x01, it includes report ID - skip it
        if (bufsize == 49 && buffer[0] == 0x01) {
            data = buffer + 1;
            len = 48;
        }

        if (len >= sizeof(ps3_out_report_t)) {
            memcpy(&ps3_output, data, sizeof(ps3_out_report_t));
            ps3_output_available = true;
            return;
        }
    }

    // PS4 output report (rumble/LED) - Report ID 5
    if (output_mode == USB_OUTPUT_MODE_PS4 && report_id == PS4_REPORT_ID_OUTPUT && bufsize >= sizeof(ps4_out_report_t)) {
        memcpy(&ps4_output, buffer, sizeof(ps4_out_report_t));
        ps4_output_available = true;
        return;
    }

    // PS4 auth feature reports
#ifndef DISABLE_USB_HOST
    if (output_mode == USB_OUTPUT_MODE_PS4 && report_type == HID_REPORT_TYPE_FEATURE) {
        switch (report_id) {
            case PS4_REPORT_ID_AUTH_PAYLOAD:    // 0xF0 - Nonce from console
                // Forward nonce to connected DS4
                printf("[USBD] PS4 SET_REPORT 0xF0 (nonce), bufsize=%d, ds4_avail=%d\n",
                       bufsize, ds4_auth_is_available());
                if (ds4_auth_is_available()) {
                    ds4_auth_send_nonce(buffer, bufsize);
                }
                return;

            case PS4_REPORT_ID_AUTH_RESET:      // 0xF3 - Reset auth
                printf("[USBD] PS4 SET_REPORT 0xF3 (reset auth)\n");
                ds4_auth_reset();
                return;

            default:
                break;
        }
    }
#endif

    (void)report_id;
    (void)buffer;
    (void)bufsize;
}

// ============================================================================
// CUSTOM CLASS DRIVER REGISTRATION
// ============================================================================

// Register custom class drivers for vendor-specific modes
usbd_class_driver_t const* usbd_app_driver_get_cb(uint8_t* driver_count)
{
    switch (output_mode) {
        case USB_OUTPUT_MODE_XBOX_ORIGINAL:
            *driver_count = 1;
            return tud_xid_class_driver();

#if CFG_TUD_XINPUT
        case USB_OUTPUT_MODE_XINPUT:
            *driver_count = 1;
            return tud_xinput_class_driver();
#endif

        case USB_OUTPUT_MODE_XBONE:
            *driver_count = 1;
            return tud_xbone_class_driver();

        default:
            // HID/Switch modes use built-in HID class driver
            *driver_count = 0;
            return NULL;
    }
}

// Vendor control request callback (for Xbox One Windows OS descriptors)
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage,
                                 tusb_control_request_t const* request)
{
    if (output_mode == USB_OUTPUT_MODE_XBONE) {
        return tud_xbone_vendor_control_xfer_cb(rhport, stage, request);
    }
    return true;  // Accept by default for other modes
}
