// abb.h - RP2040 Advanced Breakout Board (ABB) Pad Configuration
//
// Pin mappings matching GP2040-CE RP2040AdvancedBreakoutBoard config.
// Active low buttons (pressed = GPIO low, internal pull-up).
// I2C OLED on GPIO 0/1, WS2812 LEDs on GPIO 4, 12 LEDs.
//
// GP2040-CE board labels → Joypad (W3C) mapping:
//   K1 (GPIO 12) → B3 (X/Square)     P1 (GPIO 10) → R1 (RB)
//   K2 (GPIO 11) → B4 (Y/Triangle)   P2 (GPIO  9) → L1 (LB)
//   K3 (GPIO  8) → B1 (A/Cross)      P3 (GPIO  6) → R2 (RT)
//   K4 (GPIO  7) → B2 (B/Circle)     P4 (GPIO  5) → L2 (LT)
//
// Passthrough variant: USB-A host port on GPIO 23/24 (SMD slider in USB position)
// SMD sliders: USB/Option (GPIO 23/24 routing), LED/GPIO25 (LED enable)

#ifndef PAD_CONFIG_ABB_H
#define PAD_CONFIG_ABB_H

#include "../pad_input.h"

static const pad_device_config_t pad_config_abb = {
    .name = "JoypadOS Controller",
    .active_high = false,   // Active low (pull-up, grounded when pressed)

    // No I2C expanders (I2C0 used for OLED display)
    .i2c_sda = PAD_PIN_DISABLED,
    .i2c_scl = PAD_PIN_DISABLED,

    // D-pad
    .dpad_up    = 19,
    .dpad_down  = 18,
    .dpad_left  = 16,
    .dpad_right = 17,

    // Face buttons
    .b1 = 8,    // A / Cross
    .b2 = 7,    // B / Circle
    .b3 = 12,   // X / Square
    .b4 = 11,   // Y / Triangle

    // Shoulder buttons
    .l1 = 9,
    .r1 = 10,
    .l2 = 5,
    .r2 = 6,

    // Meta buttons
    .s1 = 15,   // Select / Back
    .s2 = 13,   // Start

    // Stick clicks
    .l3 = 21,
    .r3 = 22,

    // Extra buttons
    .a1 = 14,   // Home / Guide
    .a2 = 20,   // Capture
    .a3 = PAD_PIN_DISABLED,
    .a4 = PAD_PIN_DISABLED,

    .l4 = PAD_PIN_DISABLED,
    .r4 = PAD_PIN_DISABLED,

    .f1 = PAD_PIN_DISABLED,
    .f2 = PAD_PIN_DISABLED,

    // No toggle switches (screw terminal toggles are user-configurable via web config)
    .toggle = {
        { .pin = PAD_PIN_DISABLED, .function = 0, .invert = false },
        { .pin = PAD_PIN_DISABLED, .function = 0, .invert = false },
    },

    // No analog sticks (ADC pins 26-29 available but not wired by default)
    .adc_lx = PAD_PIN_DISABLED,
    .adc_ly = PAD_PIN_DISABLED,
    .adc_rx = PAD_PIN_DISABLED,
    .adc_ry = PAD_PIN_DISABLED,
    .invert_lx = false,
    .invert_ly = false,
    .invert_rx = false,
    .invert_ry = false,
    .deadzone = 10,

    // WS2812 RGB LEDs
    .led_pin = 4,
    .led_count = 12,

    // LED button mapping (GP2040-CE order)
    .led_button_map = {
        [0]  = JP_BUTTON_DL,   // LED 0 = D-Left
        [1]  = JP_BUTTON_DD,   // LED 1 = D-Down
        [2]  = JP_BUTTON_DR,   // LED 2 = D-Right
        [3]  = JP_BUTTON_DU,   // LED 3 = D-Up
        [4]  = JP_BUTTON_B3,   // LED 4 = X/Square
        [5]  = JP_BUTTON_B4,   // LED 5 = Y/Triangle
        [6]  = JP_BUTTON_R1,   // LED 6 = R1
        [7]  = JP_BUTTON_L1,   // LED 7 = L1
        [8]  = JP_BUTTON_B1,   // LED 8 = A/Cross
        [9]  = JP_BUTTON_B2,   // LED 9 = B/Circle
        [10] = JP_BUTTON_R2,   // LED 10 = R2
        [11] = JP_BUTTON_L2,   // LED 11 = L2
    },

    // No speaker
    .speaker_pin = PAD_PIN_DISABLED,
    .speaker_enable_pin = PAD_PIN_DISABLED,

    // No SPI display (OLED is I2C, handled separately)
    .display_spi = PAD_PIN_DISABLED,
    .display_sck = PAD_PIN_DISABLED,
    .display_mosi = PAD_PIN_DISABLED,
    .display_cs = PAD_PIN_DISABLED,
    .display_dc = PAD_PIN_DISABLED,
    .display_rst = PAD_PIN_DISABLED,

    // No QWIIC
    .qwiic_tx = PAD_PIN_DISABLED,
    .qwiic_rx = PAD_PIN_DISABLED,
    .qwiic_i2c_inst = PAD_PIN_DISABLED,

    // USB host via PIO-USB on GPIO 23 (D+) / GPIO 24 (D-)
    // Passthrough variant: USB-A port, SMD slider in USB position
    .usb_host_dp = 23,

    // JoyWing on I2C0 (GP4=SDA, GP5=SCL)
    .joywing = {
        { .i2c_bus = 0, .sda = 4, .scl = 5, .addr = 0x49 },
        { .i2c_bus = 0, .sda = PAD_PIN_DISABLED, .scl = PAD_PIN_DISABLED, .addr = 0x49 },
    },
};

#endif // PAD_CONFIG_ABB_H
