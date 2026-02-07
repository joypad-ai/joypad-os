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
#include "usbd_mode.h"
#include "descriptors/hid_descriptors.h"
#include "descriptors/sinput_descriptors.h"
#include "descriptors/xbox_og_descriptors.h"
#include "descriptors/xinput_descriptors.h"
#include "descriptors/switch_descriptors.h"
#include "descriptors/ps3_descriptors.h"
#include "descriptors/psclassic_descriptors.h"
#include "descriptors/ps4_descriptors.h"
#include "descriptors/xbone_descriptors.h"
#include "descriptors/xac_descriptors.h"
#include "descriptors/kbmouse_descriptors.h"
#include "descriptors/gc_adapter_descriptors.h"
#include "kbmouse/kbmouse.h"
#include "drivers/tud_xid.h"
#include "drivers/tud_xinput.h"
#include "drivers/tud_xbone.h"
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

// XID state is now in modes/xid_mode.c

// XInput state is now in modes/xinput_mode.c

// Switch state is now in modes/switch_mode.c

// PS3 state is now in modes/ps3_mode.c

// PSClassic state is now in modes/psclassic_mode.c
// PS4 state is now in modes/ps4_mode.c
// XID state is now in modes/xid_mode.c
// Xbox One state is now in modes/xbone_mode.c
// XAC state is now in modes/xac_mode.c
// KB/Mouse state is now in modes/kbmouse_mode.c
// GC Adapter state is now in modes/gc_adapter_mode.c

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
static usb_output_mode_t output_mode = USB_OUTPUT_MODE_SINPUT;
static flash_t flash_settings;

// Mode names for display
static const char* mode_names[] = {
    [USB_OUTPUT_MODE_HID] = "DInput",
    [USB_OUTPUT_MODE_SINPUT] = "SInput",
    [USB_OUTPUT_MODE_XBOX_ORIGINAL] = "Xbox Original (XID)",
    [USB_OUTPUT_MODE_XINPUT] = "XInput",
    [USB_OUTPUT_MODE_PS3] = "PS3",
    [USB_OUTPUT_MODE_PS4] = "PS4",
    [USB_OUTPUT_MODE_SWITCH] = "Switch",
    [USB_OUTPUT_MODE_PSCLASSIC] = "PS Classic",
    [USB_OUTPUT_MODE_XBONE] = "Xbox One",
    [USB_OUTPUT_MODE_XAC] = "XAC Compat",
    [USB_OUTPUT_MODE_KEYBOARD_MOUSE] = "KB/Mouse",
    [USB_OUTPUT_MODE_GC_ADAPTER] = "GC Adapter",
};

// ============================================================================
// MODE REGISTRY
// ============================================================================

// Mode registry array (populated by usbd_register_modes)
const usbd_mode_t* usbd_modes[USB_OUTPUT_MODE_COUNT] = {0};

// Current active mode pointer
static const usbd_mode_t* current_mode = NULL;

void usbd_register_modes(void)
{
    // Register all implemented modes
    usbd_modes[USB_OUTPUT_MODE_HID] = &hid_mode;
    usbd_modes[USB_OUTPUT_MODE_SINPUT] = &sinput_mode;
#if CFG_TUD_XINPUT
    usbd_modes[USB_OUTPUT_MODE_XINPUT] = &xinput_mode;
#endif
    usbd_modes[USB_OUTPUT_MODE_SWITCH] = &switch_mode;
    usbd_modes[USB_OUTPUT_MODE_PS3] = &ps3_mode;
    usbd_modes[USB_OUTPUT_MODE_PSCLASSIC] = &psclassic_mode;
    usbd_modes[USB_OUTPUT_MODE_PS4] = &ps4_mode;
    usbd_modes[USB_OUTPUT_MODE_XBOX_ORIGINAL] = &xid_mode;
    usbd_modes[USB_OUTPUT_MODE_XBONE] = &xbone_mode;
    usbd_modes[USB_OUTPUT_MODE_XAC] = &xac_mode;
    usbd_modes[USB_OUTPUT_MODE_KEYBOARD_MOUSE] = &kbmouse_mode;
#if CFG_TUD_GC_ADAPTER
    usbd_modes[USB_OUTPUT_MODE_GC_ADAPTER] = &gc_adapter_mode;
#endif
}

const usbd_mode_t* usbd_get_current_mode(void)
{
    return current_mode;
}

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
                  event->analog[ANALOG_RZ],
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

            // Apply SOCD cleaning
            if (custom->socd_mode > 0 && custom->socd_mode <= 3) {
                profile_out->buttons = apply_socd(profile_out->buttons,
                    (socd_mode_t)custom->socd_mode, 0);
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
    uint8_t output_axes[7] = {
        profile_out->left_x, profile_out->left_y,
        profile_out->right_x, profile_out->right_y,
        profile_out->l2_analog, profile_out->r2_analog,
        profile_out->rz_analog
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

    // Supported modes: SInput, HID, Xbox OG, XInput, PS3, PS4, Switch, PS Classic, Xbox One, XAC, KB/Mouse, GC Adapter
    if (mode != USB_OUTPUT_MODE_SINPUT &&
        mode != USB_OUTPUT_MODE_HID &&
        mode != USB_OUTPUT_MODE_XBOX_ORIGINAL &&
        mode != USB_OUTPUT_MODE_XINPUT &&
        mode != USB_OUTPUT_MODE_PS3 &&
        mode != USB_OUTPUT_MODE_PS4 &&
        mode != USB_OUTPUT_MODE_SWITCH &&
        mode != USB_OUTPUT_MODE_PSCLASSIC &&
        mode != USB_OUTPUT_MODE_XBONE &&
        mode != USB_OUTPUT_MODE_XAC &&
        mode != USB_OUTPUT_MODE_KEYBOARD_MOUSE &&
        mode != USB_OUTPUT_MODE_GC_ADAPTER) {
        printf("[usbd] Mode %d not yet supported\n", mode);
        return false;
    }

    if (mode == output_mode) {
        return false;  // Same mode, no change needed
    }

    printf("[usbd] Changing mode from %s to %s\n",
           mode_names[output_mode], mode_names[mode]);
    flush_debug_output();

    // Fast switch: SInput <-> KB/Mouse share the same USB descriptor (composite device)
    // No re-enumeration needed — just switch the mode logic
    bool is_sinput_family_old = (output_mode == USB_OUTPUT_MODE_SINPUT ||
                                  output_mode == USB_OUTPUT_MODE_KEYBOARD_MOUSE);
    bool is_sinput_family_new = (mode == USB_OUTPUT_MODE_SINPUT ||
                                  mode == USB_OUTPUT_MODE_KEYBOARD_MOUSE);
    if (is_sinput_family_old && is_sinput_family_new) {
        printf("[usbd] Fast switch (same USB descriptor)\n");
        flush_debug_output();

        output_mode = mode;
        flash_settings.usb_output_mode = (uint8_t)mode;
        flash_save_force(&flash_settings);

        // Re-init the new mode
        const usbd_mode_t* new_mode = usbd_modes[mode];
        if (new_mode && new_mode->init) {
            new_mode->init();
        }
        current_mode = new_mode;

        printf("[usbd] Fast switch complete: %s\n", mode_names[mode]);
        return true;
    }

    // Save mode to flash immediately (we're about to reset)
    printf("[usbd] Setting flash_settings.usb_output_mode = %d\n", mode);
    flush_debug_output();
    flash_settings.usb_output_mode = (uint8_t)mode;
    printf("[usbd] Calling flash_save_force (reset imminent)...\n");
    flush_debug_output();
    flash_save_force(&flash_settings);
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
    // Cycle through common modes: SInput → XInput → PS3 → PS4 → Switch → KB/Mouse → SInput
    // (Skip less common: DInput, PS Classic, Xbox Original, Xbox One, XAC)
    switch (output_mode) {
        case USB_OUTPUT_MODE_SINPUT:
            return USB_OUTPUT_MODE_XINPUT;
        case USB_OUTPUT_MODE_XINPUT:
            return USB_OUTPUT_MODE_PS3;
        case USB_OUTPUT_MODE_PS3:
            return USB_OUTPUT_MODE_PS4;
        case USB_OUTPUT_MODE_PS4:
            return USB_OUTPUT_MODE_SWITCH;
        case USB_OUTPUT_MODE_SWITCH:
            return USB_OUTPUT_MODE_KEYBOARD_MOUSE;
        case USB_OUTPUT_MODE_KEYBOARD_MOUSE:
        default:
            return USB_OUTPUT_MODE_SINPUT;
    }
}

bool usbd_reset_to_hid(void)
{
    // Reset to SInput (the new default, replacing DInput)
    if (output_mode != USB_OUTPUT_MODE_SINPUT) {
        usbd_set_mode(USB_OUTPUT_MODE_SINPUT);
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

    // Register all mode implementations
    usbd_register_modes();

    // Initialize and load settings from flash
    flash_init();
    printf("[usbd] Loading settings from flash...\n");
    if (flash_load(&flash_settings)) {
        printf("[usbd] Flash load success! usb_output_mode=%d, active_profile=%d\n",
               flash_settings.usb_output_mode, flash_settings.active_profile_index);
        // Validate loaded mode
        if (flash_settings.usb_output_mode < USB_OUTPUT_MODE_COUNT) {
            // Only accept supported modes
            if (flash_settings.usb_output_mode == USB_OUTPUT_MODE_SINPUT ||
                flash_settings.usb_output_mode == USB_OUTPUT_MODE_XBOX_ORIGINAL ||
                flash_settings.usb_output_mode == USB_OUTPUT_MODE_XINPUT ||
                flash_settings.usb_output_mode == USB_OUTPUT_MODE_PS3 ||
                flash_settings.usb_output_mode == USB_OUTPUT_MODE_PS4 ||
                flash_settings.usb_output_mode == USB_OUTPUT_MODE_SWITCH ||
                flash_settings.usb_output_mode == USB_OUTPUT_MODE_PSCLASSIC ||
                flash_settings.usb_output_mode == USB_OUTPUT_MODE_XBONE ||
                flash_settings.usb_output_mode == USB_OUTPUT_MODE_XAC ||
                flash_settings.usb_output_mode == USB_OUTPUT_MODE_KEYBOARD_MOUSE ||
                flash_settings.usb_output_mode == USB_OUTPUT_MODE_GC_ADAPTER) {
                output_mode = (usb_output_mode_t)flash_settings.usb_output_mode;
                printf("[usbd] Loaded mode from flash: %s\n", mode_names[output_mode]);
            } else if (flash_settings.usb_output_mode == USB_OUTPUT_MODE_HID) {
                // Migrate DInput to SInput (DInput is deprecated)
                output_mode = USB_OUTPUT_MODE_SINPUT;
                printf("[usbd] Migrating DInput to SInput\n");
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
            // XID mode: delegate to mode interface
            if (usbd_modes[USB_OUTPUT_MODE_XBOX_ORIGINAL] && usbd_modes[USB_OUTPUT_MODE_XBOX_ORIGINAL]->init) {
                usbd_modes[USB_OUTPUT_MODE_XBOX_ORIGINAL]->init();
            }
            break;

        case USB_OUTPUT_MODE_XINPUT:
            // XInput mode: delegate to mode interface
            if (usbd_modes[USB_OUTPUT_MODE_XINPUT] && usbd_modes[USB_OUTPUT_MODE_XINPUT]->init) {
                usbd_modes[USB_OUTPUT_MODE_XINPUT]->init();
            }
            break;

        case USB_OUTPUT_MODE_SWITCH:
            // Switch mode: delegate to mode interface
            if (usbd_modes[USB_OUTPUT_MODE_SWITCH] && usbd_modes[USB_OUTPUT_MODE_SWITCH]->init) {
                usbd_modes[USB_OUTPUT_MODE_SWITCH]->init();
            }
            break;

        case USB_OUTPUT_MODE_PS3:
            // PS3 mode: delegate to mode interface
            if (usbd_modes[USB_OUTPUT_MODE_PS3] && usbd_modes[USB_OUTPUT_MODE_PS3]->init) {
                usbd_modes[USB_OUTPUT_MODE_PS3]->init();
            }
            break;

        case USB_OUTPUT_MODE_PSCLASSIC:
            // PSClassic mode: delegate to mode interface
            if (usbd_modes[USB_OUTPUT_MODE_PSCLASSIC] && usbd_modes[USB_OUTPUT_MODE_PSCLASSIC]->init) {
                usbd_modes[USB_OUTPUT_MODE_PSCLASSIC]->init();
            }
            break;

        case USB_OUTPUT_MODE_PS4:
            // PS4 mode: delegate to mode interface
            if (usbd_modes[USB_OUTPUT_MODE_PS4] && usbd_modes[USB_OUTPUT_MODE_PS4]->init) {
                usbd_modes[USB_OUTPUT_MODE_PS4]->init();
            }
            break;

        case USB_OUTPUT_MODE_XBONE:
            // Xbox One mode: delegate to mode interface
            if (usbd_modes[USB_OUTPUT_MODE_XBONE] && usbd_modes[USB_OUTPUT_MODE_XBONE]->init) {
                usbd_modes[USB_OUTPUT_MODE_XBONE]->init();
            }
            break;

        case USB_OUTPUT_MODE_XAC:
            // XAC mode: delegate to mode interface
            if (usbd_modes[USB_OUTPUT_MODE_XAC] && usbd_modes[USB_OUTPUT_MODE_XAC]->init) {
                usbd_modes[USB_OUTPUT_MODE_XAC]->init();
            }
            break;

        case USB_OUTPUT_MODE_KEYBOARD_MOUSE:
            // KB/Mouse mode: delegate to mode interface
            if (usbd_modes[USB_OUTPUT_MODE_KEYBOARD_MOUSE] && usbd_modes[USB_OUTPUT_MODE_KEYBOARD_MOUSE]->init) {
                usbd_modes[USB_OUTPUT_MODE_KEYBOARD_MOUSE]->init();
            }
            break;

        case USB_OUTPUT_MODE_GC_ADAPTER:
            // GC Adapter mode: delegate to mode interface
#if CFG_TUD_GC_ADAPTER
            if (usbd_modes[USB_OUTPUT_MODE_GC_ADAPTER] && usbd_modes[USB_OUTPUT_MODE_GC_ADAPTER]->init) {
                usbd_modes[USB_OUTPUT_MODE_GC_ADAPTER]->init();
            }
#endif
            break;

        case USB_OUTPUT_MODE_HID:
            // Initialize HID mode via mode interface
            if (usbd_modes[USB_OUTPUT_MODE_HID] && usbd_modes[USB_OUTPUT_MODE_HID]->init) {
                usbd_modes[USB_OUTPUT_MODE_HID]->init();
            }
            break;

        case USB_OUTPUT_MODE_SINPUT:
        default:
            // Initialize SInput mode via mode interface (new default)
            if (usbd_modes[USB_OUTPUT_MODE_SINPUT] && usbd_modes[USB_OUTPUT_MODE_SINPUT]->init) {
                usbd_modes[USB_OUTPUT_MODE_SINPUT]->init();
            }
            break;
    }

    // Set current mode pointer for dispatch
    current_mode = usbd_modes[output_mode];

    // Initialize CDC subsystem (for SInput, HID, Switch, and KB/Mouse modes)
    if (output_mode == USB_OUTPUT_MODE_SINPUT || output_mode == USB_OUTPUT_MODE_HID ||
        output_mode == USB_OUTPUT_MODE_SWITCH || output_mode == USB_OUTPUT_MODE_KEYBOARD_MOUSE) {
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
        case USB_OUTPUT_MODE_XBOX_ORIGINAL: {
            // XID mode: delegate to mode interface
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_XBOX_ORIGINAL];
            if (mode) {
                if (mode->task) mode->task();
                if (mode->is_ready && mode->is_ready()) {
                    usbd_send_report(0);
                }
            }
            break;
        }

#if CFG_TUD_XINPUT
        case USB_OUTPUT_MODE_XINPUT: {
            // XInput mode: delegate to mode interface
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_XINPUT];
            if (mode) {
                if (mode->task) mode->task();
                if (mode->is_ready && mode->is_ready()) {
                    usbd_send_report(0);
                }
            }
            break;
        }
#endif

        case USB_OUTPUT_MODE_SWITCH: {
            // Switch mode: process CDC tasks, delegate to mode interface
            cdc_task();
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_SWITCH];
            if (mode && mode->is_ready && mode->is_ready()) {
                usbd_send_report(0);
            }
            break;
        }

        case USB_OUTPUT_MODE_PS3: {
            // PS3 mode: delegate to mode interface
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_PS3];
            if (mode && mode->is_ready && mode->is_ready()) {
                usbd_send_report(0);
            }
            break;
        }

        case USB_OUTPUT_MODE_PSCLASSIC: {
            // PSClassic mode: delegate to mode interface
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_PSCLASSIC];
            if (mode && mode->is_ready && mode->is_ready()) {
                usbd_send_report(0);
            }
            break;
        }

        case USB_OUTPUT_MODE_PS4:
            // PS4 mode: send HID report (no CDC)
            if (tud_hid_ready()) {
                usbd_send_report(0);
            }
            break;

        case USB_OUTPUT_MODE_XBONE: {
            // Xbox One mode: delegate to mode interface
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_XBONE];
            if (mode) {
                if (mode->task) mode->task();
                if (mode->is_ready && mode->is_ready()) {
                    usbd_send_report(0);
                }
            }
            break;
        }

        case USB_OUTPUT_MODE_XAC: {
            // XAC mode: delegate to mode interface
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_XAC];
            if (mode && mode->is_ready && mode->is_ready()) {
                usbd_send_report(0);
            }
            break;
        }

        case USB_OUTPUT_MODE_KEYBOARD_MOUSE: {
            // KB/Mouse mode: process CDC tasks, delegate to mode interface
            cdc_task();
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_KEYBOARD_MOUSE];
            if (mode && mode->is_ready && mode->is_ready()) {
                usbd_send_report(0);
            }
            break;
        }

#if CFG_TUD_GC_ADAPTER
        case USB_OUTPUT_MODE_GC_ADAPTER: {
            // GC Adapter mode: delegate to mode interface
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_GC_ADAPTER];
            if (mode) {
                if (mode->task) mode->task();
                if (mode->is_ready && mode->is_ready()) {
                    usbd_send_report(0);
                }
            }
            break;
        }
#endif

        case USB_OUTPUT_MODE_HID:
            // HID mode: process CDC tasks
            cdc_task();
            // Send HID report if device is ready
            if (tud_hid_ready()) {
                usbd_send_report(0);
            }
            break;

        case USB_OUTPUT_MODE_SINPUT:
        default: {
            // SInput mode: process CDC tasks
            cdc_task();
            // Run mode task (handles feature response)
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_SINPUT];
            if (mode && mode->task) mode->task();
            // Send SInput report if device is ready
            if (tud_hid_ready()) {
                usbd_send_report(0);
            }
            break;
        }
    }
}

// Send XID report - delegates to mode interface
static bool usbd_send_xid_report(uint8_t player_index)
{
    const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_XBOX_ORIGINAL];
    if (!mode || !mode->send_report) return false;
    if (mode->is_ready && !mode->is_ready()) return false;

    // Check for pending event
    if (player_index >= USB_MAX_PLAYERS || !pending_flags[player_index]) {
        return false;
    }

    const input_event_t* event = &pending_events[player_index];
    pending_flags[player_index] = false;

    // Apply profile
    profile_output_t profile_out;
    uint32_t processed_buttons = apply_usbd_profile(event, &profile_out);

    return mode->send_report(player_index, event, &profile_out, processed_buttons);
}

// Send HID report (DInput mode) - uses mode interface
static bool usbd_send_hid_report(uint8_t player_index)
{
    // Use mode interface if available
    const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_HID];
    if (!mode || !mode->send_report) {
        return false;
    }

    // Check ready via mode interface
    if (mode->is_ready && !mode->is_ready()) {
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

    // Delegate to mode implementation
    return mode->send_report(player_index, event, &profile_out, processed_buttons);
}

// Send SInput report - uses mode interface
static bool usbd_send_sinput_report(uint8_t player_index)
{
    // Use mode interface if available
    const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_SINPUT];
    if (!mode || !mode->send_report) {
        return false;
    }

    // Check ready via mode interface
    if (mode->is_ready && !mode->is_ready()) {
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

    // Delegate to mode implementation
    return mode->send_report(player_index, event, &profile_out, processed_buttons);
}

#if CFG_TUD_XINPUT
// Send XInput report (Xbox 360 mode) - uses mode interface
static bool usbd_send_xinput_report(uint8_t player_index)
{
    // Use mode interface
    const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_XINPUT];
    if (!mode || !mode->send_report) {
        return false;
    }

    // Check ready via mode interface
    if (mode->is_ready && !mode->is_ready()) {
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

    // Delegate to mode implementation
    return mode->send_report(player_index, event, &profile_out, processed_buttons);
}
#endif

// Send Switch report (Nintendo Switch mode) - uses mode interface
static bool usbd_send_switch_report(uint8_t player_index)
{
    // Use mode interface
    const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_SWITCH];
    if (!mode || !mode->send_report) {
        return false;
    }

    // Check ready via mode interface
    if (mode->is_ready && !mode->is_ready()) {
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

    // Delegate to mode implementation
    return mode->send_report(player_index, event, &profile_out, processed_buttons);
}

// Send PS3 report (PlayStation 3 DualShock 3 mode) - uses mode interface
static bool usbd_send_ps3_report(uint8_t player_index)
{
    // Use mode interface
    const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_PS3];
    if (!mode || !mode->send_report) {
        return false;
    }

    // Check ready via mode interface
    if (mode->is_ready && !mode->is_ready()) {
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

    // Delegate to mode implementation
    return mode->send_report(player_index, event, &profile_out, processed_buttons);
}

// Send PS Classic report - uses mode interface
static bool usbd_send_psclassic_report(uint8_t player_index)
{
    // Use mode interface
    const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_PSCLASSIC];
    if (!mode || !mode->send_report) {
        return false;
    }

    // Check ready via mode interface
    if (mode->is_ready && !mode->is_ready()) {
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

    // Delegate to mode implementation
    return mode->send_report(player_index, event, &profile_out, processed_buttons);
}

// Send PS4 report - delegates to mode interface
static bool usbd_send_ps4_report(uint8_t player_index)
{
    const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_PS4];
    if (!mode || !mode->send_report) return false;
    if (mode->is_ready && !mode->is_ready()) return false;

    // Check for pending event
    if (player_index >= USB_MAX_PLAYERS || !pending_flags[player_index]) {
        return false;
    }

    const input_event_t* event = &pending_events[player_index];
    pending_flags[player_index] = false;

    // Apply profile
    profile_output_t profile_out;
    uint32_t processed_buttons = apply_usbd_profile(event, &profile_out);

    return mode->send_report(player_index, event, &profile_out, processed_buttons);
}

// Send Xbox One report - delegates to mode interface
static bool usbd_send_xbone_report(uint8_t player_index)
{
    const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_XBONE];
    if (!mode || !mode->send_report) return false;
    if (mode->is_ready && !mode->is_ready()) return false;

    // Check for pending event
    if (player_index >= USB_MAX_PLAYERS || !pending_flags[player_index]) {
        return false;
    }

    const input_event_t* event = &pending_events[player_index];
    pending_flags[player_index] = false;

    // Apply profile
    profile_output_t profile_out;
    uint32_t processed_buttons = apply_usbd_profile(event, &profile_out);

    return mode->send_report(player_index, event, &profile_out, processed_buttons);
}

// Send XAC report - delegates to mode interface
static bool usbd_send_xac_report(uint8_t player_index)
{
    const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_XAC];
    if (!mode || !mode->send_report) return false;
    if (mode->is_ready && !mode->is_ready()) return false;

    // Check for pending event
    if (player_index >= USB_MAX_PLAYERS || !pending_flags[player_index]) {
        return false;
    }

    const input_event_t* event = &pending_events[player_index];
    pending_flags[player_index] = false;

    // Apply profile
    profile_output_t profile_out;
    uint32_t processed_buttons = apply_usbd_profile(event, &profile_out);

    return mode->send_report(player_index, event, &profile_out, processed_buttons);
}

// Send keyboard/mouse reports - delegates to mode interface
static bool usbd_send_kbmouse_report(uint8_t player_index)
{
    const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_KEYBOARD_MOUSE];
    if (!mode || !mode->send_report) return false;
    if (mode->is_ready && !mode->is_ready()) return false;

    // Check for pending event
    if (player_index >= USB_MAX_PLAYERS || !pending_flags[player_index]) {
        // No new input, but still send mouse report for continuous movement
        return kbmouse_mode_send_idle_mouse();
    }

    const input_event_t* event = &pending_events[player_index];
    pending_flags[player_index] = false;

    // Apply profile
    profile_output_t profile_out;
    uint32_t processed_buttons = apply_usbd_profile(event, &profile_out);

    return mode->send_report(player_index, event, &profile_out, processed_buttons);
}

#if CFG_TUD_GC_ADAPTER
// Send GC Adapter report - delegates to mode interface
static bool usbd_send_gc_adapter_report(uint8_t player_index)
{
    const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_GC_ADAPTER];
    if (!mode || !mode->send_report) return false;
    if (mode->is_ready && !mode->is_ready()) return false;

    // Check for pending event
    if (player_index >= USB_MAX_PLAYERS || !pending_flags[player_index]) {
        return false;
    }

    const input_event_t* event = &pending_events[player_index];
    pending_flags[player_index] = false;

    // Apply profile
    profile_output_t profile_out;
    uint32_t processed_buttons = apply_usbd_profile(event, &profile_out);

    return mode->send_report(player_index, event, &profile_out, processed_buttons);
}
#endif

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
        case USB_OUTPUT_MODE_KEYBOARD_MOUSE:
            return usbd_send_kbmouse_report(player_index);
#if CFG_TUD_GC_ADAPTER
        case USB_OUTPUT_MODE_GC_ADAPTER:
            return usbd_send_gc_adapter_report(player_index);
#endif
        case USB_OUTPUT_MODE_HID:
            return usbd_send_hid_report(player_index);

        case USB_OUTPUT_MODE_SINPUT:
        default:
            return usbd_send_sinput_report(player_index);
    }
}

// Get rumble value from USB host (for feedback to input controllers)
static uint8_t usbd_get_rumble(void)
{
    switch (output_mode) {
        case USB_OUTPUT_MODE_XBOX_ORIGINAL: {
            // XID: delegate to mode interface
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_XBOX_ORIGINAL];
            if (mode && mode->get_rumble) {
                return mode->get_rumble();
            }
            return 0;
        }
#if CFG_TUD_XINPUT
        case USB_OUTPUT_MODE_XINPUT: {
            // XInput: delegate to mode interface
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_XINPUT];
            if (mode && mode->get_rumble) {
                return mode->get_rumble();
            }
            return 0;
        }
#endif
        case USB_OUTPUT_MODE_PS3: {
            // PS3: delegate to mode interface
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_PS3];
            if (mode && mode->get_rumble) {
                return mode->get_rumble();
            }
            return 0;
        }
        case USB_OUTPUT_MODE_PS4: {
            // PS4: delegate to mode interface
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_PS4];
            if (mode && mode->get_rumble) {
                return mode->get_rumble();
            }
            return 0;
        }
#if CFG_TUD_GC_ADAPTER
        case USB_OUTPUT_MODE_GC_ADAPTER: {
            // GC Adapter: delegate to mode interface
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_GC_ADAPTER];
            if (mode && mode->get_rumble) {
                return mode->get_rumble();
            }
            return 0;
        }
#endif
        case USB_OUTPUT_MODE_SINPUT: {
            // SInput: delegate to mode interface
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_SINPUT];
            if (mode && mode->get_rumble) {
                return mode->get_rumble();
            }
            return 0;
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
        case USB_OUTPUT_MODE_XBOX_ORIGINAL: {
            // XID: delegate to mode interface
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_XBOX_ORIGINAL];
            if (mode && mode->get_feedback) {
                return mode->get_feedback(fb);
            }
            return false;
        }

#if CFG_TUD_XINPUT
        case USB_OUTPUT_MODE_XINPUT: {
            // XInput: delegate to mode interface
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_XINPUT];
            if (mode && mode->get_feedback) {
                return mode->get_feedback(fb);
            }
            return false;
        }
#endif

        case USB_OUTPUT_MODE_PS3: {
            // PS3: delegate to mode interface
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_PS3];
            if (mode && mode->get_feedback) {
                return mode->get_feedback(fb);
            }
            return false;
        }

        case USB_OUTPUT_MODE_PS4: {
            // PS4: delegate to mode interface
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_PS4];
            if (mode && mode->get_feedback) {
                return mode->get_feedback(fb);
            }
            return false;
        }

#if CFG_TUD_GC_ADAPTER
        case USB_OUTPUT_MODE_GC_ADAPTER: {
            // GC Adapter: delegate to mode interface
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_GC_ADAPTER];
            if (mode && mode->get_feedback) {
                return mode->get_feedback(fb);
            }
            return false;
        }
#endif

        case USB_OUTPUT_MODE_SINPUT: {
            // SInput: delegate to mode interface
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_SINPUT];
            if (mode && mode->get_feedback) {
                return mode->get_feedback(fb);
            }
            return false;
        }

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

// Interface numbers (SInput composite: 3 HID + CDC)
// HID interface numbers are defined in usbd.h (ITF_NUM_HID_GAMEPAD=0, KEYBOARD=1, MOUSE=2)
enum {
#if CFG_TUD_CDC >= 1
    ITF_NUM_CDC_0 = ITF_NUM_HID_MOUSE + 1,  // CDC 0 control interface (data port)
    ITF_NUM_CDC_0_DATA,                       // CDC 0 data interface
#endif
#if CFG_TUD_CDC >= 2
    ITF_NUM_CDC_1,             // CDC 1 control interface (debug port)
    ITF_NUM_CDC_1_DATA,        // CDC 1 data interface
#endif
    ITF_NUM_TOTAL
};

// Backward compatibility alias for non-composite modes
#define ITF_NUM_HID         ITF_NUM_HID_GAMEPAD

// Endpoint numbers (shifted to accommodate 3 HID interfaces)
#define EPNUM_HID_GAMEPAD       0x81  // Gamepad IN
#define EPNUM_HID_GAMEPAD_OUT   0x01  // Gamepad OUT (rumble/output reports)
#define EPNUM_HID_KEYBOARD      0x82  // Keyboard IN
#define EPNUM_HID_MOUSE         0x83  // Mouse IN

// Backward compatibility aliases
#define EPNUM_HID           EPNUM_HID_GAMEPAD
#define EPNUM_HID_OUT       EPNUM_HID_GAMEPAD_OUT

#if CFG_TUD_CDC >= 1
#define EPNUM_CDC_0_NOTIF   0x84
#define EPNUM_CDC_0_OUT     0x05
#define EPNUM_CDC_0_IN      0x85
#endif

#if CFG_TUD_CDC >= 2
#define EPNUM_CDC_1_NOTIF   0x86
#define EPNUM_CDC_1_OUT     0x07
#define EPNUM_CDC_1_IN      0x87
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
        case USB_OUTPUT_MODE_SINPUT:
            return (uint8_t const *)&sinput_device_descriptor;
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
        case USB_OUTPUT_MODE_KEYBOARD_MOUSE:
            // Share SInput device descriptor (same composite USB device)
            return (uint8_t const *)&sinput_device_descriptor;
        case USB_OUTPUT_MODE_GC_ADAPTER:
            return (uint8_t const *)&gc_adapter_device_descriptor;
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

// SInput composite configuration descriptor (gamepad + keyboard + mouse + CDC)
// Shared by both SInput and KB/Mouse modes for instant switching
#define CONFIG_TOTAL_LEN_SINPUT (TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN + TUD_HID_DESC_LEN + TUD_HID_DESC_LEN + (CFG_TUD_CDC * TUD_CDC_DESC_LEN))

static const uint8_t desc_configuration_sinput[] = {
    // Config: bus powered, max 500mA
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN_SINPUT, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),

    // Interface 0: HID gamepad with IN and OUT endpoints (for rumble/LEDs)
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_HID_GAMEPAD, 0, HID_ITF_PROTOCOL_NONE, sizeof(sinput_report_descriptor), EPNUM_HID_GAMEPAD_OUT, EPNUM_HID_GAMEPAD, CFG_TUD_HID_EP_BUFSIZE, 1),

    // Interface 1: HID keyboard (IN only)
    TUD_HID_DESCRIPTOR(ITF_NUM_HID_KEYBOARD, 0, HID_ITF_PROTOCOL_NONE, sizeof(sinput_keyboard_report_descriptor), EPNUM_HID_KEYBOARD, 16, 1),

    // Interface 2: HID mouse (IN only)
    TUD_HID_DESCRIPTOR(ITF_NUM_HID_MOUSE, 0, HID_ITF_PROTOCOL_NONE, sizeof(sinput_mouse_report_descriptor), EPNUM_HID_MOUSE, 8, 1),

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
        case USB_OUTPUT_MODE_SINPUT:
            return desc_configuration_sinput;
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
        case USB_OUTPUT_MODE_KEYBOARD_MOUSE:
            // Share SInput composite descriptor (same USB device)
            return desc_configuration_sinput;
        case USB_OUTPUT_MODE_GC_ADAPTER:
            return gc_adapter_config_descriptor;
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

    // XInput strings handled separately: copyright symbol in manufacturer
    // needs uint8_t cast, and security string (index 4) needs larger buffer
    if (output_mode == USB_OUTPUT_MODE_XINPUT && index >= 1 && index <= 4) {
        static uint16_t _xinput_str[96];  // Large enough for security string
        const char* xinput_str = NULL;
        switch (index) {
            case 1: xinput_str = XINPUT_MANUFACTURER; break;
            case 2: xinput_str = XINPUT_PRODUCT; break;
            case 3: xinput_str = usb_serial_str; break;
            case 4: xinput_str = XINPUT_SECURITY_STRING; break;
        }
        uint8_t xinput_len = strlen(xinput_str);
        if (xinput_len > 95) xinput_len = 95;
        for (uint8_t i = 0; i < xinput_len; i++) {
            _xinput_str[1 + i] = (uint8_t)xinput_str[i];
        }
        _xinput_str[0] = (TUSB_DESC_STRING << 8) | (2 * xinput_len + 2);
        return _xinput_str;
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
            if (output_mode == USB_OUTPUT_MODE_SINPUT ||
                output_mode == USB_OUTPUT_MODE_KEYBOARD_MOUSE) {
                str = SINPUT_MANUFACTURER;
            } else if (output_mode == USB_OUTPUT_MODE_XINPUT) {
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
            } else if (output_mode == USB_OUTPUT_MODE_GC_ADAPTER) {
                str = GC_ADAPTER_MANUFACTURER;
            } else {
                str = USB_HID_MANUFACTURER;
            }
            break;
        case STRID_PRODUCT:
            // Mode-specific product
            if (output_mode == USB_OUTPUT_MODE_SINPUT ||
                output_mode == USB_OUTPUT_MODE_KEYBOARD_MOUSE) {
                str = SINPUT_PRODUCT;
            } else if (output_mode == USB_OUTPUT_MODE_XINPUT) {
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
            } else if (output_mode == USB_OUTPUT_MODE_GC_ADAPTER) {
                str = GC_ADAPTER_PRODUCT;
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
    // SInput and KB/Mouse modes: route by interface (composite device)
    if (output_mode == USB_OUTPUT_MODE_SINPUT ||
        output_mode == USB_OUTPUT_MODE_KEYBOARD_MOUSE) {
        switch (itf) {
            case ITF_NUM_HID_GAMEPAD:  return sinput_report_descriptor;
            case ITF_NUM_HID_KEYBOARD: return sinput_keyboard_report_descriptor;
            case ITF_NUM_HID_MOUSE:    return sinput_mouse_report_descriptor;
            default:                   return sinput_report_descriptor;
        }
    }

    // All other modes: single HID interface (itf is always 0)
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
        const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_XAC];
        if (mode && mode->get_report_descriptor) {
            return mode->get_report_descriptor();
        }
    }
#if CFG_TUD_GC_ADAPTER
    if (output_mode == USB_OUTPUT_MODE_GC_ADAPTER) {
        const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_GC_ADAPTER];
        if (mode && mode->get_report_descriptor) {
            return mode->get_report_descriptor();
        }
    }
#endif
    return hid_report_descriptor;
}

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
    // SInput/KB/Mouse composite: route by interface
    if ((output_mode == USB_OUTPUT_MODE_SINPUT ||
         output_mode == USB_OUTPUT_MODE_KEYBOARD_MOUSE) &&
        itf != ITF_NUM_HID_GAMEPAD) {
        // Keyboard/mouse interfaces don't have get_report handlers
        return 0;
    }

    // PS3 feature reports: delegate to mode interface
    if (output_mode == USB_OUTPUT_MODE_PS3) {
        const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_PS3];
        if (mode && mode->get_report) {
            uint16_t result = mode->get_report(report_id, report_type, buffer, reqlen);
            if (result > 0) return result;
        }
    }

    // PS4 feature reports: delegate to mode interface
    if (output_mode == USB_OUTPUT_MODE_PS4) {
        const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_PS4];
        if (mode && mode->get_report) {
            uint16_t result = mode->get_report(report_id, report_type, buffer, reqlen);
            if (result > 0) return result;
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
    printf("[usbd] set_report_cb: itf=%d report_id=%d type=%d len=%d mode=%d\n",
           itf, report_id, report_type, bufsize, output_mode);

    // SInput/KB/Mouse composite: route by interface
    if (output_mode == USB_OUTPUT_MODE_SINPUT ||
        output_mode == USB_OUTPUT_MODE_KEYBOARD_MOUSE) {
        if (itf == ITF_NUM_HID_GAMEPAD) {
            // Gamepad output report (rumble, LEDs) → SInput handler
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_SINPUT];
            if (mode && mode->handle_output) {
                mode->handle_output(report_id, buffer, bufsize);
            }
            return;
        }
        if (itf == ITF_NUM_HID_KEYBOARD) {
            // Keyboard LED output report → KB/Mouse handler
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_KEYBOARD_MOUSE];
            if (mode && mode->handle_output) {
                mode->handle_output(report_id, buffer, bufsize);
            }
            return;
        }
        // Mouse interface has no output reports
        return;
    }

    // PS3 output report: delegate to mode interface
    if (output_mode == USB_OUTPUT_MODE_PS3) {
        const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_PS3];
        if (mode && mode->handle_output) {
            mode->handle_output(report_id, buffer, bufsize);
            return;
        }
    }

    // PS4 output report: delegate to mode interface
    if (output_mode == USB_OUTPUT_MODE_PS4) {
        const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_PS4];
        if (mode && mode->handle_output) {
            mode->handle_output(report_id, buffer, bufsize);
        }
        // Also handle feature reports for auth
        if (report_type == HID_REPORT_TYPE_FEATURE) {
            ps4_mode_set_feature_report(report_id, buffer, bufsize);
        }
        return;
    }

#if CFG_TUD_GC_ADAPTER
    // GC Adapter output reports - delegate to mode interface
    if (output_mode == USB_OUTPUT_MODE_GC_ADAPTER) {
        const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_GC_ADAPTER];
        if (mode && mode->handle_output) {
            mode->handle_output(report_id, buffer, bufsize);
            return;
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
        case USB_OUTPUT_MODE_XBOX_ORIGINAL: {
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_XBOX_ORIGINAL];
            if (mode && mode->get_class_driver) {
                *driver_count = 1;
                return mode->get_class_driver();
            }
            *driver_count = 0;
            return NULL;
        }

#if CFG_TUD_XINPUT
        case USB_OUTPUT_MODE_XINPUT:
            *driver_count = 1;
            return tud_xinput_class_driver();
#endif

        case USB_OUTPUT_MODE_XBONE: {
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_XBONE];
            if (mode && mode->get_class_driver) {
                *driver_count = 1;
                return mode->get_class_driver();
            }
            *driver_count = 0;
            return NULL;
        }

#if CFG_TUD_GC_ADAPTER
        case USB_OUTPUT_MODE_GC_ADAPTER: {
            const usbd_mode_t* mode = usbd_modes[USB_OUTPUT_MODE_GC_ADAPTER];
            if (mode && mode->get_class_driver) {
                *driver_count = 1;
                return mode->get_class_driver();
            }
            *driver_count = 0;
            return NULL;
        }
#endif

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
    if (output_mode == USB_OUTPUT_MODE_XINPUT) {
        return tud_xinput_vendor_control_xfer_cb(rhport, stage, request);
    }
    return true;  // Accept by default for other modes
}
