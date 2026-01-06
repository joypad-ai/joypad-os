// usbd.h - USB device output
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// USB device output with multiple output mode support.
// Modes can be switched via CDC commands, requiring USB re-enumeration.

#ifndef USBD_H
#define USBD_H

#include <stdint.h>
#include <stdbool.h>
#include "core/output_interface.h"

// ============================================================================
// OUTPUT MODES
// ============================================================================

// Output modes (console types)
typedef enum {
    USB_OUTPUT_MODE_HID = 0,            // Generic HID gamepad (DInput)
    USB_OUTPUT_MODE_XINPUT,             // Xbox 360/One/Series (XInput)
    USB_OUTPUT_MODE_PS3,                // PlayStation 3 (DInput/SixAxis)
    USB_OUTPUT_MODE_PS4,                // PlayStation 4
    USB_OUTPUT_MODE_SWITCH,             // Nintendo Switch (docked)
    USB_OUTPUT_MODE_PSCLASSIC,          // PlayStation Classic (PS1 Mini)
    USB_OUTPUT_MODE_XBOX_ORIGINAL,      // Original Xbox (XID protocol)
    USB_OUTPUT_MODE_XBONE,              // Xbox One (GIP protocol)
    USB_OUTPUT_MODE_XAC,                // Xbox Adaptive Controller compatible
    USB_OUTPUT_MODE_COUNT
} usb_output_mode_t;

// ============================================================================
// MODE-SPECIFIC USB IDENTIFIERS
// ============================================================================

// HID Mode (PS3-compatible DInput)
#define USB_HID_VID            0x2563  // SHANWAN
#define USB_HID_PID            0x0575  // 2In1 USB Joystick
#define USB_HID_BCD            0x0100  // v1.0
#define USB_HID_MANUFACTURER   "Joypad"
#define USB_HID_PRODUCT        "Joypad (DInput)"

// Xbox Original Mode (XID)
#define USB_XOG_VID            0x045E  // Microsoft
#define USB_XOG_PID            0x0289  // Xbox Controller S
#define USB_XOG_BCD            0x0121  // v1.21

// Legacy defines for backward compatibility
#define USB_DEVICE_VENDOR_ID   USB_HID_VID
#define USB_DEVICE_PRODUCT_ID  USB_HID_PID
#define USB_DEVICE_BCD_DEVICE  USB_HID_BCD
#define USB_STRING_MANUFACTURER USB_HID_MANUFACTURER
#define USB_STRING_PRODUCT     USB_HID_PRODUCT

// Gamepad button masks - DirectInput mapping (matches GP2040-CE)
// HID buttons are 1-indexed, bit positions are 0-indexed
// DirectInput button N = bit (N-1)
#define USB_GAMEPAD_MASK_B3    (1U << 0)   // DInput 1  - Face left (X/Square)
#define USB_GAMEPAD_MASK_B1    (1U << 1)   // DInput 2  - Face bottom (A/Cross)
#define USB_GAMEPAD_MASK_B2    (1U << 2)   // DInput 3  - Face right (B/Circle)
#define USB_GAMEPAD_MASK_B4    (1U << 3)   // DInput 4  - Face top (Y/Triangle)
#define USB_GAMEPAD_MASK_L1    (1U << 4)   // DInput 5  - Left bumper
#define USB_GAMEPAD_MASK_R1    (1U << 5)   // DInput 6  - Right bumper
#define USB_GAMEPAD_MASK_L2    (1U << 6)   // DInput 7  - Left trigger (digital)
#define USB_GAMEPAD_MASK_R2    (1U << 7)   // DInput 8  - Right trigger (digital)
#define USB_GAMEPAD_MASK_S1    (1U << 8)   // DInput 9  - Select/Back
#define USB_GAMEPAD_MASK_S2    (1U << 9)   // DInput 10 - Start
#define USB_GAMEPAD_MASK_L3    (1U << 10)  // DInput 11 - Left stick click
#define USB_GAMEPAD_MASK_R3    (1U << 11)  // DInput 12 - Right stick click
#define USB_GAMEPAD_MASK_A1    (1U << 12)  // DInput 13 - Home/Guide
#define USB_GAMEPAD_MASK_A2    (1U << 13)  // DInput 14 - Capture/Touchpad
#define USB_GAMEPAD_MASK_A3    (1U << 14)  // DInput 15 - Mute/Square
#define USB_GAMEPAD_MASK_A4    (1U << 15)  // DInput 16 - Reserved
#define USB_GAMEPAD_MASK_L4    (1U << 16)  // DInput 17 - Left paddle
#define USB_GAMEPAD_MASK_R4    (1U << 17)  // DInput 18 - Right paddle

// D-pad masks
#define USB_DPAD_MASK_UP       (1U << 0)
#define USB_DPAD_MASK_DOWN     (1U << 1)
#define USB_DPAD_MASK_LEFT     (1U << 2)
#define USB_DPAD_MASK_RIGHT    (1U << 3)

// ============================================================================
// PUBLIC API
// ============================================================================

// Initialize USB device output
void usbd_init(void);

// Process USB device tasks (call from main loop)
void usbd_task(void);

// Send gamepad report for a player
bool usbd_send_report(uint8_t player_index);

// ============================================================================
// MODE SELECTION API
// ============================================================================

// Get current output mode
usb_output_mode_t usbd_get_mode(void);

// Set output mode (requires USB re-enumeration)
// Returns true if mode was changed, false if same mode or invalid
// Note: This will trigger a device reset to re-enumerate with new descriptors
bool usbd_set_mode(usb_output_mode_t mode);

// Get mode name string
const char* usbd_get_mode_name(usb_output_mode_t mode);

// Cycle to next USB output mode (for button handlers)
// Cycles: HID → XInput → PS3 → PS4 → Switch → HID
// Call usbd_set_mode() with result to apply
usb_output_mode_t usbd_get_next_mode(void);

// Reset to default HID mode (for button handlers)
// Returns true if mode was changed
bool usbd_reset_to_hid(void);

// Output interface for app integration
extern const OutputInterface usbd_output_interface;

#endif // USBD_H
