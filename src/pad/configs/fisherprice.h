// fisherprice.h - Fisher Price Controller Pad Configuration
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Pin mappings for Fisher Price controller mod using KB2040.
// Based on GP2040-CE kb2040-fisher-price branch configuration.

#ifndef PAD_CONFIG_FISHERPRICE_H
#define PAD_CONFIG_FISHERPRICE_H

#include "../pad_input.h"

// ============================================================================
// FISHER PRICE - BUTTON ONLY (Original mod)
// ============================================================================
// KB2040 pin mapping for Fisher Price controller button mod.
// Active high buttons (pressed = GPIO high, button connects to 3.3V)

static const pad_device_config_t pad_config_fisherprice = {
    .name = "Fisher Price",
    .active_high = true,

    // No I2C expanders
    .i2c_sda = PAD_PIN_DISABLED,
    .i2c_scl = PAD_PIN_DISABLED,

    // D-pad
    .dpad_up    = 9,
    .dpad_down  = 10,
    .dpad_left  = 19,
    .dpad_right = 20,

    // Face buttons (SNES layout positions on controller)
    .b1 = 3,    // A / Cross
    .b2 = 4,    // B / Circle
    .b3 = 5,    // X / Square
    .b4 = 2,    // Y / Triangle

    // Shoulder buttons
    .l1 = 28,   // LB / L1
    .r1 = 8,    // RB / R1
    .l2 = 29,   // LT / L2
    .r2 = 7,    // RT / R2

    // Meta buttons
    .s1 = 0,    // Select / Back
    .s2 = 1,    // Start

    // Stick clicks
    .l3 = 18,   // L3
    .r3 = PAD_PIN_DISABLED,

    // Home/Capture
    .a1 = PAD_PIN_DISABLED,
    .a2 = PAD_PIN_DISABLED,

    // Extra paddles
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

    // NeoPixel on GPIO 17
    .led_pin = 17,
    .led_count = 1,
};

// ============================================================================
// FISHER PRICE ANALOG (Advanced mod with analog stick)
// ============================================================================
// KB2040 pin mapping with analog stick added.
// Based on GP2040-CE kb2040-analog-fisher-price branch.

static const pad_device_config_t pad_config_fisherprice_analog = {
    .name = "Fisher Price Analog",
    .active_high = true,

    // No I2C expanders
    .i2c_sda = PAD_PIN_DISABLED,
    .i2c_scl = PAD_PIN_DISABLED,

    // D-pad
    .dpad_up    = 9,
    .dpad_down  = 10,
    .dpad_left  = 19,
    .dpad_right = 20,

    // Face buttons
    .b1 = 3,    // A / Cross
    .b2 = 4,    // B / Circle
    .b3 = 5,    // X / Square
    .b4 = 2,    // Y / Triangle

    // Shoulder buttons
    .l1 = 28,   // LB / L1
    .r1 = 8,    // RB / R1
    .l2 = 29,   // LT / L2
    .r2 = 7,    // RT / R2

    // Meta buttons (S1 disabled, S2 moved to GPIO 18)
    .s1 = PAD_PIN_DISABLED,
    .s2 = 18,   // Start

    // Stick clicks
    .l3 = PAD_PIN_DISABLED,
    .r3 = PAD_PIN_DISABLED,

    // Home/Capture
    .a1 = PAD_PIN_DISABLED,
    .a2 = PAD_PIN_DISABLED,

    // Extra paddles
    .l4 = PAD_PIN_DISABLED,
    .r4 = PAD_PIN_DISABLED,

    // Left analog stick on ADC
    // GPIO 26 = ADC0, GPIO 27 = ADC1
    .adc_lx = 0,    // ADC channel 0 (GPIO 26)
    .adc_ly = 1,    // ADC channel 1 (GPIO 27)
    .adc_rx = PAD_PIN_DISABLED,
    .adc_ry = PAD_PIN_DISABLED,

    .invert_lx = false,
    .invert_ly = true,  // Y inverted per GP2040-CE config
    .invert_rx = false,
    .invert_ry = false,
    .deadzone = 10,

    // NeoPixel on GPIO 17
    .led_pin = 17,
    .led_count = 1,
};

#endif // PAD_CONFIG_FISHERPRICE_H
