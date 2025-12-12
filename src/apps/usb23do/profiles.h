// profiles.h - USB23DO Profile Definitions
//
// Button mapping profiles for USB to 3DO adapter.
// Uses console-specific button aliases for readability.
//
// 3DO button layout:
//   A (B3) - Top button (vertically arranged)
//   B (B1) - Middle button
//   C (B2) - Bottom button
//   L (L1) - Left shoulder
//   R (R1) - Right shoulder
//   X (S1) - Stop/Select
//   P (S2) - Play/Start
//   D-pad

#ifndef USB23DO_PROFILES_H
#define USB23DO_PROFILES_H

#include "core/services/profiles/profile.h"
#include "native/device/3do/3do_buttons.h"

// ============================================================================
// PROFILE: Default - Standard 3DO Layout
// ============================================================================
// Maps modern controllers to 3DO naturally (SNES23DO style)

static const button_map_entry_t tdo_default_map[] = {
    // Face buttons - SNES-style to 3DO
    MAP_BUTTON(JP_BUTTON_B1, TDO_BUTTON_B),      // Cross/B → 3DO B (middle)
    MAP_BUTTON(JP_BUTTON_B2, TDO_BUTTON_C),      // Circle/A → 3DO C (bottom)
    MAP_BUTTON(JP_BUTTON_B3, TDO_BUTTON_A),      // Square/X → 3DO A (top)
    MAP_DISABLED(JP_BUTTON_B4),                   // Triangle/Y → nothing

    // Shoulders - both L1/L2 map to L, both R1/R2 map to R
    MAP_BUTTON(JP_BUTTON_L1, TDO_BUTTON_L),
    MAP_BUTTON(JP_BUTTON_L2, TDO_BUTTON_L),
    MAP_BUTTON(JP_BUTTON_R1, TDO_BUTTON_R),
    MAP_BUTTON(JP_BUTTON_R2, TDO_BUTTON_R),

    // System
    MAP_BUTTON(JP_BUTTON_S1, TDO_BUTTON_X),      // Select → X (Stop)
    MAP_BUTTON(JP_BUTTON_S2, TDO_BUTTON_P),      // Start → P (Play)
};

static const profile_t tdo_profile_default = {
    .name = "default",
    .description = "Standard SNES23DO layout",
    .button_map = tdo_default_map,
    .button_map_count = sizeof(tdo_default_map) / sizeof(tdo_default_map[0]),
    .l2_behavior = TRIGGER_PASSTHROUGH,
    .r2_behavior = TRIGGER_PASSTHROUGH,
    .l2_threshold = 128,
    .r2_threshold = 128,
    .l2_analog_value = 0,
    .r2_analog_value = 0,
    .left_stick_sensitivity = 1.0f,
    .right_stick_sensitivity = 1.0f,
    .adaptive_triggers = false,
};

// ============================================================================
// PROFILE: Fighting - 6-Button Fighting Game Layout
// ============================================================================
// Optimized for fighting games like Way of the Warrior

static const button_map_entry_t tdo_fighting_map[] = {
    // Face buttons - 6-button layout
    MAP_BUTTON(JP_BUTTON_B1, TDO_BUTTON_B),      // Light Punch
    MAP_BUTTON(JP_BUTTON_B2, TDO_BUTTON_C),      // Medium Punch
    MAP_BUTTON(JP_BUTTON_B3, TDO_BUTTON_A),      // Heavy Punch
    MAP_BUTTON(JP_BUTTON_B4, TDO_BUTTON_P),      // Light Kick

    // Shoulders - Medium/Heavy Kick
    MAP_BUTTON(JP_BUTTON_L1, TDO_BUTTON_L),      // Medium Kick
    MAP_BUTTON(JP_BUTTON_L2, TDO_BUTTON_R),      // Cross-mapped
    MAP_BUTTON(JP_BUTTON_R1, TDO_BUTTON_R),      // Heavy Kick
    MAP_BUTTON(JP_BUTTON_R2, TDO_BUTTON_L),      // Cross-mapped

    // System
    MAP_BUTTON(JP_BUTTON_S1, TDO_BUTTON_X),
    MAP_BUTTON(JP_BUTTON_S2, TDO_BUTTON_P),
};

static const profile_t tdo_profile_fighting = {
    .name = "fighting",
    .description = "6-button fighting game layout",
    .button_map = tdo_fighting_map,
    .button_map_count = sizeof(tdo_fighting_map) / sizeof(tdo_fighting_map[0]),
    .l2_behavior = TRIGGER_PASSTHROUGH,
    .r2_behavior = TRIGGER_PASSTHROUGH,
    .l2_threshold = 128,
    .r2_threshold = 128,
    .l2_analog_value = 0,
    .r2_analog_value = 0,
    .left_stick_sensitivity = 1.0f,
    .right_stick_sensitivity = 1.0f,
    .adaptive_triggers = false,
};

// ============================================================================
// PROFILE: Shooter - Shooter Optimized Layout
// ============================================================================
// Shoulders as primary fire buttons

static const button_map_entry_t tdo_shooter_map[] = {
    // Face buttons
    MAP_BUTTON(JP_BUTTON_B1, TDO_BUTTON_C),      // Jump
    MAP_BUTTON(JP_BUTTON_B2, TDO_BUTTON_B),      // Action
    MAP_BUTTON(JP_BUTTON_B3, TDO_BUTTON_A),      // Weapon Switch
    MAP_BUTTON(JP_BUTTON_B4, TDO_BUTTON_X),      // Special

    // Shoulders - Primary/Secondary Fire
    MAP_BUTTON(JP_BUTTON_L1, TDO_BUTTON_L),      // Primary Fire
    MAP_BUTTON(JP_BUTTON_L2, TDO_BUTTON_L),      // Primary Fire (OR)
    MAP_BUTTON(JP_BUTTON_R1, TDO_BUTTON_R),      // Secondary Fire
    MAP_BUTTON(JP_BUTTON_R2, TDO_BUTTON_R),      // Secondary Fire (OR)

    // System
    MAP_DISABLED(JP_BUTTON_S1),                   // Unused
    MAP_BUTTON(JP_BUTTON_S2, TDO_BUTTON_P),      // Pause
};

static const profile_t tdo_profile_shooter = {
    .name = "shooter",
    .description = "Shooter-optimized layout",
    .button_map = tdo_shooter_map,
    .button_map_count = sizeof(tdo_shooter_map) / sizeof(tdo_shooter_map[0]),
    .l2_behavior = TRIGGER_PASSTHROUGH,
    .r2_behavior = TRIGGER_PASSTHROUGH,
    .l2_threshold = 128,
    .r2_threshold = 128,
    .l2_analog_value = 0,
    .r2_analog_value = 0,
    .left_stick_sensitivity = 1.0f,
    .right_stick_sensitivity = 1.0f,
    .adaptive_triggers = false,
};

// ============================================================================
// PROFILE SET
// ============================================================================

static const profile_t tdo_profiles[] = {
    tdo_profile_default,
    tdo_profile_fighting,
    tdo_profile_shooter,
};

static const profile_set_t tdo_profile_set = {
    .profiles = tdo_profiles,
    .profile_count = sizeof(tdo_profiles) / sizeof(tdo_profiles[0]),
    .default_index = 0,
};

#endif // USB23DO_PROFILES_H
