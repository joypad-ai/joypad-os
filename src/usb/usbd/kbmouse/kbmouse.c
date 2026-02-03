// kbmouse.c - Gamepad to Keyboard/Mouse conversion
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Converts gamepad input to keyboard and mouse HID reports.

#include "kbmouse.h"
#include "core/buttons.h"
#include <string.h>
#include <stdlib.h>

// ============================================================================
// STATE
// ============================================================================

// Analog configuration (can be modified at runtime)
static kbmouse_analog_config_t analog_config = {
    .deadzone = KBMOUSE_DEFAULT_DEADZONE,
    .sensitivity = KBMOUSE_DEFAULT_SENSITIVITY,
    .scroll_deadzone = KBMOUSE_DEFAULT_SCROLL_DEADZONE,
    .scroll_speed = KBMOUSE_DEFAULT_SCROLL_SPEED,
};

// Keyboard LED state (set by host via output reports)
static uint8_t keyboard_led_state = 0;

// ============================================================================
// DEFAULT BUTTON MAPPING
// ============================================================================

// Default mapping table: gamepad button -> keyboard/mouse action
// Gaming-optimized: triggers for fire/aim, face buttons for actions
static const kbmouse_button_map_t default_button_map[] = {
    // Face buttons (common game actions)
    { JP_BUTTON_B1, KBMOUSE_ACTION_KEY, HID_KEY_SPACE },              // A/Cross -> Space (jump)
    { JP_BUTTON_B2, KBMOUSE_ACTION_KEY, HID_KEY_E },                 // B/Circle -> E (interact)
    { JP_BUTTON_B3, KBMOUSE_ACTION_KEY, HID_KEY_R },                 // X/Square -> R (reload)
    { JP_BUTTON_B4, KBMOUSE_ACTION_KEY, HID_KEY_Q },                 // Y/Triangle -> Q (ability/swap)

    // Shoulder buttons (modifiers)
    { JP_BUTTON_L1, KBMOUSE_ACTION_MODIFIER, KBMOUSE_MOD_LSHIFT },   // L1 -> Shift (sprint)
    { JP_BUTTON_R1, KBMOUSE_ACTION_MODIFIER, KBMOUSE_MOD_LCTRL },    // R1 -> Ctrl (crouch)

    // Triggers (primary combat)
    { JP_BUTTON_L2, KBMOUSE_ACTION_MOUSE_BTN, KBMOUSE_BTN_RIGHT },   // L2 -> Right click (ADS/aim)
    { JP_BUTTON_R2, KBMOUSE_ACTION_MOUSE_BTN, KBMOUSE_BTN_LEFT },    // R2 -> Left click (fire)

    // Center cluster
    { JP_BUTTON_S1, KBMOUSE_ACTION_KEY, HID_KEY_TAB },               // Select -> Tab (map/scoreboard)
    { JP_BUTTON_S2, KBMOUSE_ACTION_KEY, HID_KEY_ESCAPE },            // Start -> Escape (menu)

    // Stick clicks
    { JP_BUTTON_L3, KBMOUSE_ACTION_KEY, HID_KEY_V },                 // L3 -> V (melee)
    { JP_BUTTON_R3, KBMOUSE_ACTION_KEY, HID_KEY_F },                 // R3 -> F (interact/melee)

    // D-pad (weapon slots)
    { JP_BUTTON_DU, KBMOUSE_ACTION_KEY, HID_KEY_1 },                 // D-Up -> 1 (primary)
    { JP_BUTTON_DD, KBMOUSE_ACTION_KEY, HID_KEY_3 },                 // D-Down -> 3 (equipment)
    { JP_BUTTON_DL, KBMOUSE_ACTION_KEY, HID_KEY_4 },                 // D-Left -> 4 (grenade/util)
    { JP_BUTTON_DR, KBMOUSE_ACTION_KEY, HID_KEY_2 },                 // D-Right -> 2 (secondary)

    // Auxiliary
    { JP_BUTTON_A1, KBMOUSE_ACTION_KEY, HID_KEY_M },                 // Home/Guide -> M (map)
};

#define DEFAULT_MAP_COUNT (sizeof(default_button_map) / sizeof(default_button_map[0]))

// ============================================================================
// ANALOG PROCESSING
// ============================================================================

// Apply deadzone and sensitivity curve to analog stick value
// Returns mouse movement delta (-127 to 127)
static int8_t process_analog_to_mouse(uint8_t analog, uint8_t deadzone, uint8_t sensitivity)
{
    // Center analog value to signed
    int16_t centered = (int16_t)analog - 128;

    // Apply deadzone
    if (abs(centered) < deadzone) {
        return 0;
    }

    // Calculate sign and magnitude
    int16_t sign = (centered > 0) ? 1 : -1;
    int16_t magnitude = abs(centered) - deadzone;

    // Normalize to 0.0-1.0 range (after deadzone)
    float normalized = (float)magnitude / (127 - deadzone);

    // Apply quadratic curve for acceleration (more precise at low speeds)
    float curved = normalized * normalized;

    // Scale by sensitivity (1-10 maps to 0.2-2.0)
    float sens_factor = (float)sensitivity / 5.0f;

    // Calculate final result
    int16_t result = (int16_t)(curved * 127.0f * sens_factor) * sign;

    // Clamp to int8 range
    if (result > 127) result = 127;
    if (result < -127) result = -127;

    return (int8_t)result;
}

// Process analog stick for scroll (right stick)
// Returns scroll delta (-127 to 127)
static int8_t process_analog_to_scroll(uint8_t analog, uint8_t deadzone, uint8_t speed)
{
    // Center analog value to signed
    int16_t centered = (int16_t)analog - 128;

    // Apply deadzone
    if (abs(centered) < deadzone) {
        return 0;
    }

    // Calculate sign and magnitude
    int16_t sign = (centered > 0) ? 1 : -1;
    int16_t magnitude = abs(centered) - deadzone;

    // Normalize to 0.0-1.0 range
    float normalized = (float)magnitude / (127 - deadzone);

    // Linear scaling for scroll (no curve - feels more natural)
    // Speed 1-10 maps to 0.1-1.0 of max scroll rate
    float speed_factor = (float)speed / 10.0f;

    // Calculate result (scroll values are typically smaller)
    int16_t result = (int16_t)(normalized * 15.0f * speed_factor) * sign;

    // Clamp
    if (result > 127) result = 127;
    if (result < -127) result = -127;

    return (int8_t)result;
}

// ============================================================================
// CONVERSION API
// ============================================================================

void kbmouse_init(void)
{
    // Reset to default configuration
    analog_config.deadzone = KBMOUSE_DEFAULT_DEADZONE;
    analog_config.sensitivity = KBMOUSE_DEFAULT_SENSITIVITY;
    analog_config.scroll_deadzone = KBMOUSE_DEFAULT_SCROLL_DEADZONE;
    analog_config.scroll_speed = KBMOUSE_DEFAULT_SCROLL_SPEED;
    keyboard_led_state = 0;
}

void kbmouse_convert(uint32_t buttons,
                     const profile_output_t* profile_out,
                     kbmouse_keyboard_report_t* kb_report,
                     kbmouse_mouse_report_t* mouse_report)
{
    // Clear reports
    memset(kb_report, 0, sizeof(kbmouse_keyboard_report_t));
    memset(mouse_report, 0, sizeof(kbmouse_mouse_report_t));

    uint8_t keycode_index = 0;

    // Process button mappings
    for (size_t i = 0; i < DEFAULT_MAP_COUNT; i++) {
        const kbmouse_button_map_t* map = &default_button_map[i];

        // Check if gamepad button is pressed
        if (buttons & map->gamepad_button) {
            switch (map->type) {
                case KBMOUSE_ACTION_KEY:
                    // Add keycode if we have room (6 key rollover)
                    if (keycode_index < 6) {
                        kb_report->keycode[keycode_index++] = map->value;
                    }
                    break;

                case KBMOUSE_ACTION_MODIFIER:
                    // Add modifier
                    kb_report->modifier |= map->value;
                    break;

                case KBMOUSE_ACTION_MOUSE_BTN:
                    // Add mouse button
                    mouse_report->buttons |= map->value;
                    break;

                default:
                    break;
            }
        }
    }

    // Process analog sticks

    // Right stick -> Mouse movement (like "look" in FPS games)
    mouse_report->x = process_analog_to_mouse(
        profile_out->right_x,
        analog_config.deadzone,
        analog_config.sensitivity
    );
    mouse_report->y = process_analog_to_mouse(
        profile_out->right_y,
        analog_config.deadzone,
        analog_config.sensitivity
    );

    // Left stick -> WASD keys (movement)
    // Use a larger deadzone for digital output to avoid drift
    const uint8_t wasd_deadzone = 40;

    // W - stick up (Y < center - deadzone)
    if (profile_out->left_y < (128 - wasd_deadzone) && keycode_index < 6) {
        kb_report->keycode[keycode_index++] = HID_KEY_W;
    }
    // S - stick down (Y > center + deadzone)
    if (profile_out->left_y > (128 + wasd_deadzone) && keycode_index < 6) {
        kb_report->keycode[keycode_index++] = HID_KEY_S;
    }
    // A - stick left (X < center - deadzone)
    if (profile_out->left_x < (128 - wasd_deadzone) && keycode_index < 6) {
        kb_report->keycode[keycode_index++] = HID_KEY_A;
    }
    // D - stick right (X > center + deadzone)
    if (profile_out->left_x > (128 + wasd_deadzone) && keycode_index < 6) {
        kb_report->keycode[keycode_index++] = HID_KEY_D;
    }
}

const kbmouse_analog_config_t* kbmouse_get_config(void)
{
    return &analog_config;
}

void kbmouse_set_config(const kbmouse_analog_config_t* config)
{
    if (config) {
        analog_config = *config;
    }
}

uint8_t kbmouse_get_led_state(void)
{
    return keyboard_led_state;
}

void kbmouse_set_led_state(uint8_t leds)
{
    keyboard_led_state = leds;
}
