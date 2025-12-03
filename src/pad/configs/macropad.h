// macropad.h - Adafruit MacroPad RP2040 Pad Configuration
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Pin mappings for Adafruit MacroPad RP2040.
// 12 mechanical keys in 4x3 grid + rotary encoder with button.
//
// Physical layout:
//   [1] [2] [3]
//   [4] [5] [6]
//   [7] [8] [9]
//   [10][11][12]
//   + Rotary encoder (button on GPIO 0)

#ifndef PAD_CONFIG_MACROPAD_H
#define PAD_CONFIG_MACROPAD_H

#include "../pad_input.h"

// User button: Rotary encoder button (GPIO 0)
// Set via BUTTON_USER_GPIO=0 in CMakeLists.txt

// ============================================================================
// MACROPAD - D-Pad + Buttons Layout
// ============================================================================
// Maps 12 keys to D-pad and gamepad buttons.
//
// Physical layout → Mapping:
//   [1] [2] [3]       [←] [SE] [L1]
//   [4] [5] [6]   =   [↓] [↑]  [R1]
//   [7] [8] [9]       [→] [A]  [X]
//   [10][11][12]      [ST][B]  [Y]
//
// Encoder button = Mode switch (double-click to cycle USB modes)

static const pad_device_config_t pad_config_macropad = {
    .name = "MacroPad",
    .active_high = false,   // Keys have pull-ups (pressed = low)

    // No I2C expanders
    .i2c_sda = PAD_PIN_DISABLED,
    .i2c_scl = PAD_PIN_DISABLED,

    // D-pad (keys 1, 4, 5, 7)
    .dpad_up    = 5,         // Key 5
    .dpad_down  = 4,         // Key 4
    .dpad_left  = 1,         // Key 1
    .dpad_right = 7,         // Key 7

    // Face buttons (keys 8, 9, 11, 12)
    .b1 = 8,                 // A / Cross (Key 8)
    .b2 = 11,                // B / Circle (Key 11)
    .b3 = 9,                 // X / Square (Key 9)
    .b4 = 12,                // Y / Triangle (Key 12)

    // Shoulder buttons (keys 3, 6)
    .l1 = 3,                 // LB / L1 (Key 3)
    .r1 = 6,                 // RB / R1 (Key 6)
    .l2 = PAD_PIN_DISABLED,
    .r2 = PAD_PIN_DISABLED,

    // Meta buttons (key 10)
    .s1 = 2,                 // Select / Back (Key 2)
    .s2 = 10,                // Start (Key 10)

    // No stick clicks
    .l3 = PAD_PIN_DISABLED,
    .r3 = PAD_PIN_DISABLED,

    // Aux buttons (unused)
    .a1 = 0,                // Home / Guide (Encoder button)
    .a2 = PAD_PIN_DISABLED,

    // Extra buttons (Key 2 unused)
    .l4 = PAD_PIN_DISABLED,
    .r4 = PAD_PIN_DISABLED,

    // No analog sticks
    .adc_lx = PAD_PIN_DISABLED,
    .adc_ly = PAD_PIN_DISABLED,
    .adc_rx = PAD_PIN_DISABLED,
    .adc_ry = PAD_PIN_DISABLED,

    .invert_lx = false,
    .invert_ly = false,
    .invert_rx = false,
    .invert_ry = false,
    .deadzone = 10,

    // NeoPixel on GPIO 19 (12 LEDs, one per key)
    .led_pin = 19,
    .led_count = 12,

    // Per-key LED colors (R, G, B) - matches physical 4x3 layout
    // LED index = Key number - 1 (LED 0 = Key 1, etc.)
    //
    //   [Key 1]  [Key 2]  [Key 3]      [D-Left] [Select] [L1]
    //   [Key 4]  [Key 5]  [Key 6]  =>  [D-Down] [D-Up]   [R1]
    //   [Key 7]  [Key 8]  [Key 9]      [D-Right][A]      [X]
    //   [Key 10] [Key 11] [Key 12]     [Start]  [B]      [Y]
    //
    .led_colors = {
        {  0,   0,  64},  // Key 1:  D-Left  - Blue (D-pad)
        { 32,  32,  32},  // Key 2:  Select  - White (Meta)
        { 64,   0,  64},  // Key 3:  L1      - Purple (Shoulder)
        {  0,   0,  64},  // Key 4:  D-Down  - Blue (D-pad)
        {  0,   0,  64},  // Key 5:  D-Up    - Blue (D-pad)
        { 64,   0,  64},  // Key 6:  R1      - Purple (Shoulder)
        {  0,   0,  64},  // Key 7:  D-Right - Blue (D-pad)
        {  0,  64,   0},  // Key 8:  A       - Green (Xbox A)
        {  0,   0,  64},  // Key 9:  X       - Blue (Xbox X)
        { 32,  32,  32},  // Key 10: Start   - White (Meta)
        { 64,   0,   0},  // Key 11: B       - Red (Xbox B)
        { 64,  64,   0},  // Key 12: Y       - Yellow (Xbox Y)
    },
};

#endif // PAD_CONFIG_MACROPAD_H
