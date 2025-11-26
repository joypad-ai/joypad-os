// profile.c - Universal Profile System Implementation
//
// Provides shared profile switching logic for all output devices.
// Supports per-output-target profile sets with shared fallback.

#include "profile.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

// External dependencies (feedback and visual indication)
#include "core/services/leds/ws2812.h"
#include "core/services/players/feedback.h"

// Flash storage
#include "core/services/storage/flash.h"

// ============================================================================
// PROFILE SYSTEM STATE
// ============================================================================

// Configuration
static const profile_config_t* config = NULL;

// Active index per output target
static uint8_t active_index[MAX_OUTPUT_TARGETS] = {0};

// Profile switch combo state
static uint32_t select_hold_start = 0;
static bool select_was_held = false;
static bool dpad_up_was_pressed = false;
static bool dpad_down_was_pressed = false;
static bool initial_trigger_done = false;

// Timing constants
static const uint32_t INITIAL_HOLD_TIME_MS = 2000;  // Must hold 2 seconds for first trigger

// Callbacks
static uint8_t (*get_player_count)(void) = NULL;
static profile_switch_callback_t on_switch_callback = NULL;

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

// Get profile set for an output target (with fallback to shared)
static const profile_set_t* get_profile_set(output_target_t output)
{
    if (!config) return NULL;

    // Try output-specific first
    if (output >= 0 && output < MAX_OUTPUT_TARGETS) {
        if (config->output_profiles[output]) {
            return config->output_profiles[output];
        }
    }

    // Fall back to shared profiles
    return config->shared_profiles;
}

// ============================================================================
// PROFILE SYSTEM API
// ============================================================================

void profile_init(const profile_config_t* cfg)
{
    config = cfg;

    if (!config) {
        return;
    }

    // Load saved profile indices for each configured output
    for (int i = 0; i < MAX_OUTPUT_TARGETS; i++) {
        const profile_set_t* set = NULL;
        if (config->output_profiles[i]) {
            set = config->output_profiles[i];
        }
        if (set) {
            active_index[i] = profile_load_from_flash((output_target_t)i, set->default_index);
            if (active_index[i] >= set->profile_count) {
                active_index[i] = set->default_index;
            }
        } else {
            active_index[i] = 0;
        }
    }

    // Also handle shared profiles for primary output
    output_target_t primary = router_get_primary_output();
    if (primary != OUTPUT_TARGET_NONE && !config->output_profiles[primary] && config->shared_profiles) {
        active_index[primary] = profile_load_from_flash(primary, config->shared_profiles->default_index);
        if (active_index[primary] >= config->shared_profiles->profile_count) {
            active_index[primary] = config->shared_profiles->default_index;
        }
    }
}

void profile_set_player_count_callback(uint8_t (*callback)(void))
{
    get_player_count = callback;
}

void profile_set_switch_callback(profile_switch_callback_t callback)
{
    on_switch_callback = callback;
}

const profile_t* profile_get_active(output_target_t output)
{
    const profile_set_t* set = get_profile_set(output);
    if (!set || set->profile_count == 0) {
        return NULL;
    }

    uint8_t idx = (output >= 0 && output < MAX_OUTPUT_TARGETS) ? active_index[output] : 0;
    if (idx >= set->profile_count) {
        idx = 0;
    }

    return &set->profiles[idx];
}

uint8_t profile_get_active_index(output_target_t output)
{
    if (output >= 0 && output < MAX_OUTPUT_TARGETS) {
        return active_index[output];
    }
    return 0;
}

uint8_t profile_get_count(output_target_t output)
{
    const profile_set_t* set = get_profile_set(output);
    return set ? set->profile_count : 0;
}

const char* profile_get_name(output_target_t output, uint8_t index)
{
    const profile_set_t* set = get_profile_set(output);
    if (!set || index >= set->profile_count) {
        return NULL;
    }
    return set->profiles[index].name;
}

// ============================================================================
// PROFILE SWITCHING
// ============================================================================

void profile_set_active(output_target_t output, uint8_t index)
{
    const profile_set_t* set = get_profile_set(output);
    if (!set || set->profile_count == 0 || index >= set->profile_count) {
        return;
    }

    if (output >= 0 && output < MAX_OUTPUT_TARGETS) {
        active_index[output] = index;
    }

    // Notify device of switch
    if (on_switch_callback) {
        on_switch_callback(output, index);
    }

    // Trigger visual and haptic feedback
    neopixel_indicate_profile(index);

    uint8_t player_count = get_player_count ? get_player_count() : 0;
    feedback_trigger(index, player_count);

    // Save to flash
    profile_save_to_flash(output);

    const char* name = profile_get_name(output, index);
    printf("[profile] Switched to: %s (output=%d)\n", name ? name : "(unknown)", output);
}

void profile_cycle_next(output_target_t output)
{
    uint8_t count = profile_get_count(output);
    if (count == 0) return;

    uint8_t current = profile_get_active_index(output);
    uint8_t new_index = (current + 1) % count;
    profile_set_active(output, new_index);
}

void profile_cycle_prev(output_target_t output)
{
    uint8_t count = profile_get_count(output);
    if (count == 0) return;

    uint8_t current = profile_get_active_index(output);
    uint8_t new_index = (current == 0) ? (count - 1) : (current - 1);
    profile_set_active(output, new_index);
}

// ============================================================================
// PROFILE SWITCH COMBO DETECTION
// ============================================================================
// SELECT + D-pad Up/Down to cycle profiles
// Requires holding SELECT for 2 seconds before first switch

void profile_check_switch_combo(uint32_t buttons)
{
    // Use primary output for switching
    output_target_t output = router_get_primary_output();
    if (output == OUTPUT_TARGET_NONE) return;

    uint8_t count = profile_get_count(output);
    if (count <= 1) return;

    uint8_t player_count = get_player_count ? get_player_count() : 0;
    if (player_count == 0) return;  // No controllers connected

    // Check button states (buttons are active-low in USBR format)
    bool select_held = ((buttons & USBR_BUTTON_S1) == 0);
    bool dpad_up_pressed = ((buttons & USBR_BUTTON_DU) == 0);
    bool dpad_down_pressed = ((buttons & USBR_BUTTON_DD) == 0);

    // Select released - reset everything
    if (!select_held) {
        select_hold_start = 0;
        select_was_held = false;
        dpad_up_was_pressed = false;
        dpad_down_was_pressed = false;
        initial_trigger_done = false;
        return;
    }

    // Select is held
    if (!select_was_held) {
        // Select just pressed - start timer
        select_hold_start = to_ms_since_boot(get_absolute_time());
        select_was_held = true;
    }

    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    uint32_t select_hold_duration = current_time - select_hold_start;

    // Check if initial 2-second hold period has elapsed
    bool can_trigger = initial_trigger_done || (select_hold_duration >= INITIAL_HOLD_TIME_MS);

    if (!can_trigger) {
        // Still waiting for initial 2-second hold
        return;
    }

    // Don't allow switching while feedback is still active
    if (neopixel_is_indicating() || feedback_is_active()) {
        return;
    }

    // D-pad Up - cycle forward on rising edge
    if (dpad_up_pressed && !dpad_up_was_pressed) {
        profile_cycle_next(output);
        initial_trigger_done = true;
    }
    dpad_up_was_pressed = dpad_up_pressed;

    // D-pad Down - cycle backward on rising edge
    if (dpad_down_pressed && !dpad_down_was_pressed) {
        profile_cycle_prev(output);
        initial_trigger_done = true;
    }
    dpad_down_was_pressed = dpad_down_pressed;
}

// ============================================================================
// FLASH PERSISTENCE
// ============================================================================

uint8_t profile_load_from_flash(output_target_t output, uint8_t default_index)
{
    flash_t settings;
    if (flash_load(&settings)) {
        // For now, use single stored index for all outputs
        // TODO: Store per-output indices if needed
        return settings.active_profile_index;
    }
    return default_index;
}

void profile_save_to_flash(output_target_t output)
{
    flash_t settings;
    // For now, save primary output's index
    // TODO: Store per-output indices if needed
    if (output >= 0 && output < MAX_OUTPUT_TARGETS) {
        settings.active_profile_index = active_index[output];
        flash_save(&settings);
    }
}

// ============================================================================
// BUTTON MAPPING APPLICATION
// ============================================================================

// Helper to apply analog target to output
static void apply_analog_target(analog_target_t target, uint8_t value, profile_output_t* output)
{
    switch (target) {
        case ANALOG_TARGET_LX_MIN:
            output->left_x = 0;
            output->left_x_override = true;
            break;
        case ANALOG_TARGET_LX_MAX:
            output->left_x = 255;
            output->left_x_override = true;
            break;
        case ANALOG_TARGET_LY_MIN:
            output->left_y = 0;
            output->left_y_override = true;
            break;
        case ANALOG_TARGET_LY_MAX:
            output->left_y = 255;
            output->left_y_override = true;
            break;
        case ANALOG_TARGET_RX_MIN:
            output->right_x = 0;
            output->right_x_override = true;
            break;
        case ANALOG_TARGET_RX_MAX:
            output->right_x = 255;
            output->right_x_override = true;
            break;
        case ANALOG_TARGET_RY_MIN:
            output->right_y = 0;
            output->right_y_override = true;
            break;
        case ANALOG_TARGET_RY_MAX:
            output->right_y = 255;
            output->right_y_override = true;
            break;
        case ANALOG_TARGET_L2_FULL:
            output->l2_analog = 255;
            output->l2_analog_override = true;
            break;
        case ANALOG_TARGET_R2_FULL:
            output->r2_analog = 255;
            output->r2_analog_override = true;
            break;
        case ANALOG_TARGET_L2_CUSTOM:
            output->l2_analog = value;
            output->l2_analog_override = true;
            break;
        case ANALOG_TARGET_R2_CUSTOM:
            output->r2_analog = value;
            output->r2_analog_override = true;
            break;
        case ANALOG_TARGET_NONE:
        default:
            break;
    }
}

void profile_apply(const profile_t* profile,
                   uint32_t input_buttons,
                   uint8_t lx, uint8_t ly,
                   uint8_t rx, uint8_t ry,
                   uint8_t l2, uint8_t r2,
                   profile_output_t* output)
{
    // Initialize output with passthrough values
    memset(output, 0, sizeof(profile_output_t));
    output->buttons = input_buttons;  // Start with passthrough
    output->left_x = lx;
    output->left_y = ly;
    output->right_x = rx;
    output->right_y = ry;
    output->l2_analog = l2;
    output->r2_analog = r2;

    if (!profile || !profile->button_map || profile->button_map_count == 0) {
        // No mapping, passthrough
        return;
    }

    // Build output button state
    // Start with all buttons released (active-high for building)
    uint32_t output_buttons = 0;
    uint32_t mapped_inputs = 0;

    // Apply explicit mappings
    for (uint8_t i = 0; i < profile->button_map_count; i++) {
        const button_map_entry_t* entry = &profile->button_map[i];

        // Check if input button is pressed (active-low: pressed = bit clear)
        bool pressed = ((input_buttons & entry->input) == 0);

        if (pressed) {
            // Set output button(s) (active-high during building)
            output_buttons |= entry->output;

            // Apply analog target if specified
            if (entry->analog != ANALOG_TARGET_NONE) {
                apply_analog_target(entry->analog, entry->analog_value, output);
            }
        }

        // Mark this input as mapped (even if not pressed)
        mapped_inputs |= entry->input;
    }

    // Passthrough unmapped buttons
    uint32_t unmapped_inputs = ~mapped_inputs;
    uint32_t pressed_unmapped = ~input_buttons & unmapped_inputs;
    output_buttons |= pressed_unmapped;

    // Convert back to active-low format
    output->buttons = ~output_buttons;

    // Apply stick sensitivity scaling
    if (profile->left_stick_sensitivity != 1.0f) {
        if (!output->left_x_override) {
            int16_t rel_x = (int16_t)output->left_x - 128;
            output->left_x = (uint8_t)(128 + (int16_t)(rel_x * profile->left_stick_sensitivity));
        }
        if (!output->left_y_override) {
            int16_t rel_y = (int16_t)output->left_y - 128;
            output->left_y = (uint8_t)(128 + (int16_t)(rel_y * profile->left_stick_sensitivity));
        }
    }

    if (profile->right_stick_sensitivity != 1.0f) {
        if (!output->right_x_override) {
            int16_t rel_x = (int16_t)output->right_x - 128;
            output->right_x = (uint8_t)(128 + (int16_t)(rel_x * profile->right_stick_sensitivity));
        }
        if (!output->right_y_override) {
            int16_t rel_y = (int16_t)output->right_y - 128;
            output->right_y = (uint8_t)(128 + (int16_t)(rel_y * profile->right_stick_sensitivity));
        }
    }

    // Apply trigger behavior (if triggers weren't overridden by button mappings)
    if (!output->l2_analog_override) {
        switch (profile->l2_behavior) {
            case TRIGGER_DIGITAL_ONLY:
                output->l2_analog = 0;
                break;
            case TRIGGER_FULL_PRESS:
                if ((input_buttons & USBR_BUTTON_L2) == 0) {
                    output->l2_analog = 255;
                }
                break;
            case TRIGGER_LIGHT_PRESS:
                if ((input_buttons & USBR_BUTTON_L2) == 0) {
                    output->l2_analog = profile->l2_analog_value;
                }
                break;
            case TRIGGER_PASSTHROUGH:
            default:
                // Already set above
                break;
        }
    }

    if (!output->r2_analog_override) {
        switch (profile->r2_behavior) {
            case TRIGGER_DIGITAL_ONLY:
                output->r2_analog = 0;
                break;
            case TRIGGER_FULL_PRESS:
                if ((input_buttons & USBR_BUTTON_R2) == 0) {
                    output->r2_analog = 255;
                }
                break;
            case TRIGGER_LIGHT_PRESS:
                if ((input_buttons & USBR_BUTTON_R2) == 0) {
                    output->r2_analog = profile->r2_analog_value;
                }
                break;
            case TRIGGER_PASSTHROUGH:
            default:
                // Already set above
                break;
        }
    }
}

uint32_t profile_apply_button_map(const profile_t* profile, uint32_t input_buttons)
{
    profile_output_t output;
    profile_apply(profile, input_buttons, 128, 128, 128, 128, 0, 0, &output);
    return output.buttons;
}
