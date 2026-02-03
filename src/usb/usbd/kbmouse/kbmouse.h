// kbmouse.h - Gamepad to Keyboard/Mouse conversion
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Converts gamepad input to keyboard and mouse HID reports.
// Enables using any controller for desktop applications, accessibility, or games.

#ifndef KBMOUSE_H
#define KBMOUSE_H

#include <stdint.h>
#include <stdbool.h>
#include "core/services/profiles/profile.h"
#include "descriptors/kbmouse_descriptors.h"

// ============================================================================
// REPORT STRUCTURES
// ============================================================================

// Keyboard report (matches HID descriptor with report ID 1)
typedef struct __attribute__((packed)) {
    uint8_t modifier;       // Modifier keys (Ctrl, Shift, Alt, GUI)
    uint8_t reserved;       // Reserved byte
    uint8_t keycode[6];     // Up to 6 simultaneous keycodes
} kbmouse_keyboard_report_t;

// Mouse report (matches HID descriptor with report ID 2)
typedef struct __attribute__((packed)) {
    uint8_t buttons;        // Button states (5 buttons)
    int8_t x;               // X movement (-127 to 127)
    int8_t y;               // Y movement (-127 to 127)
    int8_t wheel;           // Vertical scroll (-127 to 127)
    int8_t pan;             // Horizontal scroll (-127 to 127)
} kbmouse_mouse_report_t;

// ============================================================================
// KEYBOARD MODIFIERS
// ============================================================================

#define KBMOUSE_MOD_LCTRL   (1 << 0)
#define KBMOUSE_MOD_LSHIFT  (1 << 1)
#define KBMOUSE_MOD_LALT    (1 << 2)
#define KBMOUSE_MOD_LGUI    (1 << 3)
#define KBMOUSE_MOD_RCTRL   (1 << 4)
#define KBMOUSE_MOD_RSHIFT  (1 << 5)
#define KBMOUSE_MOD_RALT    (1 << 6)
#define KBMOUSE_MOD_RGUI    (1 << 7)

// HID keycodes: use TinyUSB's HID_KEY_* defines from class/hid/hid.h
// (included transitively via tusb.h / kbmouse_descriptors.h)

// ============================================================================
// MOUSE BUTTONS
// ============================================================================

#define KBMOUSE_BTN_LEFT    (1 << 0)
#define KBMOUSE_BTN_RIGHT   (1 << 1)
#define KBMOUSE_BTN_MIDDLE  (1 << 2)
#define KBMOUSE_BTN_BACK    (1 << 3)
#define KBMOUSE_BTN_FORWARD (1 << 4)

// ============================================================================
// BUTTON MAPPING TYPES
// ============================================================================

typedef enum {
    KBMOUSE_ACTION_NONE = 0,
    KBMOUSE_ACTION_KEY,         // Keyboard key press
    KBMOUSE_ACTION_MODIFIER,    // Keyboard modifier (Shift, Ctrl, etc.)
    KBMOUSE_ACTION_MOUSE_BTN,   // Mouse button click
} kbmouse_action_type_t;

// Button mapping entry
typedef struct {
    uint32_t gamepad_button;        // JP_BUTTON_* input
    kbmouse_action_type_t type;     // Action type
    uint8_t value;                  // Keycode, modifier, or mouse button
} kbmouse_button_map_t;

// ============================================================================
// ANALOG CONFIGURATION
// ============================================================================

typedef struct {
    uint8_t deadzone;           // Deadzone (0-127, default 15)
    uint8_t sensitivity;        // Sensitivity multiplier (1-10, default 5)
    uint8_t scroll_deadzone;    // Scroll deadzone (default 30)
    uint8_t scroll_speed;       // Scroll speed (1-10, default 3)
} kbmouse_analog_config_t;

// Default analog configuration
#define KBMOUSE_DEFAULT_DEADZONE        15
#define KBMOUSE_DEFAULT_SENSITIVITY     5
#define KBMOUSE_DEFAULT_SCROLL_DEADZONE 30
#define KBMOUSE_DEFAULT_SCROLL_SPEED    3

// ============================================================================
// PUBLIC API
// ============================================================================

// Initialize keyboard/mouse converter
void kbmouse_init(void);

// Convert gamepad buttons and analog values to keyboard/mouse reports
// buttons: remapped button state from profile_output_t
// profile_out: contains analog values after profile processing
// kb_report: output keyboard report
// mouse_report: output mouse report
void kbmouse_convert(uint32_t buttons,
                     const profile_output_t* profile_out,
                     kbmouse_keyboard_report_t* kb_report,
                     kbmouse_mouse_report_t* mouse_report);

// Get/set analog configuration
const kbmouse_analog_config_t* kbmouse_get_config(void);
void kbmouse_set_config(const kbmouse_analog_config_t* config);

// Get keyboard LED state (Caps Lock, Num Lock, etc.)
// Returns bitmask: bit 0 = Num Lock, bit 1 = Caps Lock, bit 2 = Scroll Lock
uint8_t kbmouse_get_led_state(void);

// Set keyboard LED state (called from USB HID output report callback)
void kbmouse_set_led_state(uint8_t leds);

#endif // KBMOUSE_H
