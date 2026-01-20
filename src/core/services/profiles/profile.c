// profile.c - Universal Profile System Implementation
//
// Provides shared profile switching logic for all output devices.
// Supports per-output-target profile sets with shared fallback.

#include "profile.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

// External dependencies (feedback and visual indication)
#include "core/services/leds/leds.h"
#include "core/services/profiles/profile_indicator.h"
#include "core/services/players/feedback.h"

// Flash storage
#include "core/services/storage/flash.h"

// ============================================================================
// PROFILE SYSTEM STATE
// ============================================================================

// Configuration
static const profile_config_t* config = NULL;

// Legacy: Active index per output target (for backwards compatibility)
static uint8_t active_index[MAX_OUTPUT_TARGETS] = {0};

// Per-player profile state
static player_profile_state_t player_profiles[MAX_PLAYERS] = {0};

// Per-player switch combo state
typedef struct {
    uint32_t p_select_hold_start;
    bool p_select_was_held;
    bool p_dpad_up_was_pressed;
    bool p_dpad_down_was_pressed;
    bool p_dpad_left_was_pressed;
    bool p_dpad_right_was_pressed;
    bool p_initial_trigger_done;
} player_combo_state_t;

static player_combo_state_t player_combo[MAX_PLAYERS] = {0};

// Legacy combo state (player 0)
#define select_hold_start       player_combo[0].p_select_hold_start
#define select_was_held         player_combo[0].p_select_was_held
#define dpad_up_was_pressed     player_combo[0].p_dpad_up_was_pressed
#define dpad_down_was_pressed   player_combo[0].p_dpad_down_was_pressed
#define dpad_left_was_pressed   player_combo[0].p_dpad_left_was_pressed
#define dpad_right_was_pressed  player_combo[0].p_dpad_right_was_pressed
#define initial_trigger_done    player_combo[0].p_initial_trigger_done

// Timing constants
static const uint32_t INITIAL_HOLD_TIME_MS = 2000;  // Must hold 2 seconds for first trigger

// SOCD last-input-wins state tracking (per player)
typedef struct {
    uint8_t ud_last;    // 0=none, 1=up, 2=down
    uint8_t lr_last;    // 0=none, 1=left, 2=right
    bool up_was_pressed;
    bool down_was_pressed;
    bool left_was_pressed;
    bool right_was_pressed;
} socd_state_t;

static socd_state_t socd_state[MAX_PLAYERS] = {0};

// Callbacks
static uint8_t (*get_player_count)(void) = NULL;
static profile_switch_callback_t on_switch_callback = NULL;
static profile_player_switch_callback_t on_player_switch_callback = NULL;
static output_mode_callback_t on_output_mode_callback = NULL;

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

void profile_set_player_switch_callback(profile_player_switch_callback_t callback)
{
    on_player_switch_callback = callback;
}

void profile_set_output_mode_callback(output_mode_callback_t callback)
{
    on_output_mode_callback = callback;
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
    leds_indicate_profile(index);

    uint8_t player_count = get_player_count ? get_player_count() : 0;
    profile_indicator_trigger(index, player_count);

    // Save to flash
    profile_save_to_flash(output);

    const char* name = profile_get_name(output, index);
    printf("[profile] Switched to: %s (output=%d)\n", name ? name : "(unknown)", output);
}

void profile_cycle_next(output_target_t output)
{
    uint8_t count = profile_get_count(output);

    // If no built-in profiles, fall back to custom profiles
    if (count == 0) {
        uint8_t custom_count = flash_get_total_profile_count();
        if (custom_count > 1) {
            flash_cycle_profile_next();

            // Trigger feedback for custom profile switch
            uint8_t new_index = flash_get_active_profile_index();
            leds_indicate_profile(new_index);
            uint8_t player_count = get_player_count ? get_player_count() : 0;
            profile_indicator_trigger(new_index, player_count);

            // Get profile name for logging
            const custom_profile_t* custom = flash_get_active_custom_profile();
            const char* name = custom ? custom->name : "Default";
            printf("[profile] Custom profile switched to: %s (index=%d)\n", name, new_index);
        }
        return;
    }

    uint8_t current = profile_get_active_index(output);
    uint8_t new_index = (current + 1) % count;
    profile_set_active(output, new_index);
}

void profile_cycle_prev(output_target_t output)
{
    uint8_t count = profile_get_count(output);

    // If no built-in profiles, fall back to custom profiles
    if (count == 0) {
        uint8_t custom_count = flash_get_total_profile_count();
        if (custom_count > 1) {
            flash_cycle_profile_prev();

            // Trigger feedback for custom profile switch
            uint8_t new_index = flash_get_active_profile_index();
            leds_indicate_profile(new_index);
            uint8_t player_count = get_player_count ? get_player_count() : 0;
            profile_indicator_trigger(new_index, player_count);

            // Get profile name for logging
            const custom_profile_t* custom = flash_get_active_custom_profile();
            const char* name = custom ? custom->name : "Default";
            printf("[profile] Custom profile switched to: %s (index=%d)\n", name, new_index);
        }
        return;
    }

    uint8_t current = profile_get_active_index(output);
    uint8_t new_index = (current == 0) ? (count - 1) : (current - 1);
    profile_set_active(output, new_index);
}

// ============================================================================
// PER-PLAYER PROFILE API
// ============================================================================

const profile_t* profile_get_active_for_player(output_target_t output, uint8_t player_index)
{
    if (player_index >= MAX_PLAYERS) return NULL;

    const profile_set_t* set = get_profile_set(output);
    if (!set || set->profile_count == 0) {
        return NULL;
    }

    uint8_t idx = player_profiles[player_index].profile_index;
    if (idx >= set->profile_count) {
        idx = 0;
    }

    return &set->profiles[idx];
}

uint8_t profile_get_player_index(output_target_t output, uint8_t player_index)
{
    if (player_index >= MAX_PLAYERS) return 0;
    return player_profiles[player_index].profile_index;
}

void profile_set_player_active(output_target_t output, uint8_t player_index, uint8_t profile_index)
{
    if (player_index >= MAX_PLAYERS) return;

    const profile_set_t* set = get_profile_set(output);
    if (!set || set->profile_count == 0 || profile_index >= set->profile_count) {
        return;
    }

    player_profiles[player_index].profile_index = profile_index;
    player_profiles[player_index].dirty = true;

    // Also update legacy active_index for player 0 (backwards compatibility)
    if (player_index == 0 && output >= 0 && output < MAX_OUTPUT_TARGETS) {
        active_index[output] = profile_index;
    }

    // Notify callbacks
    if (on_player_switch_callback) {
        on_player_switch_callback(output, player_index, profile_index);
    }
    if (player_index == 0 && on_switch_callback) {
        on_switch_callback(output, profile_index);
    }

    // Trigger per-player feedback using new feedback system
    feedback_set_rumble(player_index, 192, 192);  // Rumble this player's controller
    feedback_set_led_player(player_index, profile_index + 1);  // LED shows profile number

    // Also trigger NeoPixel for visual indication (global)
    leds_indicate_profile(profile_index);

    // Save to flash (for now, just save player 0's profile)
    if (player_index == 0) {
        profile_save_to_flash(output);
    }

    const char* name = profile_get_name(output, profile_index);
    printf("[profile] Player %d switched to: %s (output=%d)\n",
           player_index, name ? name : "(unknown)", output);
}

void profile_cycle_player_next(output_target_t output, uint8_t player_index)
{
    if (player_index >= MAX_PLAYERS) return;

    uint8_t count = profile_get_count(output);
    if (count == 0) return;

    uint8_t current = player_profiles[player_index].profile_index;
    uint8_t new_index = (current + 1) % count;
    profile_set_player_active(output, player_index, new_index);
}

void profile_cycle_player_prev(output_target_t output, uint8_t player_index)
{
    if (player_index >= MAX_PLAYERS) return;

    uint8_t count = profile_get_count(output);
    if (count == 0) return;

    uint8_t current = player_profiles[player_index].profile_index;
    uint8_t new_index = (current == 0) ? (count - 1) : (current - 1);
    profile_set_player_active(output, player_index, new_index);
}

// ============================================================================
// PER-PLAYER COMBO DETECTION
// ============================================================================

void profile_check_player_switch_combo(uint8_t player_index, uint32_t buttons)
{
    if (player_index >= MAX_PLAYERS) return;

    output_target_t output = router_get_primary_output();
    if (output == OUTPUT_TARGET_NONE) return;

    player_combo_state_t* combo = &player_combo[player_index];

    // Check button states (buttons are active-high: 1 = pressed)
    bool select_held = ((buttons & JP_BUTTON_S1) != 0);
    bool dpad_up_pressed = ((buttons & JP_BUTTON_DU) != 0);
    bool dpad_down_pressed = ((buttons & JP_BUTTON_DD) != 0);

    // Select released - reset everything for this player
    if (!select_held) {
        combo->p_select_hold_start = 0;
        combo->p_select_was_held = false;
        combo->p_dpad_up_was_pressed = false;
        combo->p_dpad_down_was_pressed = false;
        combo->p_dpad_left_was_pressed = false;
        combo->p_dpad_right_was_pressed = false;
        combo->p_initial_trigger_done = false;
        return;
    }

    // Select is held
    uint32_t current_time = to_ms_since_boot(get_absolute_time());

    if (!combo->p_select_was_held) {
        combo->p_select_hold_start = current_time;
        combo->p_select_was_held = true;
        // D-pad state will be tracked in the wait period below
    }

    uint32_t hold_duration = current_time - combo->p_select_hold_start;
    bool can_trigger = combo->p_initial_trigger_done || (hold_duration >= INITIAL_HOLD_TIME_MS);

    if (!can_trigger) {
        // Still waiting for initial hold
        return;
    }

    // Check if this player's feedback is still active
    // But still track D-pad state to avoid missing edges
    feedback_state_t* fb = feedback_get_state(player_index);
    if (fb && fb->rumble.left > 0) {
        combo->p_dpad_up_was_pressed = dpad_up_pressed;
        combo->p_dpad_down_was_pressed = dpad_down_pressed;
        return;  // Still rumbling from last switch
    }

    // Initial trigger: when 2-second hold completes, trigger immediately if D-pad is held
    // Subsequent triggers: require rising edge (release and press again)
    bool trigger_up = false;
    bool trigger_down = false;

    if (!combo->p_initial_trigger_done) {
        // Initial trigger - just check if D-pad is currently pressed
        trigger_up = dpad_up_pressed;
        trigger_down = dpad_down_pressed;
    } else {
        // Subsequent triggers - require rising edge
        trigger_up = dpad_up_pressed && !combo->p_dpad_up_was_pressed;
        trigger_down = dpad_down_pressed && !combo->p_dpad_down_was_pressed;
    }

    // D-pad Up - cycle profile forward
    if (trigger_up) {
        uint8_t count = profile_get_count(output);
        if (count > 1) {
            profile_cycle_player_next(output, player_index);
            combo->p_initial_trigger_done = true;
        }
    }
    combo->p_dpad_up_was_pressed = dpad_up_pressed;

    // D-pad Down - cycle profile backward
    if (trigger_down && !trigger_up) {  // Don't trigger both directions
        uint8_t count = profile_get_count(output);
        if (count > 1) {
            profile_cycle_player_prev(output, player_index);
            combo->p_initial_trigger_done = true;
        }
    }
    combo->p_dpad_down_was_pressed = dpad_down_pressed;
}

bool profile_player_switch_combo_active(uint8_t player_index)
{
    if (player_index >= MAX_PLAYERS) return false;
    return player_combo[player_index].p_select_was_held &&
           player_combo[player_index].p_initial_trigger_done;
}

// ============================================================================
// LEGACY PROFILE SWITCH COMBO DETECTION
// ============================================================================
// SELECT + D-pad Up/Down to cycle profiles
// Requires holding SELECT for 2 seconds before first switch

void profile_check_switch_combo(uint32_t buttons)
{
    // Use primary output for switching
    output_target_t output = router_get_primary_output();
    if (output == OUTPUT_TARGET_NONE) return;

    uint8_t player_count = get_player_count ? get_player_count() : 0;
    if (player_count == 0) return;  // No controllers connected

    // Check button states (buttons are active-high: 1 = pressed)
    bool select_held = ((buttons & JP_BUTTON_S1) != 0);
    bool dpad_up_pressed = ((buttons & JP_BUTTON_DU) != 0);
    bool dpad_down_pressed = ((buttons & JP_BUTTON_DD) != 0);
    bool dpad_left_pressed = ((buttons & JP_BUTTON_DL) != 0);
    bool dpad_right_pressed = ((buttons & JP_BUTTON_DR) != 0);

    // Select released - reset everything
    if (!select_held) {
        select_hold_start = 0;
        select_was_held = false;
        dpad_up_was_pressed = false;
        dpad_down_was_pressed = false;
        dpad_left_was_pressed = false;
        dpad_right_was_pressed = false;
        initial_trigger_done = false;
        return;
    }

    // Select is held
    uint32_t current_time = to_ms_since_boot(get_absolute_time());

    if (!select_was_held) {
        // Select just pressed - start timer
        select_hold_start = current_time;
        select_was_held = true;
    }

    uint32_t select_hold_duration = current_time - select_hold_start;

    // Check if initial 2-second hold period has elapsed
    bool can_trigger = initial_trigger_done || (select_hold_duration >= INITIAL_HOLD_TIME_MS);

    if (!can_trigger) {
        // Still waiting for initial 2-second hold
        return;
    }

    // Don't allow switching while feedback is still active
    // But still track D-pad state to avoid missing edges
    if (leds_is_indicating() || profile_indicator_is_active()) {
        dpad_up_was_pressed = dpad_up_pressed;
        dpad_down_was_pressed = dpad_down_pressed;
        dpad_left_was_pressed = dpad_left_pressed;
        dpad_right_was_pressed = dpad_right_pressed;
        return;
    }

    // Initial trigger: when 2-second hold completes, trigger immediately if D-pad is held
    // Subsequent triggers: require rising edge (release and press again)
    bool trigger_up = false;
    bool trigger_down = false;

    if (!initial_trigger_done) {
        // Initial trigger - just check if D-pad is currently pressed
        trigger_up = dpad_up_pressed;
        trigger_down = dpad_down_pressed;
    } else {
        // Subsequent triggers - require rising edge
        trigger_up = dpad_up_pressed && !dpad_up_was_pressed;
        trigger_down = dpad_down_pressed && !dpad_down_was_pressed;
    }

    // D-pad Up - cycle profile forward
    if (trigger_up) {
        uint8_t count = profile_get_count(output);
        if (count > 1) {
            profile_cycle_next(output);
            initial_trigger_done = true;
        }
    }
    dpad_up_was_pressed = dpad_up_pressed;

    // D-pad Down - cycle profile backward
    if (trigger_down && !trigger_up) {  // Don't trigger both directions
        uint8_t count = profile_get_count(output);
        if (count > 1) {
            profile_cycle_prev(output);
            initial_trigger_done = true;
        }
    }
    dpad_down_was_pressed = dpad_down_pressed;

    // D-pad Left - cycle output mode backward on rising edge
    if (dpad_left_pressed && !dpad_left_was_pressed) {
        if (on_output_mode_callback) {
            if (on_output_mode_callback(-1)) {
                initial_trigger_done = true;
            }
        }
    }
    dpad_left_was_pressed = dpad_left_pressed;

    // D-pad Right - cycle output mode forward on rising edge
    if (dpad_right_pressed && !dpad_right_was_pressed) {
        if (on_output_mode_callback) {
            if (on_output_mode_callback(+1)) {
                initial_trigger_done = true;
            }
        }
    }
    dpad_right_was_pressed = dpad_right_pressed;
}

bool profile_switch_combo_active(void)
{
    // Combo is active when Select has been held long enough
    return select_was_held && initial_trigger_done;
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
// SOCD RESOLUTION
// ============================================================================

// Apply SOCD cleaning to D-pad buttons
// player_index: for last-win state tracking (use 0 for legacy/single player)
// Returns the cleaned button state
static uint32_t apply_socd(uint32_t buttons, socd_mode_t mode, uint8_t player_index)
{
    if (mode == SOCD_PASSTHROUGH || player_index >= MAX_PLAYERS) {
        return buttons;
    }

    bool up = (buttons & JP_BUTTON_DU) != 0;
    bool down = (buttons & JP_BUTTON_DD) != 0;
    bool left = (buttons & JP_BUTTON_DL) != 0;
    bool right = (buttons & JP_BUTTON_DR) != 0;

    socd_state_t* state = &socd_state[player_index];

    switch (mode) {
        case SOCD_NEUTRAL:
            // Cancel opposite directions (both become neutral)
            if (up && down) {
                buttons &= ~(JP_BUTTON_DU | JP_BUTTON_DD);
            }
            if (left && right) {
                buttons &= ~(JP_BUTTON_DL | JP_BUTTON_DR);
            }
            break;

        case SOCD_UP_PRIORITY:
            // U+D = U (up wins), L+R = neutral
            if (up && down) {
                buttons &= ~JP_BUTTON_DD;  // Remove down, keep up
            }
            if (left && right) {
                buttons &= ~(JP_BUTTON_DL | JP_BUTTON_DR);  // Cancel both
            }
            break;

        case SOCD_LAST_WIN:
            // Last input wins - track rising edges
            // Up/Down
            if (up && down) {
                // Both pressed - determine which was pressed last
                if (up && !state->up_was_pressed) {
                    // Up just pressed
                    state->ud_last = 1;
                }
                if (down && !state->down_was_pressed) {
                    // Down just pressed
                    state->ud_last = 2;
                }
                // Apply last winner
                if (state->ud_last == 1) {
                    buttons &= ~JP_BUTTON_DD;  // Keep up
                } else if (state->ud_last == 2) {
                    buttons &= ~JP_BUTTON_DU;  // Keep down
                }
            } else if (up) {
                state->ud_last = 1;
            } else if (down) {
                state->ud_last = 2;
            } else {
                state->ud_last = 0;
            }

            // Left/Right
            if (left && right) {
                // Both pressed - determine which was pressed last
                if (left && !state->left_was_pressed) {
                    // Left just pressed
                    state->lr_last = 1;
                }
                if (right && !state->right_was_pressed) {
                    // Right just pressed
                    state->lr_last = 2;
                }
                // Apply last winner
                if (state->lr_last == 1) {
                    buttons &= ~JP_BUTTON_DR;  // Keep left
                } else if (state->lr_last == 2) {
                    buttons &= ~JP_BUTTON_DL;  // Keep right
                }
            } else if (left) {
                state->lr_last = 1;
            } else if (right) {
                state->lr_last = 2;
            } else {
                state->lr_last = 0;
            }

            // Update previous state for next frame
            state->up_was_pressed = up;
            state->down_was_pressed = down;
            state->left_was_pressed = left;
            state->right_was_pressed = right;
            break;

        default:
            break;
    }

    return buttons;
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
    // Suppress combo buttons when profile switch is active
    // This prevents Select + D-pad from being output during switching
    // Note: active-high (bit set = pressed, bit clear = released)
    if (profile_switch_combo_active()) {
        // Clear combo buttons to "released"
        input_buttons &= ~JP_BUTTON_S1;   // Select
        input_buttons &= ~JP_BUTTON_DU;   // D-pad Up
        input_buttons &= ~JP_BUTTON_DD;   // D-pad Down
        input_buttons &= ~JP_BUTTON_DL;   // D-pad Left
        input_buttons &= ~JP_BUTTON_DR;   // D-pad Right
    }

    // Initialize output with passthrough values
    memset(output, 0, sizeof(profile_output_t));
    output->buttons = input_buttons;  // Start with passthrough
    output->left_x = lx;
    output->left_y = ly;
    output->right_x = rx;
    output->right_y = ry;
    output->l2_analog = l2;
    output->r2_analog = r2;

    // Set L2/R2 digital buttons based on analog threshold (if threshold > 0)
    // Threshold of 0 means disabled (never trigger digital from analog)
    // This replaces the input driver threshold logic, allowing per-profile control
    if (profile) {
        if (profile->l2_threshold > 0 && l2 >= profile->l2_threshold) {
            output->buttons |= JP_BUTTON_L2;
        }
        if (profile->r2_threshold > 0 && r2 >= profile->r2_threshold) {
            output->buttons |= JP_BUTTON_R2;
        }
    }

    // Process button combos first (before individual mappings)
    // Combos can add buttons and optionally consume their input buttons
    // Note: Router uses active-high (1 = pressed, 0 = released)
    uint32_t combo_consumed = 0;  // Track which inputs were consumed by combos
    if (profile && profile->combo_map && profile->combo_map_count > 0) {
        for (uint8_t i = 0; i < profile->combo_map_count; i++) {
            const button_combo_entry_t* combo = &profile->combo_map[i];

            // Check if all combo inputs are pressed (active-high: pressed = bit set)
            // All bits in combo->inputs must be set in input_buttons
            bool combo_inputs_pressed = ((input_buttons & combo->inputs) == combo->inputs);

            // For exclusive combos, also check that NO other buttons are pressed
            bool combo_active = combo_inputs_pressed;
            if (combo_active && combo->exclusive) {
                // Exclusive: input_buttons must be EXACTLY combo->inputs (no extra buttons)
                combo_active = (input_buttons == combo->inputs);
            }

            if (combo_active) {
                // Combo is active - add output button(s)
                // Set output bits to 1 (pressed) for combo outputs
                output->buttons |= combo->output;

                // If consuming inputs, track them for removal
                if (combo->consume_inputs) {
                    combo_consumed |= combo->inputs;
                }
            }
        }

        // Remove consumed inputs from output (clear bits = released in active-high)
        output->buttons &= ~combo_consumed;
    }

    if (!profile || !profile->button_map || profile->button_map_count == 0) {
        // No mapping, passthrough (combos already applied above)
        return;
    }

    // Build output button state (active-high: 1 = pressed, 0 = released)
    uint32_t output_buttons = 0;
    uint32_t mapped_inputs = 0;

    // Apply explicit mappings
    for (uint8_t i = 0; i < profile->button_map_count; i++) {
        const button_map_entry_t* entry = &profile->button_map[i];

        // Check if input button is pressed (active-high: pressed = bit set)
        bool pressed = ((input_buttons & entry->input) != 0);

        if (pressed) {
            // Set output button(s)
            output_buttons |= entry->output;

            // Apply analog target if specified
            if (entry->analog != ANALOG_TARGET_NONE) {
                apply_analog_target(entry->analog, entry->analog_value, output);
            }
        }

        // Mark this input as mapped (even if not pressed)
        mapped_inputs |= entry->input;
    }

    // Passthrough unmapped buttons (active-high)
    uint32_t unmapped_inputs = ~mapped_inputs;
    uint32_t pressed_unmapped = input_buttons & unmapped_inputs;
    output_buttons |= pressed_unmapped;

    // Output is active-high
    output->buttons = output_buttons;

    // Apply SOCD cleaning if configured
    if (profile->socd_mode != SOCD_PASSTHROUGH) {
        // Use player 0 for SOCD state tracking (legacy single-player path)
        output->buttons = apply_socd(output->buttons, profile->socd_mode, 0);
    }

    // Determine effective left stick sensitivity (check modifiers first)
    float left_sens = profile->left_stick_sensitivity;
    for (uint8_t i = 0; i < profile->left_stick_modifier_count; i++) {
        const stick_modifier_t* mod = &profile->left_stick_modifiers[i];
        if (input_buttons & mod->trigger) {
            left_sens = mod->sensitivity;
            if (mod->consume_trigger) {
                output->buttons &= ~mod->trigger;
            }
            break;  // First matching modifier wins
        }
    }

    // Determine effective right stick sensitivity (check modifiers first)
    float right_sens = profile->right_stick_sensitivity;
    for (uint8_t i = 0; i < profile->right_stick_modifier_count; i++) {
        const stick_modifier_t* mod = &profile->right_stick_modifiers[i];
        if (input_buttons & mod->trigger) {
            right_sens = mod->sensitivity;
            if (mod->consume_trigger) {
                output->buttons &= ~mod->trigger;
            }
            break;  // First matching modifier wins
        }
    }

    // Apply left stick sensitivity scaling
    if (left_sens != 1.0f) {
        if (!output->left_x_override) {
            int16_t rel_x = (int16_t)output->left_x - 128;
            output->left_x = (uint8_t)(128 + (int16_t)(rel_x * left_sens));
        }
        if (!output->left_y_override) {
            int16_t rel_y = (int16_t)output->left_y - 128;
            output->left_y = (uint8_t)(128 + (int16_t)(rel_y * left_sens));
        }
    }

    // Apply right stick sensitivity scaling
    if (right_sens != 1.0f) {
        if (!output->right_x_override) {
            int16_t rel_x = (int16_t)output->right_x - 128;
            output->right_x = (uint8_t)(128 + (int16_t)(rel_x * right_sens));
        }
        if (!output->right_y_override) {
            int16_t rel_y = (int16_t)output->right_y - 128;
            output->right_y = (uint8_t)(128 + (int16_t)(rel_y * right_sens));
        }
    }

    // Apply trigger behavior (if triggers weren't overridden by button mappings)
    // Note: Use output->buttons which includes threshold-based L2/R2 for XInput controllers
    if (!output->l2_analog_override) {
        switch (profile->l2_behavior) {
            case TRIGGER_DISABLED:
                output->l2_analog = 0;
                output->buttons &= ~JP_BUTTON_L2;  // Clear digital too
                break;
            case TRIGGER_DIGITAL_ONLY:
                output->l2_analog = 0;
                break;
            case TRIGGER_FULL_PRESS:
                if (output->buttons & JP_BUTTON_L2) {
                    output->l2_analog = 255;
                }
                break;
            case TRIGGER_LIGHT_PRESS:
                if (output->buttons & JP_BUTTON_L2) {
                    output->l2_analog = profile->l2_analog_value;
                }
                break;
            case TRIGGER_INSTANT:
                // Analog zeroed, digital handled by threshold logic elsewhere
                output->l2_analog = 0;
                break;
            case TRIGGER_PASSTHROUGH:
            default:
                // Already set above
                break;
        }
    }

    if (!output->r2_analog_override) {
        switch (profile->r2_behavior) {
            case TRIGGER_DISABLED:
                output->r2_analog = 0;
                output->buttons &= ~JP_BUTTON_R2;  // Clear digital too
                break;
            case TRIGGER_DIGITAL_ONLY:
                output->r2_analog = 0;
                break;
            case TRIGGER_FULL_PRESS:
                if (output->buttons & JP_BUTTON_R2) {
                    output->r2_analog = 255;
                }
                break;
            case TRIGGER_LIGHT_PRESS:
                if (output->buttons & JP_BUTTON_R2) {
                    output->r2_analog = profile->r2_analog_value;
                }
                break;
            case TRIGGER_INSTANT:
                // Analog zeroed, digital handled by threshold logic elsewhere
                output->r2_analog = 0;
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
