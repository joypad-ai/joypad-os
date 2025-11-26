// hotkeys.c - Button combination detection service

#include "hotkeys.h"
#include "pico/time.h"
#include "core/services/players/manager.h"

// Registered hotkeys
static HotkeyDef registered_hotkeys[MAX_HOTKEYS];
static bool hotkey_active[MAX_HOTKEYS];
static int hotkey_count = 0;

// Per-player hold state tracking
typedef struct {
    uint32_t held_buttons;       // Currently held buttons
    absolute_time_t hold_start;  // When the current combo started being held
    bool holding[MAX_HOTKEYS];   // Track if currently holding this hotkey's combo
    bool triggered[MAX_HOTKEYS]; // Track if hotkey already triggered (prevent repeat for ON_HOLD)
} PlayerHoldState;

static PlayerHoldState player_state[MAX_PLAYERS];

// Global input state (combined from all players)
static uint32_t global_buttons = 0xFFFFFFFF;  // Start with all released (active-low)
static absolute_time_t global_hold_start;
static bool global_holding[MAX_HOTKEYS];
static bool global_triggered[MAX_HOTKEYS];

int hotkeys_register(const HotkeyDef* hotkey) {
    if (hotkey_count >= MAX_HOTKEYS) {
        return -1;
    }

    int id = hotkey_count;
    registered_hotkeys[id] = *hotkey;
    hotkey_active[id] = true;
    hotkey_count++;

    return id;
}

void hotkeys_unregister(int hotkey_id) {
    if (hotkey_id >= 0 && hotkey_id < MAX_HOTKEYS) {
        hotkey_active[hotkey_id] = false;
    }
}

void hotkeys_clear(void) {
    hotkey_count = 0;
    for (int i = 0; i < MAX_HOTKEYS; i++) {
        hotkey_active[i] = false;
    }
}

void hotkeys_reset_player(uint8_t player) {
    if (player >= MAX_PLAYERS) return;

    player_state[player].held_buttons = 0xFFFFFFFF;  // All released (active-low)
    player_state[player].hold_start = nil_time;
    for (int i = 0; i < MAX_HOTKEYS; i++) {
        player_state[player].holding[i] = false;
        player_state[player].triggered[i] = false;
    }
}

// Check if all required buttons are pressed (active low: 0 = pressed)
static inline bool buttons_match(uint32_t current, uint32_t required) {
    // In USBRetro, buttons are active-low (0 = pressed, 1 = released)
    // So we invert and check if all required bits are set
    uint32_t pressed = ~current;
    return (pressed & required) == required;
}

void hotkeys_check(uint32_t buttons, uint8_t player) {
    if (player >= MAX_PLAYERS) return;

    PlayerHoldState* state = &player_state[player];
    absolute_time_t now = get_absolute_time();

    // Update global combined state (AND for active-low: 0 means ANY player has it pressed)
    global_buttons &= buttons;

    for (int i = 0; i < hotkey_count; i++) {
        if (!hotkey_active[i]) continue;

        HotkeyDef* hotkey = &registered_hotkeys[i];

        if (hotkey->global) {
            // Global hotkey - handled in hotkeys_check_global()
            continue;
        }

        // Per-player hotkey check
        bool match = buttons_match(buttons, hotkey->buttons);
        bool was_holding = state->holding[i];

        if (match) {
            // Currently holding the combo
            if (!was_holding) {
                // Just started holding
                state->hold_start = now;
                state->triggered[i] = false;
            }
            state->holding[i] = true;

            // Check for ON_HOLD trigger
            if (hotkey->trigger == HOTKEY_TRIGGER_ON_HOLD && !state->triggered[i]) {
                int64_t held_ms = absolute_time_diff_us(state->hold_start, now) / 1000;

                if (held_ms >= hotkey->duration_ms) {
                    if (hotkey->callback) {
                        hotkey->callback(player, (uint32_t)held_ms);
                    }
                    state->triggered[i] = true;
                }
            }
        } else {
            // Combo released
            if (was_holding) {
                // Just released - check for release-based triggers
                int64_t held_ms = absolute_time_diff_us(state->hold_start, now) / 1000;

                if (hotkey->trigger == HOTKEY_TRIGGER_ON_RELEASE) {
                    // Trigger if held long enough
                    if (held_ms >= hotkey->duration_ms) {
                        if (hotkey->callback) {
                            hotkey->callback(player, (uint32_t)held_ms);
                        }
                    }
                } else if (hotkey->trigger == HOTKEY_TRIGGER_ON_TAP) {
                    // Trigger if released quickly (tap)
                    if (held_ms < hotkey->duration_ms) {
                        if (hotkey->callback) {
                            hotkey->callback(player, (uint32_t)held_ms);
                        }
                    }
                }
            }
            state->holding[i] = false;
            state->triggered[i] = false;
        }
    }

    // Update held buttons state
    state->held_buttons = buttons;
}

void hotkeys_check_global(void) {
    absolute_time_t now = get_absolute_time();

    for (int i = 0; i < hotkey_count; i++) {
        if (!hotkey_active[i]) continue;

        HotkeyDef* hotkey = &registered_hotkeys[i];

        if (!hotkey->global) continue;

        bool match = buttons_match(global_buttons, hotkey->buttons);
        bool was_holding = global_holding[i];

        if (match) {
            if (!was_holding) {
                global_hold_start = now;
                global_triggered[i] = false;
            }
            global_holding[i] = true;

            if (hotkey->trigger == HOTKEY_TRIGGER_ON_HOLD && !global_triggered[i]) {
                int64_t held_ms = absolute_time_diff_us(global_hold_start, now) / 1000;

                if (held_ms >= hotkey->duration_ms) {
                    if (hotkey->callback) {
                        hotkey->callback(0xFF, (uint32_t)held_ms);
                    }
                    global_triggered[i] = true;
                }
            }
        } else {
            if (was_holding) {
                int64_t held_ms = absolute_time_diff_us(global_hold_start, now) / 1000;

                if (hotkey->trigger == HOTKEY_TRIGGER_ON_RELEASE) {
                    if (held_ms >= hotkey->duration_ms) {
                        if (hotkey->callback) {
                            hotkey->callback(0xFF, (uint32_t)held_ms);
                        }
                    }
                } else if (hotkey->trigger == HOTKEY_TRIGGER_ON_TAP) {
                    if (held_ms < hotkey->duration_ms) {
                        if (hotkey->callback) {
                            hotkey->callback(0xFF, (uint32_t)held_ms);
                        }
                    }
                }
            }
            global_holding[i] = false;
            global_triggered[i] = false;
        }
    }

    // Reset global buttons for next frame (all released = 0xFFFFFFFF for active-low)
    global_buttons = 0xFFFFFFFF;
}
