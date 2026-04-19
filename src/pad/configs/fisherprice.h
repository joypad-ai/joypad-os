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
// FISHER PRICE V1 - BUTTON ONLY (Original mod)
// ============================================================================
// KB2040 pin mapping for Fisher Price controller button mod.
// Active high buttons (pressed = GPIO high, button connects to 3.3V)

static const pad_device_config_t pad_config_fisherprice_v1 = {
    .name = "Fisher Price V1",
    .active_high = true,

    // No I2C expanders
    .i2c_sda = PAD_PIN_DISABLED,
    .i2c_scl = PAD_PIN_DISABLED,

    // D-pad (on ADC pins A0-A3 = GPIO 26-29)
    .dpad_up    = 27,   // A1
    .dpad_down  = 29,   // A3
    .dpad_left  = 26,   // A0
    .dpad_right = 28,   // A2

    // Face buttons
    .b1 = 3,    // A / Cross
    .b2 = 2,    // B / Circle
    .b3 = 5,    // X / Square
    .b4 = 4,    // Y / Triangle

    // Shoulder buttons (disabled for now)
    .l1 = PAD_PIN_DISABLED,
    .r1 = PAD_PIN_DISABLED,
    .l2 = PAD_PIN_DISABLED,
    .r2 = PAD_PIN_DISABLED,

    // Meta buttons
    .s1 = PAD_PIN_DISABLED,
    .s2 = 18,   // Start

    // Stick clicks
    .l3 = PAD_PIN_DISABLED,
    .r3 = PAD_PIN_DISABLED,

    // Home/Capture
    .a1 = PAD_PIN_DISABLED,
    .a2 = PAD_PIN_DISABLED,
    .a3 = PAD_PIN_DISABLED,
    .a4 = PAD_PIN_DISABLED,

    // Extra paddles
    .l4 = PAD_PIN_DISABLED,
    .r4 = PAD_PIN_DISABLED,

    .f1 = PAD_PIN_DISABLED,
    .f2 = PAD_PIN_DISABLED,

    .toggle = {
        { .pin = 6, .function = PAD_TOGGLE_FUNC_DPAD_LSTICK, .invert = true },
        { .pin = PAD_PIN_DISABLED, .function = 0, .invert = false },
    },

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

    // No QWIIC/UART link
    .qwiic_tx = PAD_PIN_DISABLED,
    .qwiic_rx = PAD_PIN_DISABLED,
    .qwiic_i2c_inst = PAD_PIN_DISABLED,
    .usb_host_dp = PAD_PIN_DISABLED,
    .joywing = { 
        { .i2c_bus = 0, .sda = PAD_PIN_DISABLED, .scl = PAD_PIN_DISABLED, .addr = 0x49 }, 
        { .i2c_bus = 0, .sda = PAD_PIN_DISABLED, .scl = PAD_PIN_DISABLED, .addr = 0x49 }, 
    },
};

// ============================================================================
// FISHER PRICE V2 - ANALOG (Advanced mod with analog stick + shoulders)
// ============================================================================
// KB2040 pin mapping with analog stick on ADC.
// D-pad buttons directly wired, analog stick on ADC pins 26-27.
// Based on GP2040-CE kb2040-fisher-price-v2 branch.

static const pad_device_config_t pad_config_fisherprice_v2 = {
    .name = "Fisher Price V2",
    .active_high = true,

    // No I2C expanders
    .i2c_sda = PAD_PIN_DISABLED,
    .i2c_scl = PAD_PIN_DISABLED,

    // D-pad (directly wired digital buttons)
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

    // Meta buttons
    .s1 = 0,    // Select / Back
    .s2 = 1,    // Start

    // Stick clicks
    .l3 = 18,   // Left stick click
    .r3 = PAD_PIN_DISABLED,

    // Home/Capture
    .a1 = PAD_PIN_DISABLED,
    .a2 = PAD_PIN_DISABLED,
    .a3 = PAD_PIN_DISABLED,
    .a4 = PAD_PIN_DISABLED,

    // Extra paddles
    .l4 = PAD_PIN_DISABLED,
    .r4 = PAD_PIN_DISABLED,

    .f1 = PAD_PIN_DISABLED,
    .f2 = PAD_PIN_DISABLED,

    .toggle = {
        { .pin = 6, .function = PAD_TOGGLE_FUNC_DPAD_RSTICK, .invert = false },
        { .pin = PAD_PIN_DISABLED, .function = 0, .invert = false },
    },

    // Left analog stick on ADC (GPIO 26-27)
    .adc_lx = 0,    // ADC channel 0 (GPIO 26)
    .adc_ly = 1,    // ADC channel 1 (GPIO 27)
    .adc_rx = PAD_PIN_DISABLED,
    .adc_ry = PAD_PIN_DISABLED,

    .invert_lx = false,
    .invert_ly = true,
    .invert_rx = false,
    .invert_ry = false,
    .deadzone = 10,

    // NeoPixel on GPIO 17
    .led_pin = 17,
    .led_count = 1,

    // No QWIIC/UART link
    .qwiic_tx = PAD_PIN_DISABLED,
    .qwiic_rx = PAD_PIN_DISABLED,
    .qwiic_i2c_inst = PAD_PIN_DISABLED,
    .usb_host_dp = PAD_PIN_DISABLED,
    .joywing = { 
        { .i2c_bus = 0, .sda = PAD_PIN_DISABLED, .scl = PAD_PIN_DISABLED, .addr = 0x49 }, 
        { .i2c_bus = 0, .sda = PAD_PIN_DISABLED, .scl = PAD_PIN_DISABLED, .addr = 0x49 }, 
    },
};

#endif // PAD_CONFIG_FISHERPRICE_H
