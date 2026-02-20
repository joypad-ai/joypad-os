// profile.h - Universal Profile System
//
// Provides a standardized profile structure for button remapping across all outputs.
// Uses JP_BUTTON_* constants for both input and output, with console-specific
// aliases (GC_BUTTON_A, TDO_BUTTON_B, PCE_BUTTON_I, etc.) for readability.
//
// Architecture:
//   Input Device → JP_BUTTON_* → Profile Mapping → JP_BUTTON_* → Output Device → Console Native
//
// The profile system supports:
//   - Simple 1:1 button remapping (B1 → B2)
//   - Button to multiple buttons (R2 → L2 + R2)
//   - Button to analog axis output (L1 → right stick up)
//   - Button with analog value modifier (L1 → L2 digital + L2 analog at 255)
//   - Trigger behavior configuration (passthrough, digital only, full press, etc.)
//
// Console devices interpret USBR outputs using their own knowledge of what
// each button/analog means on their platform.

#ifndef CORE_PROFILE_H
#define CORE_PROFILE_H

#include <stdint.h>
#include <stdbool.h>
#include "core/buttons.h"
#include "core/router/router.h"

// ============================================================================
// ANALOG OUTPUT TARGETS
// ============================================================================
// For mapping buttons to analog outputs (e.g., C-stick directions)

typedef enum {
    ANALOG_TARGET_NONE = 0,

    // Left stick
    ANALOG_TARGET_LX_MIN,       // Left stick X to 0
    ANALOG_TARGET_LX_MAX,       // Left stick X to 255
    ANALOG_TARGET_LY_MIN,       // Left stick Y to 0
    ANALOG_TARGET_LY_MAX,       // Left stick Y to 255

    // Right stick (C-stick on GC)
    ANALOG_TARGET_RX_MIN,       // Right stick X to 0 (GC: C-Left)
    ANALOG_TARGET_RX_MAX,       // Right stick X to 255 (GC: C-Right)
    ANALOG_TARGET_RY_MIN,       // Right stick Y to 0 (GC: C-Down)
    ANALOG_TARGET_RY_MAX,       // Right stick Y to 255 (GC: C-Up)

    // Triggers
    ANALOG_TARGET_L2_FULL,      // L2 analog to 255
    ANALOG_TARGET_R2_FULL,      // R2 analog to 255
    ANALOG_TARGET_L2_CUSTOM,    // L2 analog to custom value (uses analog_value field)
    ANALOG_TARGET_R2_CUSTOM,    // R2 analog to custom value (uses analog_value field)

} analog_target_t;

// ============================================================================
// BUTTON MAPPING ENTRY
// ============================================================================
// Maps one input button to output(s) - supports advanced mappings

typedef struct {
    uint32_t input;             // JP_BUTTON_* input (e.g., JP_BUTTON_B1)
    uint32_t output;            // JP_BUTTON_* output(s) - can OR multiple buttons
    analog_target_t analog;     // Optional analog output (0 = none)
    uint8_t analog_value;       // Custom analog value for ANALOG_TARGET_*_CUSTOM
} button_map_entry_t;

// ============================================================================
// BUTTON COMBO ENTRY
// ============================================================================
// Maps multiple input buttons pressed together to output(s)

typedef struct {
    uint32_t inputs;            // JP_BUTTON_* inputs (OR'd together - all must be pressed)
    uint32_t output;            // JP_BUTTON_* output(s) when combo active
    bool consume_inputs;        // If true, remove input buttons from output when combo fires
    bool exclusive;             // If true, combo only fires when EXACTLY these inputs are pressed (no other buttons)
} button_combo_entry_t;

// ============================================================================
// STICK MODIFIER
// ============================================================================
// Button-triggered sensitivity modifier for analog sticks

typedef struct {
    uint32_t trigger;           // Button that activates modifier (e.g., JP_BUTTON_L3)
    float sensitivity;          // Sensitivity when modifier active (0.0-1.0)
    bool consume_trigger;       // If true, remove trigger button from output
} stick_modifier_t;

// ============================================================================
// TRIGGER BEHAVIOR
// ============================================================================
// How analog triggers (L2/R2) should behave

typedef enum {
    TRIGGER_PASSTHROUGH = 0,    // Analog value passed through, digital at threshold
    TRIGGER_DIGITAL_ONLY,       // Digital only, no analog output
    TRIGGER_FULL_PRESS,         // Digital + analog forced to 255
    TRIGGER_LIGHT_PRESS,        // Analog capped at custom value (l2/r2_analog_value), no digital
    TRIGGER_INSTANT,            // Digital triggers immediately (threshold = 1)
    TRIGGER_DISABLED,           // No output
} trigger_behavior_t;

// ============================================================================
// SOCD (SIMULTANEOUS OPPOSITE CARDINAL DIRECTIONS) HANDLING
// ============================================================================
// How to resolve when opposite directions are pressed simultaneously

typedef enum {
    SOCD_PASSTHROUGH = 0,       // No SOCD cleaning (both directions output)
    SOCD_NEUTRAL,               // Cancel opposite directions (U+D=neutral, L+R=neutral)
    SOCD_UP_PRIORITY,           // U+D=U, L+R=neutral (Hitbox style)
    SOCD_LAST_WIN,              // Last input wins (requires state tracking)
} socd_mode_t;

// ============================================================================
// PROFILE STRUCTURE
// ============================================================================

#define MAX_BUTTON_MAPPINGS 24  // Max button mappings per profile
#define MAX_BUTTON_COMBOS 8     // Max button combos per profile

typedef struct {
    // Identity
    const char* name;           // Short name (e.g., "default", "ssbm")
    const char* description;    // Human-readable description

    // Button mappings (sparse - only non-default mappings needed)
    // If a button isn't in this list, it passes through unchanged (input == output)
    const button_map_entry_t* button_map;
    uint8_t button_map_count;

    // Button combos (multiple buttons → output)
    // Checked before individual mappings; if consume_inputs is true, input buttons are removed
    const button_combo_entry_t* combo_map;
    uint8_t combo_map_count;

    // Trigger configuration
    trigger_behavior_t l2_behavior;
    trigger_behavior_t r2_behavior;
    uint8_t l2_threshold;       // Analog threshold for digital activation (0-255)
    uint8_t r2_threshold;       // Analog threshold for digital activation (0-255)
    uint8_t l2_analog_value;    // Custom analog value for TRIGGER_LIGHT_PRESS
    uint8_t r2_analog_value;    // Custom analog value for TRIGGER_LIGHT_PRESS

    // Analog stick settings
    float left_stick_sensitivity;   // 0.0-1.0 (1.0 = 100%)
    float right_stick_sensitivity;  // 0.0-1.0 (0.0 = disabled)

    // Stick modifiers (button-triggered sensitivity changes)
    const stick_modifier_t* left_stick_modifiers;
    uint8_t left_stick_modifier_count;
    const stick_modifier_t* right_stick_modifiers;
    uint8_t right_stick_modifier_count;

    // DualSense adaptive trigger feedback
    bool adaptive_triggers;

    // SOCD handling for D-pad (fighting games, Hitbox controllers)
    socd_mode_t socd_mode;

} profile_t;

// ============================================================================
// PROFILE OUTPUT STATE
// ============================================================================
// Result of applying profile mapping to input - consumed by output devices

typedef struct {
    uint32_t buttons;           // Remapped button state (active-high: 1 = pressed)

    // Analog outputs (can be modified by button mappings)
    uint8_t left_x;
    uint8_t left_y;
    uint8_t right_x;
    uint8_t right_y;
    uint8_t l2_analog;
    uint8_t r2_analog;
    uint8_t rz_analog;

    // Flags for analog overrides (buttons forced analog values)
    bool left_x_override;
    bool left_y_override;
    bool right_x_override;
    bool right_y_override;
    bool l2_analog_override;
    bool r2_analog_override;
    bool rz_analog_override;

    // Motion data (passthrough from input)
    int16_t accel[3];           // Accelerometer X, Y, Z
    int16_t gyro[3];            // Gyroscope X, Y, Z
    bool has_motion;            // Motion data is valid

    // Pressure-sensitive button data (DS3 passthrough)
    // Order: up, right, down, left, L2, R2, L1, R1, triangle, circle, cross, square
    uint8_t pressure[12];       // 0x00 = released, 0xFF = fully pressed
    bool has_pressure;          // Pressure data is valid

} profile_output_t;

// ============================================================================
// PROFILE SET (per output target)
// ============================================================================

typedef struct {
    const profile_t* profiles;      // Array of profiles
    uint8_t profile_count;          // Number of profiles
    uint8_t default_index;          // Default profile index
} profile_set_t;

// ============================================================================
// PER-PLAYER PROFILE STATE
// ============================================================================
// Each player can have their own active profile index

#ifndef MAX_PLAYERS
#define MAX_PLAYERS 5
#endif

typedef struct {
    uint8_t profile_index;          // Active profile index for this player
    bool dirty;                     // Profile changed, needs feedback
} player_profile_state_t;

// ============================================================================
// PROFILE SYSTEM CONFIGURATION
// ============================================================================

#define MAX_OUTPUT_TARGETS OUTPUT_TARGET_COUNT

typedef struct {
    // Profile sets indexed by output_target_t
    // NULL means no profiles for that output (use passthrough)
    const profile_set_t* output_profiles[MAX_OUTPUT_TARGETS];

    // Shared profile set (used when output-specific not defined)
    const profile_set_t* shared_profiles;

} profile_config_t;

// ============================================================================
// PROFILE SYSTEM API
// ============================================================================

// Initialize profile system with configuration
void profile_init(const profile_config_t* config);

// Get active profile for an output target (legacy - uses player 0's profile)
// Falls back to shared profiles if output-specific not defined
// Returns NULL if no profiles configured
const profile_t* profile_get_active(output_target_t output);

// Get profile info (legacy - uses player 0)
uint8_t profile_get_active_index(output_target_t output);
uint8_t profile_get_count(output_target_t output);
const char* profile_get_name(output_target_t output, uint8_t index);

// Switch profiles (legacy - affects player 0, with feedback)
void profile_set_active(output_target_t output, uint8_t index);
void profile_cycle_next(output_target_t output);
void profile_cycle_prev(output_target_t output);

// ============================================================================
// PER-PLAYER PROFILE API
// ============================================================================

// Get active profile for a specific player
// Returns NULL if player_index invalid or no profiles configured
const profile_t* profile_get_active_for_player(output_target_t output, uint8_t player_index);

// Get profile index for a specific player
uint8_t profile_get_player_index(output_target_t output, uint8_t player_index);

// Set profile for a specific player (with per-player feedback)
void profile_set_player_active(output_target_t output, uint8_t player_index, uint8_t profile_index);
void profile_cycle_player_next(output_target_t output, uint8_t player_index);
void profile_cycle_player_prev(output_target_t output, uint8_t player_index);

// Check for per-player profile switch combo
// player_index: which player's buttons to check
// buttons: that player's button state
void profile_check_player_switch_combo(uint8_t player_index, uint32_t buttons);

// Check if a specific player's switch combo is active
bool profile_player_switch_combo_active(uint8_t player_index);

// ============================================================================
// CALLBACKS
// ============================================================================

// Set callback for when profile switches (device can update its own state)
// Legacy: called for player 0 changes
typedef void (*profile_switch_callback_t)(output_target_t output, uint8_t new_index);
void profile_set_switch_callback(profile_switch_callback_t callback);

// Set callback for per-player profile switches
typedef void (*profile_player_switch_callback_t)(output_target_t output, uint8_t player_index, uint8_t new_index);
void profile_set_player_switch_callback(profile_player_switch_callback_t callback);

// Set player count callback (for feedback)
void profile_set_player_count_callback(uint8_t (*callback)(void));

// Set callback for output mode switching (D-pad Left/Right)
// direction: -1 = left (previous), +1 = right (next)
// Callback should return true if mode was changed (to trigger feedback)
typedef bool (*output_mode_callback_t)(int8_t direction);
void profile_set_output_mode_callback(output_mode_callback_t callback);

// ============================================================================
// LEGACY COMBO DETECTION (uses player 0)
// ============================================================================

// Check for profile switch combo (call from output device's update loop)
// Uses primary output target for switching
void profile_check_switch_combo(uint32_t buttons);

// Check if profile switch combo is currently active
// When true, caller should suppress Select + D-pad from output
bool profile_switch_combo_active(void);

// Load/save profile index from flash
uint8_t profile_load_from_flash(output_target_t output, uint8_t default_index);
void profile_save_to_flash(output_target_t output);

// ============================================================================
// BUTTON MAPPING APPLICATION
// ============================================================================

// Apply profile to input event and get output state
// This is the main function output devices call
void profile_apply(const profile_t* profile,
                   uint32_t input_buttons,
                   uint8_t lx, uint8_t ly,
                   uint8_t rx, uint8_t ry,
                   uint8_t l2, uint8_t r2,
                   uint8_t rz,
                   profile_output_t* output);

// Simple button-only mapping (for basic use cases)
uint32_t profile_apply_button_map(const profile_t* profile, uint32_t input_buttons);

// ============================================================================
// HELPER MACROS FOR PROFILE DEFINITIONS
// ============================================================================

// Simple button remap: input → output
#define MAP_BUTTON(in, out) \
    { .input = (in), .output = (out), .analog = ANALOG_TARGET_NONE, .analog_value = 0 }

// Button to multiple buttons: input → out1 | out2
#define MAP_BUTTON_MULTI(in, out1, out2) \
    { .input = (in), .output = ((out1) | (out2)), .analog = ANALOG_TARGET_NONE, .analog_value = 0 }

// Button to button + analog: input → button + analog at value
#define MAP_BUTTON_ANALOG(in, out, analog_tgt, value) \
    { .input = (in), .output = (out), .analog = (analog_tgt), .analog_value = (value) }

// Button to analog only: input → analog axis
#define MAP_ANALOG_ONLY(in, analog_tgt) \
    { .input = (in), .output = 0, .analog = (analog_tgt), .analog_value = 0 }

// Button disabled: input → nothing
#define MAP_DISABLED(in) \
    { .input = (in), .output = 0, .analog = ANALOG_TARGET_NONE, .analog_value = 0 }

// Button combo: multiple inputs → output (consumes inputs by default)
#define MAP_COMBO(ins, out) \
    { .inputs = (ins), .output = (out), .consume_inputs = true, .exclusive = false }

// Button combo: multiple inputs → output (keeps inputs in output)
#define MAP_COMBO_KEEP(ins, out) \
    { .inputs = (ins), .output = (out), .consume_inputs = false, .exclusive = false }

// Button combo: ONLY these inputs → output (no other buttons pressed)
// If additional buttons are held, combo doesn't fire and inputs pass through
#define MAP_COMBO_EXCLUSIVE(ins, out) \
    { .inputs = (ins), .output = (out), .consume_inputs = true, .exclusive = true }

// Stick modifier: button → reduced sensitivity (consumes trigger by default)
#define STICK_MODIFIER(btn, sens) \
    { .trigger = (btn), .sensitivity = (sens), .consume_trigger = true }

// Stick modifier: button → reduced sensitivity (keeps trigger in output)
#define STICK_MODIFIER_KEEP(btn, sens) \
    { .trigger = (btn), .sensitivity = (sens), .consume_trigger = false }

// Standard trigger settings
#define PROFILE_TRIGGERS_DEFAULT \
    .l2_behavior = TRIGGER_PASSTHROUGH, \
    .r2_behavior = TRIGGER_PASSTHROUGH, \
    .l2_threshold = 128, \
    .r2_threshold = 128, \
    .l2_analog_value = 0, \
    .r2_analog_value = 0

// Standard analog settings
#define PROFILE_ANALOG_DEFAULT \
    .left_stick_sensitivity = 1.0f, \
    .right_stick_sensitivity = 1.0f, \
    .left_stick_modifiers = NULL, \
    .left_stick_modifier_count = 0, \
    .right_stick_modifiers = NULL, \
    .right_stick_modifier_count = 0

// Full default profile (passthrough)
#define PROFILE_DEFAULT { \
    .name = "default", \
    .description = "Standard 1:1 mapping", \
    .button_map = NULL, \
    .button_map_count = 0, \
    .combo_map = NULL, \
    .combo_map_count = 0, \
    PROFILE_TRIGGERS_DEFAULT, \
    PROFILE_ANALOG_DEFAULT, \
    .adaptive_triggers = false, \
    .socd_mode = SOCD_PASSTHROUGH, \
}

// ============================================================================
// SOCD API (for use by custom profile path in usbd.c)
// ============================================================================

// Apply SOCD cleaning to D-pad buttons
// mode: SOCD cleaning mode
// player_index: for last-win state tracking (use 0 for legacy/single player)
// Returns the cleaned button state
uint32_t apply_socd(uint32_t buttons, socd_mode_t mode, uint8_t player_index);

#endif // CORE_PROFILE_H
