// alpakka.h - Alpakka Controller Pad Configuration
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Pin mappings for Alpakka 3D printed controller.
// Based on Input Labs Alpakka firmware pin definitions.
// https://github.com/inputlabs/alpakka_firmware

#ifndef PAD_CONFIG_ALPAKKA_H
#define PAD_CONFIG_ALPAKKA_H

#include "../pad_input.h"

// ============================================================================
// ALPAKKA - Input Labs 3D Printed Controller
// ============================================================================
// Raspberry Pi Pico with dual PCA9555 I2C I/O expanders.
// Most buttons are on I2C expanders, thumbstick on ADC.
//
// Pin addressing:
//   0-29:    Direct GPIO pins
//   100-115: I2C I/O expander 0 (address 0x20)
//   200-215: I2C I/O expander 1 (address 0x21)

static const pad_device_config_t pad_config_alpakka = {
    .name = "Alpakka",
    .active_high = false,   // Pull-up buttons (pressed = low)

    // I2C for PCA9555 expanders
    .i2c_sda = 14,
    .i2c_scl = 15,

    // D-pad (I2C expander 0)
    .dpad_up    = 103,      // PIN_DPAD_UP
    .dpad_down  = 100,      // PIN_DPAD_DOWN
    .dpad_left  = 104,      // PIN_DPAD_LEFT
    .dpad_right = 101,      // PIN_DPAD_RIGHT

    // Face buttons (I2C expander 1)
    .b1 = 215,              // A / Cross (PIN_A)
    .b2 = 210,              // B / Circle (PIN_B)
    .b3 = 213,              // X / Square (PIN_X)
    .b4 = 211,              // Y / Triangle (PIN_Y)

    // Shoulder buttons
    .l1 = 102,              // LB / L1 (expander 0)
    .r1 = 212,              // RB / R1 (expander 1)
    .l2 = 115,              // LT / L2 (expander 0)
    .r2 = 214,              // RT / R2 (expander 1)

    // Meta buttons
    .s1 = 114,              // Select / Back (PIN_SELECT_1, expander 0)
    .s2 = 200,              // Start (PIN_START_1, expander 1)

    // Stick clicks
    .l3 = 112,              // Left stick click (expander 0)
    .r3 = PAD_PIN_DISABLED, // No right stick

    // Home (direct GPIO)
    .a1 = 20,               // Home / Guide (PIN_HOME)
    .a2 = PAD_PIN_DISABLED,

    // Extra paddles
    .l4 = 109,              // Left paddle (expander 0, PIN_L4)
    .r4 = 207,              // Right paddle (expander 1, PIN_R4)

    // Left analog stick on ADC
    // Alpakka has single thumbstick on left side
    .adc_lx = 1,            // ADC channel 1 (GPIO 27, PIN_TX)
    .adc_ly = 0,            // ADC channel 0 (GPIO 26, PIN_TY)
    .adc_rx = PAD_PIN_DISABLED,
    .adc_ry = PAD_PIN_DISABLED,

    .invert_lx = false,
    .invert_ly = false,
    .invert_rx = false,
    .invert_ry = false,
    .deadzone = 10,

    // No NeoPixel on standard Alpakka (uses OLED + LEDs on GPIO 2-5)
    .led_pin = PAD_PIN_DISABLED,
    .led_count = 0,
};

#endif // PAD_CONFIG_ALPAKKA_H
