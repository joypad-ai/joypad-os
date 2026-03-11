// profiles.h - USB2GC Profile Definitions
//
// Button mapping profiles for USB to GameCube adapter.
// Uses console-specific button aliases for readability.
//
// GameCube button layout:
//   A (B1) - Large green button
//   B (B2) - Small red button
//   X (B4) - Right of A
//   Y (B3) - Above A
//   Z (R1) - Digital shoulder
//   L (L2) - Left trigger (analog + digital)
//   R (R2) - Right trigger (analog + digital)
//   Start (S2)
//   D-pad, Control stick, C-stick

#ifndef USB2GC_PROFILES_H
#define USB2GC_PROFILES_H

#include "core/services/profiles/profile.h"
#include "native/device/gamecube/gamecube_buttons.h"

// ============================================================================
// PROFILE: Default - Standard GameCube Layout
// ============================================================================
// Maps modern controllers to GameCube naturally

static const button_map_entry_t gc_default_map[] = {
    // Face buttons - SNES/PlayStation-style to GameCube
    MAP_BUTTON(JP_BUTTON_B1, GC_BUTTON_B),      // Cross/B → GC B
    MAP_BUTTON(JP_BUTTON_B2, GC_BUTTON_A),      // Circle/A → GC A
    MAP_BUTTON(JP_BUTTON_B3, GC_BUTTON_Y),      // Square/X → GC Y
    MAP_BUTTON(JP_BUTTON_B4, GC_BUTTON_X),      // Triangle/Y → GC X

    // Shoulders
    MAP_BUTTON(JP_BUTTON_R1, GC_BUTTON_Z),      // R1/RB → Z

    // L1/LB → nothing (GC has no equivalent)
    MAP_DISABLED(JP_BUTTON_L1),

    // System
    MAP_BUTTON(JP_BUTTON_S2, GC_BUTTON_START),  // Start → Start
    MAP_DISABLED(JP_BUTTON_S1),                  // Select → nothing (profile switch)
};

static const profile_t gc_profile_default = {
    .name = "default",
    .description = "Standard mapping matching GameCube layout",
    .button_map = gc_default_map,
    .button_map_count = sizeof(gc_default_map) / sizeof(gc_default_map[0]),
    .l2_behavior = TRIGGER_PASSTHROUGH,
    .r2_behavior = TRIGGER_PASSTHROUGH,
    .l2_threshold = 250,
    .r2_threshold = 250,
    .l2_analog_value = 0,
    .r2_analog_value = 0,
    .left_stick_sensitivity = 1.0f,
    .right_stick_sensitivity = 1.0f,
    .left_stick_modifiers = NULL,
    .left_stick_modifier_count = 0,
    .right_stick_modifiers = NULL,
    .right_stick_modifier_count = 0,
    .adaptive_triggers = true,
    .socd_mode = SOCD_PASSTHROUGH,
};

// ============================================================================
// PROFILE: SNES - Original SNES Controller Mapping
// ============================================================================
// For SNES-style controllers: L/R as full press, Select → Z

static const button_map_entry_t gc_snes_map[] = {
    // Face buttons - same as default
    MAP_BUTTON(JP_BUTTON_B1, GC_BUTTON_B),
    MAP_BUTTON(JP_BUTTON_B2, GC_BUTTON_A),
    MAP_BUTTON(JP_BUTTON_B3, GC_BUTTON_Y),
    MAP_BUTTON(JP_BUTTON_B4, GC_BUTTON_X),

    // Shoulders with full analog press
    MAP_BUTTON_ANALOG(JP_BUTTON_L1, GC_BUTTON_L, ANALOG_TARGET_L2_FULL, 0),
    MAP_BUTTON_ANALOG(JP_BUTTON_R1, GC_BUTTON_R, ANALOG_TARGET_R2_FULL, 0),

    // Select → Z
    MAP_BUTTON(JP_BUTTON_S1, GC_BUTTON_Z),
    MAP_BUTTON(JP_BUTTON_S2, GC_BUTTON_START),
};

static const profile_t gc_profile_snes = {
    .name = "snes",
    .description = "SNES mapping: Select→Z, L/R→full press",
    .button_map = gc_snes_map,
    .button_map_count = sizeof(gc_snes_map) / sizeof(gc_snes_map[0]),
    .l2_behavior = TRIGGER_PASSTHROUGH,
    .r2_behavior = TRIGGER_PASSTHROUGH,
    .l2_threshold = 250,
    .r2_threshold = 250,
    .l2_analog_value = 0,
    .r2_analog_value = 0,
    .left_stick_sensitivity = 1.0f,
    .right_stick_sensitivity = 1.0f,
    .left_stick_modifiers = NULL,
    .left_stick_modifier_count = 0,
    .right_stick_modifiers = NULL,
    .right_stick_modifier_count = 0,
    .adaptive_triggers = true,
    .socd_mode = SOCD_PASSTHROUGH,
};

// ============================================================================
// PROFILE: SSBM - Super Smash Bros Melee Competitive
// ============================================================================
// Yoink1975's config: L1→Z, LT→Light shield, RT→L+R quit combo
// L3 = walk modifier (50% sensitivity)

static const button_map_entry_t gc_ssbm_map[] = {
    // Face buttons
    MAP_BUTTON(JP_BUTTON_B1, GC_BUTTON_B),
    MAP_BUTTON(JP_BUTTON_B2, GC_BUTTON_A),
    MAP_BUTTON(JP_BUTTON_B3, GC_BUTTON_Y),
    MAP_BUTTON(JP_BUTTON_B4, GC_BUTTON_X),

    // L1 (LB) → Z
    MAP_BUTTON(JP_BUTTON_L1, GC_BUTTON_Z),

    // R1 (RB) → X (for short hop aerials)
    MAP_BUTTON(JP_BUTTON_R1, GC_BUTTON_X),

    // System
    MAP_DISABLED(JP_BUTTON_S1),
    MAP_BUTTON(JP_BUTTON_S2, GC_BUTTON_START),
};

// L3 = walk modifier (reduces to 50% for precise movement)
static const stick_modifier_t gc_ssbm_left_modifiers[] = {
    STICK_MODIFIER(JP_BUTTON_L3, 0.50f),
};

static const profile_t gc_profile_ssbm = {
    .name = "ssbm",
    .description = "SSBM: LB→Z, L3→walk, 85% stick",
    .button_map = gc_ssbm_map,
    .button_map_count = sizeof(gc_ssbm_map) / sizeof(gc_ssbm_map[0]),
    .l2_behavior = TRIGGER_LIGHT_PRESS,   // Light shield (analog capped, no digital)
    .r2_behavior = TRIGGER_FULL_PRESS,    // Full press for L+R quit combo effect
    .l2_threshold = 0,                    // No digital L from analog (light shield only)
    .r2_threshold = 140,
    .l2_analog_value = 43,                // ~17% light shield
    .r2_analog_value = 0,
    .left_stick_sensitivity = 0.85f,      // 85% for Melee precision
    .right_stick_sensitivity = 1.0f,
    .left_stick_modifiers = gc_ssbm_left_modifiers,
    .left_stick_modifier_count = sizeof(gc_ssbm_left_modifiers) / sizeof(gc_ssbm_left_modifiers[0]),
    .right_stick_modifiers = NULL,
    .right_stick_modifier_count = 0,
    .adaptive_triggers = true,
    .socd_mode = SOCD_NEUTRAL,            // SSBM benefits from neutral SOCD
};

// ============================================================================
// PROFILE: Mario Kart Wii - Drift Optimized
// ============================================================================
// RB→R(full), RT→Z(instant), LB→D-pad Up

static const button_map_entry_t gc_mkwii_map[] = {
    // Face buttons
    MAP_BUTTON(JP_BUTTON_B1, GC_BUTTON_B),
    MAP_BUTTON(JP_BUTTON_B2, GC_BUTTON_A),
    MAP_BUTTON(JP_BUTTON_B3, GC_BUTTON_Y),
    MAP_BUTTON(JP_BUTTON_B4, GC_BUTTON_X),

    // L1 (LB) → D-pad Up (for wheelies/tricks)
    MAP_BUTTON(JP_BUTTON_L1, GC_BUTTON_DU),

    // R1 (RB) → R with full analog
    MAP_BUTTON_ANALOG(JP_BUTTON_R1, GC_BUTTON_R, ANALOG_TARGET_R2_FULL, 0),

    // System
    MAP_DISABLED(JP_BUTTON_S1),
    MAP_BUTTON(JP_BUTTON_S2, GC_BUTTON_START),
};

static const profile_t gc_profile_mkwii = {
    .name = "mkwii",
    .description = "MKWii: RB→R(full), RT→Z(instant), LB→DUp",
    .button_map = gc_mkwii_map,
    .button_map_count = sizeof(gc_mkwii_map) / sizeof(gc_mkwii_map[0]),
    .l2_behavior = TRIGGER_PASSTHROUGH,
    .r2_behavior = TRIGGER_INSTANT,       // Very sensitive RT for instant Z
    .l2_threshold = 250,
    .r2_threshold = 10,                   // Instant trigger
    .l2_analog_value = 0,
    .r2_analog_value = 0,
    .left_stick_sensitivity = 1.0f,
    .right_stick_sensitivity = 1.0f,
    .left_stick_modifiers = NULL,
    .left_stick_modifier_count = 0,
    .right_stick_modifiers = NULL,
    .right_stick_modifier_count = 0,
    .adaptive_triggers = true,
    .socd_mode = SOCD_PASSTHROUGH,
};

// ============================================================================
// PROFILE: Fighting Games
// ============================================================================
// L1→C-Up (for macros), right stick disabled

static const button_map_entry_t gc_fighting_map[] = {
    // Face buttons
    MAP_BUTTON(JP_BUTTON_B1, GC_BUTTON_B),
    MAP_BUTTON(JP_BUTTON_B2, GC_BUTTON_A),
    MAP_BUTTON(JP_BUTTON_B3, GC_BUTTON_Y),
    MAP_BUTTON(JP_BUTTON_B4, GC_BUTTON_X),

    // L1 → C-stick Up (for in-game config/macros)
    MAP_ANALOG_ONLY(JP_BUTTON_L1, ANALOG_TARGET_RY_MAX),

    // R1 → Z
    MAP_BUTTON(JP_BUTTON_R1, GC_BUTTON_Z),

    // System
    MAP_DISABLED(JP_BUTTON_S1),
    MAP_BUTTON(JP_BUTTON_S2, GC_BUTTON_START),
};

static const profile_t gc_profile_fighting = {
    .name = "fighting",
    .description = "Fighting: L1→C-Up, right stick disabled",
    .button_map = gc_fighting_map,
    .button_map_count = sizeof(gc_fighting_map) / sizeof(gc_fighting_map[0]),
    .l2_behavior = TRIGGER_PASSTHROUGH,
    .r2_behavior = TRIGGER_PASSTHROUGH,
    .l2_threshold = 250,
    .r2_threshold = 250,
    .l2_analog_value = 0,
    .r2_analog_value = 0,
    .left_stick_sensitivity = 1.0f,
    .right_stick_sensitivity = 0.0f,      // Disabled
    .left_stick_modifiers = NULL,
    .left_stick_modifier_count = 0,
    .right_stick_modifiers = NULL,
    .right_stick_modifier_count = 0,
    .adaptive_triggers = false,
    .socd_mode = SOCD_UP_PRIORITY,        // Hitbox-style SOCD for fighting games
};

// ============================================================================
// PROFILE SET
// ============================================================================

static const profile_t gc_profiles[] = {
    gc_profile_default,
    gc_profile_snes,
    gc_profile_ssbm,
    gc_profile_mkwii,
    gc_profile_fighting,
};

static const profile_set_t gc_profile_set = {
    .profiles = gc_profiles,
    .profile_count = sizeof(gc_profiles) / sizeof(gc_profiles[0]),
    .default_index = 0,
};

#endif // USB2GC_PROFILES_H
