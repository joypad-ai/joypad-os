// runtime_profile.c - Runtime profile mapping service

#include "runtime_profile.h"
#include "platform/platform.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/services/leds/leds.h"
#include "core/services/profiles/profile_indicator.h"
#include "core/services/players/manager.h"
#include <stdio.h>
#include <stddef.h>

// ============================================================================
// STATE
// ============================================================================

static const runtime_profile_config_t*        full_cfg = NULL;
static const runtime_profile_output_config_t* cfg      = NULL;

typedef enum {
    RUNTIME_IDLE = 0,
    RUNTIME_MAPPING,      // entry-by-entry mapping (SELECT alone 3s trigger)
    RUNTIME_MAPPING_ALT,  // tap-count mapping      (START + 2 buttons 3s trigger)
    RUNTIME_AUTOFIRE,     // auto-fire assignment   (START + 1 button  3s trigger)
} runtime_state_t;

// Tap timeout: silence after last tap before committing the period (ms)
#define RUNTIME_TAP_TIMEOUT_MS 800

static runtime_state_t runtime_state        = RUNTIME_IDLE;
static uint8_t         runtime_entry        = 0;
static uint32_t        runtime_mapped_mask  = 0;  // bitmask of already-mapped inputs
static uint32_t        runtime_select_hold  = 0;  // SELECT hold timer (Trigger A + clear)
static uint32_t        runtime_combo_hold   = 0;  // START+mask hold timer  (Trigger B/C)
static uint32_t        runtime_prev_buttons = 0;
static bool            hold_was_elapsed     = false;  // SELECT hold has elapsed

// Tap / auto-fire mode state (shared)
static uint32_t tap_button  = 0;  // input button being tapped
static uint8_t  tap_count   = 0;  // taps so far in current sequence
static uint32_t tap_timeout = 0;  // timestamp of last tap (ms)

// Auto-fire frequency table: index = tap_count-1, value = period ms
static const uint8_t autofire_period_ms_table[] = {
    AUTOFIRE_30HZ,  // 1 tap  → 30 Hz
    AUTOFIRE_20HZ,  // 2 taps → 20 Hz
    AUTOFIRE_15HZ,  // 3 taps → 15 Hz
    AUTOFIRE_12HZ,  // 4 taps → 12 Hz
    AUTOFIRE_10HZ,  // 5 taps → 10 Hz
    AUTOFIRE_7HZ,   // 6 taps → 7.5 Hz
};

// Writable button-map buffer — pointed to by cfg->profile->button_map on init.
static button_map_entry_t runtime_map[RUNTIME_PROFILE_ENTRY_MAX];

// Callbacks
static uint8_t (*get_player_count)(void) = NULL;

// ============================================================================
// HELPERS
// ============================================================================

static void runtime_profile_indicator(uint8_t blinks) {
    uint8_t player_count = get_player_count ? get_player_count() : 0;
    leds_indicate_profile(blinks);
    profile_indicator_trigger(blinks, player_count);
}

static const char* jp_button_names(uint32_t mask) {
    static const char* const names[] = {
        "JP_BUTTON_B1", "JP_BUTTON_B2", "JP_BUTTON_B3", "JP_BUTTON_B4",  // 0-3
        "JP_BUTTON_L1", "JP_BUTTON_R1", "JP_BUTTON_L2", "JP_BUTTON_R2",  // 4-7
        "JP_BUTTON_S1", "JP_BUTTON_S2", "JP_BUTTON_L3", "JP_BUTTON_R3",  // 8-11
        "JP_BUTTON_DU", "JP_BUTTON_DD", "JP_BUTTON_DL", "JP_BUTTON_DR",  // 12-15
        "JP_BUTTON_A1", "JP_BUTTON_A2", "JP_BUTTON_A3", "JP_BUTTON_A4",  // 16-19
        "JP_BUTTON_L4", "JP_BUTTON_R4",                                  // 20-21
    };
    if (mask == 0) return "JP_BUTTON_UNKNOWN";
    int idx = __builtin_ctz(mask);
    if (idx >= (int)(sizeof(names)/sizeof(names[0]))) return "JP_BUTTON_UNKNOWN";
    return names[idx];
}

static const char* output_button_name(uint8_t slot) {
    if (cfg->output_button_names && cfg->output_button_names[slot])
        return cfg->output_button_names[slot];
    return jp_button_names(cfg->output_buttons[slot]);
}

static void map_entry(uint32_t input_mask) {
    runtime_map[runtime_entry] = (button_map_entry_t){0};
    runtime_map[runtime_entry].input  = input_mask;
    runtime_map[runtime_entry].output = cfg->output_buttons[runtime_entry];
    runtime_mapped_mask |= input_mask;
    printf("[runtime_profile] Slot %d/%d: %s -> %s\n",
           runtime_entry + 1, cfg->output_button_count,
           jp_button_names(input_mask),
           output_button_name(runtime_entry));
    runtime_entry++;
}

// Commit the current tap sequence: assign tap_button to output_buttons[tap_count-1].
// Updates an existing entry for that button if present, otherwise appends.
static void tap_commit(void) {
    if (tap_button == 0 || tap_count == 0) return;
    uint8_t  output_slot = tap_count - 1;
    uint32_t output = (output_slot < cfg->output_button_count)
                      ? cfg->output_buttons[output_slot]
                      : 0;
    for (uint8_t i = 0; i < runtime_entry; i++) {
        if (runtime_map[i].input == tap_button) {
            // runtime_profile_indicator(0);
            runtime_map[i].output = output;
            printf("[runtime_profile] Map update: %s -> %s\n",
                   jp_button_names(tap_button),
                   output == 0 ? "DISABLED" : output_button_name(output_slot));
            return;
        }
    }
    if (output != 0 && runtime_entry < RUNTIME_PROFILE_ENTRY_MAX) {
        // runtime_profile_indicator(0);
        runtime_map[runtime_entry]        = (button_map_entry_t){0};
        runtime_map[runtime_entry].input  = tap_button;
        runtime_map[runtime_entry].output = output;
        printf("[runtime_profile] Tap assign: %s -> %s\n",
               jp_button_names(tap_button), output_button_name(output_slot));
        runtime_entry++;
    }
}

// Commit auto-fire: find tap_button in the existing button map and update its
// autofire_period_ms. Buttons not in the map are silently ignored — autofire
// overlays the current mapping without creating new entries.
static void autofire_commit(void) {
    if (tap_button == 0 || tap_count == 0) return;
    uint8_t table_size = sizeof(autofire_period_ms_table) / sizeof(autofire_period_ms_table[0]);
    uint8_t period_ms = (tap_count > table_size) ? 0 : autofire_period_ms_table[tap_count - 1];

    // No runtime mapping yet — seed from the active normal profile so autofire
    // overlays on top of the current button remapping instead of replacing it.
    if (runtime_entry == 0) {
        const profile_t* base = profile_get_active(router_get_primary_output());
        if (base && base->button_map && base->button_map_count > 0) {
            uint8_t n = base->button_map_count;
            if (n > RUNTIME_PROFILE_ENTRY_MAX) n = RUNTIME_PROFILE_ENTRY_MAX;
            for (uint8_t i = 0; i < n; i++) runtime_map[i] = base->button_map[i];
            runtime_entry                   = n;
            cfg->profile->button_map_count = n;
            printf("[runtime_profile] AutoFire: seeded %d entries from active profile\n", n);
        }
    }

    for (uint8_t i = 0; i < runtime_entry; i++) {
        if (runtime_map[i].input == tap_button) {
            // runtime_profile_indicator(0);
            runtime_map[i].autofire_period_ms = period_ms;
            printf("[runtime_profile] AutoFire update: %s @ %d Hz\n",
                   jp_button_names(tap_button), period_ms ? 1000 / period_ms : 0);
            return;
        }
    }
    printf("[runtime_profile] AutoFire: %s not in map, ignored\n",
           jp_button_names(tap_button));
}

static void finish_mapping(void) {
    // Disable all input_mask buttons that were not mapped an entry,
    // so they produce no output instead of passing through.
    uint32_t mapped_mask = 0;
    for (uint8_t i = 0; i < runtime_entry; i++) {
        mapped_mask |= runtime_map[i].input;
    }
    uint8_t total = runtime_entry;
    uint32_t unmapped = cfg->input_mask & ~mapped_mask;
    while (unmapped && total < RUNTIME_PROFILE_ENTRY_MAX) {
        uint32_t btn = unmapped & (~unmapped + 1);
        runtime_map[total]       = (button_map_entry_t){0};
        runtime_map[total].input = btn;
        total++;
        unmapped &= unmapped - 1;
    }
    cfg->profile->button_map_count = total;
    runtime_state = RUNTIME_IDLE;
    runtime_profile_indicator(1);
    printf("[runtime_profile] Mapping complete\n");
}

// ============================================================================
// API
// ============================================================================

void runtime_profile_init(const runtime_profile_config_t* config) {
    full_cfg = config;
    cfg      = NULL;
    if (config) {
        for (int i = 0; i < MAX_OUTPUT_TARGETS; i++) {
            if (config->output_configs[i]) { cfg = config->output_configs[i]; break; }
        }
    }
    runtime_state        = RUNTIME_IDLE;
    runtime_entry        = 0;
    runtime_mapped_mask  = 0;
    runtime_select_hold  = 0;
    runtime_combo_hold   = 0;

    runtime_prev_buttons = 0;
    hold_was_elapsed     = false;

    tap_button           = 0;
    tap_count            = 0;
    tap_timeout          = 0;
    if (cfg && cfg->profile) {
        cfg->profile->button_map       = runtime_map;
        cfg->profile->button_map_count = 0;
    }
}

void runtime_profile_set_player_count_callback(uint8_t (*callback)(void))
{
    get_player_count = callback;
}

bool runtime_profile_is_active(void) {
    return runtime_state == RUNTIME_MAPPING     ||
           runtime_state == RUNTIME_MAPPING_ALT ||
           runtime_state == RUNTIME_AUTOFIRE;
}

void runtime_profile_clear(void) {
    if (cfg && cfg->profile) {
        cfg->profile->button_map_count = 0;
    }
    runtime_state        = RUNTIME_IDLE;
    runtime_entry        = 0;
    runtime_mapped_mask  = 0;
    runtime_select_hold  = 0;
    runtime_combo_hold   = 0;

    runtime_prev_buttons = 0;
    hold_was_elapsed     = false;

    tap_button           = 0;
    tap_count            = 0;
    tap_timeout          = 0;
    printf("[runtime_profile] Cleared\n");
}

void runtime_autofire_clear(void) {
    runtime_state        = RUNTIME_IDLE;

    tap_button           = 0;
    tap_count            = 0;
    tap_timeout          = 0;

    for (uint8_t i = 0; i < runtime_entry; i++) {
        runtime_map[i].autofire_period_ms = 0;
    }
}

const profile_t* runtime_profile_get_active(output_target_t output) {
    if (!full_cfg || output >= MAX_OUTPUT_TARGETS) return NULL;
    const runtime_profile_output_config_t* out = full_cfg->output_configs[output];
    if (!out || !out->profile) return NULL;
    return out->profile->button_map_count > 0 ? out->profile : NULL;
}

void runtime_profile_check_combo(uint32_t input_buttons, uint8_t l2, uint8_t r2) {
    if (!cfg) return;
    if (!cfg->profile) return;

    // Normalize: digital-only triggers (fight sticks, arcade pads) report
    // digital L2/R2 with no analog data. Synthesize full analog press so
    // threshold logic works uniformly across all controller types.
    if ((input_buttons & JP_BUTTON_L2) && l2 == 0) {
        l2 = 255;
    }
    if ((input_buttons & JP_BUTTON_R2) && r2 == 0) {
        r2 = 255;
    }

    // Set L2/R2 digital buttons based on analog threshold (if threshold > 0).
    // When threshold is set, it OVERRIDES input L2/R2 (e.g. DualSense's early digital).
    // Threshold of 0 means passthrough (use input driver's L2/R2 as-is).
    if (cfg->profile->l2_threshold > 0) {
        input_buttons &= ~JP_BUTTON_L2;
        if (l2 >= cfg->profile->l2_threshold) {
            input_buttons |= JP_BUTTON_L2;
        }
    }
    if (cfg->profile->r2_threshold > 0) {
        input_buttons &= ~JP_BUTTON_R2;
        if (r2 >= cfg->profile->r2_threshold) {
            input_buttons |= JP_BUTTON_R2;
        }
    }

    uint32_t new_prev = input_buttons;

    if (leds_is_indicating() || profile_indicator_is_active()) {
        runtime_prev_buttons = new_prev;
        return;
    }

    switch (runtime_state) {

        case RUNTIME_IDLE: {
            bool select_held   = (input_buttons & JP_BUTTON_S1) != 0;
            bool start_held    = (input_buttons & JP_BUTTON_S2) != 0;
            bool dpad_held     = (input_buttons & (JP_BUTTON_DU | JP_BUTTON_DD |
                                             JP_BUTTON_DL | JP_BUTTON_DR)) != 0;
            uint32_t prev_eligible = runtime_prev_buttons & cfg->input_mask;
            uint32_t curr_eligible = input_buttons & cfg->input_mask;

            // --- Trigger A: SELECT alone (no mask buttons, no dpad) for hold_ms,
            //     then first mask button press → entry mode ---
            if (select_held && curr_eligible == 0 && !dpad_held) {
                if (runtime_select_hold == 0) runtime_select_hold = platform_time_ms();
            } else if (!hold_was_elapsed) {
                runtime_select_hold = 0;
            }

            bool hold_elapsed = runtime_select_hold != 0 &&
                                (platform_time_ms() - runtime_select_hold) >= cfg->hold_ms;
            if (hold_elapsed && !hold_was_elapsed) { hold_was_elapsed = true; new_prev = 0; }
            if (!hold_elapsed) hold_was_elapsed = false;

            if (hold_elapsed && select_held) {
                bool s2_rising = (input_buttons  & JP_BUTTON_S2) &&
                                !(runtime_prev_buttons & JP_BUTTON_S2);
                if (s2_rising && curr_eligible == 0) {
                    runtime_profile_indicator(1);
                    runtime_profile_clear();
                    break;
                }
            }

            if (hold_elapsed && select_held && prev_eligible == 0 && curr_eligible != 0) {
                cfg->profile->button_map_count = 0;
                runtime_entry       = 0;
                runtime_mapped_mask = 0;
                runtime_select_hold = 0;
                runtime_combo_hold = 0;
                hold_was_elapsed   = false;
                uint32_t input_btn = curr_eligible & (~curr_eligible + 1);
                map_entry(input_btn);
                if (runtime_entry >= cfg->output_button_count) {
                    finish_mapping();
                } else {
                    runtime_profile_indicator(1);
                    runtime_state = RUNTIME_MAPPING;
                    printf("[runtime_profile] Entry mode: press button for entry %d/%d\n",
                           runtime_entry + 1, cfg->output_button_count);
                }
                new_prev = input_buttons;
                break;
            }

            // --- Trigger B: START + 2 mask buttons for hold_ms → tap mode
            //     Trigger C: START + 1 mask button  for hold_ms → auto-fire mode ---
            if (start_held && !select_held && curr_eligible != 0) {
                if (runtime_combo_hold == 0) runtime_combo_hold = platform_time_ms();
            } else {
                runtime_combo_hold = 0;
            }

            if (runtime_combo_hold != 0 &&
                (platform_time_ms() - runtime_combo_hold) >= cfg->hold_ms) {
                bool one_button = (curr_eligible & (curr_eligible - 1)) == 0;
                runtime_select_hold = 0;
                runtime_combo_hold = 0;
                hold_was_elapsed   = false;
                new_prev = input_buttons;
                runtime_profile_indicator(1);
                if (one_button) {
                    runtime_state = RUNTIME_AUTOFIRE;
                    printf("[runtime_profile] AutoFire mode: tap button Nx for Hz, START to save, SELECT to clear\n");
                    printf("[runtime_profile]   1x=30Hz 2x=20Hz 3x=15Hz 4x=12Hz 5x=10Hz 6x=7.5Hz\n");
                } else {
                    cfg->profile->button_map_count = 0;
                    runtime_entry = 0;
                    runtime_state = RUNTIME_MAPPING_ALT;
                    printf("[runtime_profile] Tap mode: tap buttons, START to save, SELECT to clear\n");
                }
            }
            break;
        }

        case RUNTIME_MAPPING: {
            // Cancel: START
            bool s2_rising = (input_buttons  & JP_BUTTON_S2) &&
                            !(runtime_prev_buttons & JP_BUTTON_S2);
            if (s2_rising) {
                runtime_profile_indicator(1);
                runtime_profile_clear();
                break;
            }

            uint32_t prev_eligible = runtime_prev_buttons & cfg->input_mask;
            uint32_t curr_eligible = input_buttons & cfg->input_mask;
            if (prev_eligible == 0 && curr_eligible != 0) {
                uint32_t input_btn = curr_eligible & (~curr_eligible + 1);
                if (input_btn & runtime_mapped_mask) {
                    printf("[runtime_profile] Input %s already mapped, skipping\n",
                           jp_button_names(input_btn));
                    break;
                }
                map_entry(input_btn);
                if (runtime_entry >= cfg->output_button_count) {
                    finish_mapping();
                } else {
                    runtime_profile_indicator(0);
                    printf("[runtime_profile] Press button for entry %d/%d\n",
                           runtime_entry + 1, cfg->output_button_count);
                }
            }
            break;
        }

        case RUNTIME_MAPPING_ALT: {
            uint32_t curr_eligible = input_buttons & cfg->input_mask;
            uint32_t prev_eligible = runtime_prev_buttons & cfg->input_mask;

            // START alone (rising edge, no mask buttons) → commit + save
            bool s2_rising = (input_buttons & JP_BUTTON_S2) &&
                             !(runtime_prev_buttons & JP_BUTTON_S2);
            if (s2_rising && !(input_buttons & JP_BUTTON_S1) && curr_eligible == 0) {
                tap_commit();
                tap_button  = 0;
                tap_count   = 0;
                tap_timeout = 0;
                finish_mapping();
                break;
            }

            // SELECT alone (rising edge, no mask buttons) → clear + exit
            bool s1_rising = (input_buttons & JP_BUTTON_S1) &&
                             !(runtime_prev_buttons & JP_BUTTON_S1);
            if (s1_rising && !(input_buttons & JP_BUTTON_S2) && curr_eligible == 0) {
                runtime_profile_indicator(1);
                runtime_profile_clear();
                break;
            }

            // Commit pending sequence on timeout
            if (tap_button != 0 && tap_timeout != 0 &&
                (platform_time_ms() - tap_timeout) >= RUNTIME_TAP_TIMEOUT_MS) {
                runtime_profile_indicator(0);
                tap_commit();
                tap_button  = 0;
                tap_count   = 0;
                tap_timeout = 0;
            }

            // Detect rising edge on eligible buttons
            if (prev_eligible == 0 && curr_eligible != 0) {
                uint32_t input_btn = curr_eligible & (~curr_eligible + 1);
                if (tap_button == 0) {
                    tap_button  = input_btn;
                    tap_count   = 1;
                    tap_timeout = platform_time_ms();
                    printf("[runtime_profile] Tap 1x: %s\n", jp_button_names(input_btn));
                } else if (input_btn == tap_button) {
                    tap_count++;
                    tap_timeout = platform_time_ms();
                    printf("[runtime_profile] Tap %dx: %s\n",
                           tap_count, jp_button_names(input_btn));
                } else {
                    // Different button: commit previous, start new sequence
                    runtime_profile_indicator(0);
                    tap_commit();
                    tap_button  = input_btn;
                    tap_count   = 1;
                    tap_timeout = platform_time_ms();
                    printf("[runtime_profile] Tap 1x: %s\n", jp_button_names(input_btn));
                }
            }
            break;
        }

        case RUNTIME_AUTOFIRE: {
            uint32_t curr_eligible = input_buttons & cfg->input_mask;
            uint32_t prev_eligible = runtime_prev_buttons & cfg->input_mask;

            // START alone (rising edge, no mask buttons) → commit + back to IDLE
            bool s2_rising = (input_buttons & JP_BUTTON_S2) &&
                             !(runtime_prev_buttons & JP_BUTTON_S2);
            if (s2_rising && !(input_buttons & JP_BUTTON_S1) && curr_eligible == 0) {
                runtime_profile_indicator(1);
                autofire_commit();
                tap_button    = 0;
                tap_count     = 0;
                tap_timeout   = 0;
                runtime_state = RUNTIME_IDLE;
                printf("[runtime_profile] AutoFire mapping complete\n");
                break;
            }

            // SELECT alone (rising edge, no mask buttons) → discard + back to IDLE
            bool s1_rising = (input_buttons & JP_BUTTON_S1) &&
                             !(runtime_prev_buttons & JP_BUTTON_S1);
            if (s1_rising && !(input_buttons & JP_BUTTON_S2) && curr_eligible == 0) {
                runtime_profile_indicator(1);
                runtime_autofire_clear();
                break;
            }

            // Commit pending sequence on timeout
            if (tap_button != 0 && tap_timeout != 0 &&
                (platform_time_ms() - tap_timeout) >= RUNTIME_TAP_TIMEOUT_MS) {
                runtime_profile_indicator(0);
                autofire_commit();
                tap_button  = 0;
                tap_count   = 0;
                tap_timeout = 0;
            }

            // Detect rising edge on eligible buttons
            if (prev_eligible == 0 && curr_eligible != 0) {
                uint32_t input_btn = curr_eligible & (~curr_eligible + 1);
                if (tap_button == 0) {
                    tap_button  = input_btn;
                    tap_count   = 1;
                    tap_timeout = platform_time_ms();
                    printf("[runtime_profile] AutoFire tap 1x: %s\n",
                           jp_button_names(input_btn));
                } else if (input_btn == tap_button) {
                    tap_count++;
                    tap_timeout = platform_time_ms();
                    printf("[runtime_profile] AutoFire tap %dx: %s\n",
                           tap_count, jp_button_names(input_btn));
                } else {
                    // Different button: commit previous, start new
                    runtime_profile_indicator(0);
                    autofire_commit();
                    tap_button  = input_btn;
                    tap_count   = 1;
                    tap_timeout = platform_time_ms();
                    printf("[runtime_profile] AutoFire tap 1x: %s\n",
                           jp_button_names(input_btn));
                }
            }
            break;
        }
    }

    runtime_prev_buttons = new_prev;
}
