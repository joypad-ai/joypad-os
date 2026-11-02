// profile.c - Universal Profile System Implementation
//
// Provides shared profile switching logic for all output devices.
// Supports per-output-target profile sets with shared fallback.

#include "profile.h"
#include "runtime_profile.h"
#include "platform/platform.h"
#include <stdio.h>
#include <stddef.h>
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

// Shared turbo-rate ladder (see profile.h). index 0 = off; 1..6 → 30/20/15/12/10/7.5 Hz.
uint8_t profile_autofire_rate_ms(uint8_t index)
{
    static const uint8_t ladder[AUTOFIRE_RATE_COUNT] = {
        0, AUTOFIRE_30HZ, AUTOFIRE_20HZ, AUTOFIRE_15HZ,
        AUTOFIRE_12HZ, AUTOFIRE_10HZ, AUTOFIRE_7HZ,
    };
    return (index < AUTOFIRE_RATE_COUNT) ? ladder[index] : 0;
}

uint8_t profile_autofire_index_from_ms(uint8_t period_ms)
{
    if (period_ms == 0) return 0;  // off
    uint8_t best = 1, best_diff = 255;
    for (uint8_t i = 1; i < AUTOFIRE_RATE_COUNT; i++) {
        uint8_t p = profile_autofire_rate_ms(i);
        uint8_t d = (p > period_ms) ? (p - period_ms) : (period_ms - p);
        if (d < best_diff) { best_diff = d; best = i; }
    }
    return best;
}

uint8_t profile_get_active_output_mode(output_target_t output)
{
    // A custom profile carries its own output_mode; a built-in exposes it on the
    // profile_t. flash_get_active_custom_profile() returns non-NULL only when a
    // custom is active, so this resolves the effective mode either way.
    const custom_profile_t* cp = flash_get_active_custom_profile();
    if (cp) return cp->output_mode;
    const profile_t* p = profile_get_by_index(output, profile_get_active_index(output));
    return p ? p->output_mode : 0;
}

const char* profile_get_output_modes(const char* const** names, uint8_t* count)
{
    if (!config || !config->output_mode_names || config->output_mode_count == 0) {
        if (names) *names = NULL;
        if (count) *count = 0;
        return NULL;
    }
    if (names) *names = config->output_mode_names;
    if (count) *count = config->output_mode_count;
    return config->output_type_name;
}

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
    // A live runtime profile (SELECT-hold autofire / remap gesture) overrides
    // the normal profile for this output while it has any entries.
    const profile_t* rt = runtime_profile_get_active(output);
    if (rt) return rt;

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

const profile_t* profile_get_by_index(output_target_t output, uint8_t index)
{
    const profile_set_t* set = get_profile_set(output);
    if (!set || index >= set->profile_count) {
        return NULL;
    }
    return &set->profiles[index];
}

// ============================================================================
// PROFILE SWITCHING
// ============================================================================

// Shared core for the persistent (profile_set_active) and ephemeral
// (profile_select_active) variants. persist=true writes the new index to
// flash; persist=false updates RAM and triggers feedback only. The latter
// is for joypad-live / crowd-control flows where flash writes per switch
// would burn out the chip over thousands of changes per session.
static void profile_set_active_internal(output_target_t output, uint8_t index, bool persist)
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

    if (persist) {
        profile_save_to_flash(output);
    }

    const char* name = profile_get_name(output, index);
    printf("[profile] %s: %s (output=%d)\n",
           persist ? "Switched" : "Selected (RAM only)",
           name ? name : "(unknown)", output);
}

void profile_set_active(output_target_t output, uint8_t index)
{
    profile_set_active_internal(output, index, /*persist=*/true);
}

// Ephemeral variant: same effect for the current session, no flash write.
// On reboot, the previously-persisted selection comes back.
void profile_select_active(output_target_t output, uint8_t index)
{
    profile_set_active_internal(output, index, /*persist=*/false);
}

// Unified cycle helpers — apps with built-in profiles share the same unified
// [built-ins, customs] index space the web config uses (see PROFILE.LIST).
// Apps with no built-ins keep their existing custom-only cycle path.
//
// Index mapping:
//   unified 0..builtin_count-1                            = built-in profiles
//   unified builtin_count..builtin_count+custom_count-1   = custom profiles
//
// Flash state representation matches:
//   flash_active = 0       → no custom selected; built-in wins per
//                            profile_get_active_index(output)
//   flash_active = 1..N    → custom N-1 selected (router gives this
//                            precedence over the built-in active)
//
// Switching to a built-in MUST clear flash_active to 0 — otherwise the
// runtime keeps applying the previously-selected custom on top of it.

static void profile_apply_unified_index(output_target_t output,
                                         uint8_t unified_index,
                                         uint8_t builtin_count)
{
    // Use the DEFERRED variant of flash_set_active_profile_index. The
    // SELECT+D-pad cycle hotkey can fire many times in quick succession;
    // the immediate (flash_save_now) variant blocks ~50 ms with interrupts
    // disabled which stalls USB host polling and console-output PIO
    // callbacks long enough to hang the firmware on usb2gc / usb2pce /
    // etc. Debounced save commits ~5 s after the last cycle event.
    if (unified_index < builtin_count) {
        // Built-in profile — clear any custom override so the built-in
        // actually takes effect.
        flash_set_active_profile_index_deferred(0);
        profile_set_active(output, unified_index);
    } else {
        // Custom profile (flash_active is 1-based: 0=default override,
        // 1=first custom).
        uint8_t custom_idx = unified_index - builtin_count;
        flash_set_active_profile_index_deferred((uint8_t)(custom_idx + 1));
    }
}

static uint8_t profile_get_unified_active_index(output_target_t output,
                                                  uint8_t builtin_count)
{
    uint8_t flash_active = flash_get_active_profile_index();
    if (flash_active > 0) {
        // Custom selected — return its unified slot.
        return builtin_count + (uint8_t)(flash_active - 1);
    }
    // Otherwise the built-in's own active index.
    return profile_get_active_index(output);
}

static void profile_announce_switch(output_target_t output,
                                     uint8_t unified_index,
                                     uint8_t builtin_count)
{
    leds_indicate_profile(unified_index);
    uint8_t player_count = get_player_count ? get_player_count() : 0;
    profile_indicator_trigger(unified_index, player_count);

    const char* name = NULL;
    if (unified_index < builtin_count) {
        name = profile_get_name(output, unified_index);
        printf("[profile] Built-in profile switched to: %s (index=%u)\n",
               name ? name : "?", unified_index);
    } else {
        const custom_profile_t* custom = flash_get_active_custom_profile();
        name = custom ? custom->name : "Default";
        printf("[profile] Custom profile switched to: %s (unified=%u)\n",
               name, unified_index);
    }
}

// True if a unified profile index is selectable by the hotkey cycle. Custom
// profiles are always enabled; built-ins can be individually disabled via the
// web config (flash builtin_disabled_mask).
static bool profile_unified_index_enabled(uint8_t idx, uint8_t builtin_count)
{
    if (idx >= builtin_count) return true;               // custom profile
    return !((flash_get_builtin_disabled_mask() >> idx) & 1u);
}

// Step from `current` in direction `dir` (+1/-1), skipping disabled built-ins.
// Returns the next enabled unified index, or `current` when there is none
// (clamp at the ends, or every candidate disabled).
static uint8_t profile_cycle_find(uint8_t current, uint8_t total,
                                  uint8_t builtin_count, int dir, bool wrap)
{
    uint8_t idx = current;
    for (uint8_t step = 0; step < total; step++) {
        int ni = (int)idx + dir;
        if (ni < 0)            { if (!wrap) return current; ni = total - 1; }
        else if (ni >= total)  { if (!wrap) return current; ni = 0; }
        idx = (uint8_t)ni;
        if (profile_unified_index_enabled(idx, builtin_count)) return idx;
    }
    return current;  // nothing else enabled
}

void profile_cycle_next(output_target_t output, bool wrap)
{
    uint8_t builtin_count = profile_get_count(output);

    // Apps with no built-in profiles use the legacy custom-only cycle
    // path (preserves existing behavior for n642dc / gc2dc / etc.).
    if (builtin_count == 0) {
        uint8_t total_flash = flash_get_total_profile_count();
        if (total_flash > 1) {
            uint8_t cur = flash_get_active_profile_index();
            if (!wrap && cur + 1 >= total_flash) return;  // clamp at last
            flash_set_active_profile_index((uint8_t)((cur + 1) % total_flash));
            uint8_t new_index = flash_get_active_profile_index();
            leds_indicate_profile(new_index);
            uint8_t player_count = get_player_count ? get_player_count() : 0;
            profile_indicator_trigger(new_index, player_count);
            const custom_profile_t* custom = flash_get_active_custom_profile();
            const char* name = custom ? custom->name : "Default";
            printf("[profile] Custom profile switched to: %s (index=%d)\n", name, new_index);
        }
        return;
    }

    // Cycle the unified [built-ins, customs] space, skipping any built-in that
    // has been disabled in the web config.
    uint8_t custom_count = (uint8_t)(flash_get_total_profile_count() - 1);
    uint8_t total = builtin_count + custom_count;
    if (total <= 1) return;

    uint8_t current = profile_get_unified_active_index(output, builtin_count);
    uint8_t next = profile_cycle_find(current, total, builtin_count, +1, wrap);
    if (next == current) return;  // clamp / nothing enabled ahead
    profile_apply_unified_index(output, next, builtin_count);
    profile_announce_switch(output, next, builtin_count);
}

void profile_cycle_prev(output_target_t output, bool wrap)
{
    uint8_t builtin_count = profile_get_count(output);

    if (builtin_count == 0) {
        uint8_t total_flash = flash_get_total_profile_count();
        if (total_flash > 1) {
            uint8_t cur = flash_get_active_profile_index();
            if (!wrap && cur == 0) return;  // clamp at first
            flash_set_active_profile_index(cur == 0 ? (uint8_t)(total_flash - 1)
                                                    : (uint8_t)(cur - 1));
            uint8_t new_index = flash_get_active_profile_index();
            leds_indicate_profile(new_index);
            uint8_t player_count = get_player_count ? get_player_count() : 0;
            profile_indicator_trigger(new_index, player_count);
            const custom_profile_t* custom = flash_get_active_custom_profile();
            const char* name = custom ? custom->name : "Default";
            printf("[profile] Custom profile switched to: %s (index=%d)\n", name, new_index);
        }
        return;
    }

    uint8_t custom_count = (uint8_t)(flash_get_total_profile_count() - 1);
    uint8_t total = builtin_count + custom_count;
    if (total <= 1) return;

    uint8_t current = profile_get_unified_active_index(output, builtin_count);
    uint8_t prev = profile_cycle_find(current, total, builtin_count, -1, wrap);
    if (prev == current) return;  // clamp / nothing enabled behind
    profile_apply_unified_index(output, prev, builtin_count);
    profile_announce_switch(output, prev, builtin_count);
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
    // For now, save primary output's index
    // TODO: Store per-output indices if needed
    if (output < 0 || output >= MAX_OUTPUT_TARGETS) {
        return;
    }

    // Persist through the live settings record, never a stack copy.
    // flash_save() takes all 256 bytes of flash_t, so filling in one field of
    // a local and handing it over writes uninitialized stack across every
    // other global setting and all four custom profiles. flash_save() also
    // stamps magic and schema_version itself, so the resulting record passes
    // both validation checks on load and nothing downstream can detect it.
    flash_t* settings = flash_get_settings();
    if (!settings) {
        return;  // flash_init() hasn't run — nothing safe to merge into.
    }

    // Idempotent: the SELECT + D-pad hotkey re-enters this on every switch,
    // and each flash_save() costs a journal slot.
    if (settings->active_profile_index == active_index[output]) {
        return;
    }

    settings->active_profile_index = active_index[output];
    flash_save(settings);
}

// ============================================================================
// SOCD RESOLUTION
// ============================================================================

// Apply SOCD cleaning to D-pad buttons
// player_index: for last-win state tracking (use 0 for legacy/single player)
// Returns the cleaned button state
uint32_t apply_socd(uint32_t buttons, socd_mode_t mode, uint8_t player_index)
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
                   uint8_t rz,
                   profile_output_t* output)
{

    // Zero output fields only — autofire_start_ms at the end is preserved across calls.
    memset(output, 0, offsetof(profile_output_t, autofire_start_ms));
    output->buttons = input_buttons;  // Start with passthrough
    output->left_x = lx;
    output->left_y = ly;
    output->right_x = rx;
    output->right_y = ry;
    output->l2_analog = l2;
    output->r2_analog = r2;
    output->rz_analog = rz;

    // Normalize: digital-only triggers (fight sticks, arcade pads) report
    // digital L2/R2 with no analog data. Synthesize full analog press so
    // threshold logic works uniformly across all controller types.
    if ((input_buttons & JP_BUTTON_L2) && l2 == 0) {
        l2 = 255;
        output->l2_analog = 255;
    }
    if ((input_buttons & JP_BUTTON_R2) && r2 == 0) {
        r2 = 255;
        output->r2_analog = 255;
    }

    // Optional analog→digital trigger threshold (profile-driven).
    // When a built-in console profile sets l2_threshold > 0 it OVERRIDES
    // the input L2/R2 button bits — useful for consoles whose button
    // layout needs deliberate half/full-press semantics (e.g. avoiding
    // DualSense's hair-trigger digital firing on a SNES output).
    //
    // When no profile is active (apps like usb2usb / gc2usb that pass
    // through to a USB HID gamepad descriptor with both digital and
    // analog), DO NOTHING here — let the driver's reported L2/R2 button
    // bits + analog values flow through unchanged. The output mode is
    // responsible for any per-output trigger interpretation. Defaulting
    // to a synthesised threshold (previously 1, then briefly 128) caused
    // false trigger fires on controllers whose analog l/r reading is
    // non-zero at rest (e.g. GameCube — buttons 13/14 stuck on in gc2usb).
    // Custom profiles may still override this post-hoc (see usbd/ble
    // output paths) using their own per-profile threshold fields.
    if (profile && profile->l2_threshold > 0) {
        output->buttons &= ~JP_BUTTON_L2;
        if (l2 >= profile->l2_threshold) output->buttons |= JP_BUTTON_L2;
    }
    if (profile && profile->r2_threshold > 0) {
        output->buttons &= ~JP_BUTTON_R2;
        if (r2 >= profile->r2_threshold) output->buttons |= JP_BUTTON_R2;
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
    // Use output->buttons which includes threshold-based L2/R2 for XInput
    uint32_t buttons_with_triggers = output->buttons;
    uint32_t output_buttons = 0;
    uint32_t mapped_inputs = 0;

    // Apply explicit mappings
    for (uint8_t i = 0; i < profile->button_map_count; i++) {
        const button_map_entry_t* entry = &profile->button_map[i];

        // Check if input button is pressed (includes threshold-based L2/R2)
        bool pressed = ((buttons_with_triggers & entry->input) != 0);

        if (entry->autofire_period_ms > 0) {
            uint8_t bit = __builtin_ctz(entry->input);
            if (bit < AUTOFIRE_BUTTON_COUNT) {
                if (pressed) {
                    if (output->autofire_start_ms[bit] == 0)
                        output->autofire_start_ms[bit] = platform_time_ms();
                    uint32_t elapsed = platform_time_ms() - output->autofire_start_ms[bit];
                    // Time within the current cycle = elapsed % period (ms).
                    // Button ON for the first half, OFF for the second half.
                    // e.g. 30 Hz (period=33ms): 0-16ms ON, 17-32ms OFF, 33ms new cycle ...
                    pressed = (elapsed % entry->autofire_period_ms) < (entry->autofire_period_ms / 2);
                } else {
                    output->autofire_start_ms[bit] = 0;
                }
            }
        }

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
    uint32_t pressed_unmapped = buttons_with_triggers & unmapped_inputs;
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
                // Cap analog at custom value (proportional up to cap), no digital
                if (output->l2_analog > profile->l2_analog_value) {
                    output->l2_analog = profile->l2_analog_value;
                }
                output->buttons &= ~JP_BUTTON_L2;  // Never produce digital
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
                // Cap analog at custom value (proportional up to cap), no digital
                if (output->r2_analog > profile->r2_analog_value) {
                    output->r2_analog = profile->r2_analog_value;
                }
                output->buttons &= ~JP_BUTTON_R2;  // Never produce digital
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

    // Digital-only trigger fallback: if analog is 0 but digital button is pressed,
    // synthesize full-press analog value. Handles controllers that report L2/R2 as
    // buttons only (e.g. 8BitDo Pro2 via generic BT driver).
    if (output->l2_analog == 0 && (output->buttons & JP_BUTTON_L2)) {
        output->l2_analog = 255;
    }
    if (output->r2_analog == 0 && (output->buttons & JP_BUTTON_R2)) {
        output->r2_analog = 255;
    }
}

uint32_t profile_apply_button_map(const profile_t* profile, uint32_t input_buttons)
{
    profile_output_t output;
    profile_apply(profile, input_buttons, 128, 128, 128, 128, 0, 0, 0, &output);
    return output.buttons;
}

uint32_t profile_inputs_for_output(const profile_t* profile, uint32_t output_mask)
{
    uint32_t sources = 0;
    uint32_t mapped_inputs = 0;

    if (profile && profile->button_map) {
        for (uint8_t i = 0; i < profile->button_map_count; i++) {
            const button_map_entry_t* entry = &profile->button_map[i];
            if (entry->output & output_mask) {
                sources |= entry->input;
            }
            mapped_inputs |= entry->input;
        }
    }

    // Unmapped buttons pass through to themselves
    return sources | (output_mask & ~mapped_inputs);
}
