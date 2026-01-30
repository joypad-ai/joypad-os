// profiles.h - USB2N64 Profile Definitions
//
// Button mapping profiles for USB to N64 adapter.
// Uses N64-specific button aliases for readability.
//
// N64 button layout:
//   A - Large blue button
//   B - Small green button
//   Z - Under controller trigger
//   L - Left shoulder
//   R - Right shoulder
//   Start
//   D-pad
//   Control stick (single analog)
//   C-Up, C-Down, C-Left, C-Right (4 yellow buttons)

#ifndef USB2N64_PROFILES_H
#define USB2N64_PROFILES_H

#include "core/services/profiles/profile.h"
#include "native/device/n64/n64_buttons.h"

// ============================================================================
// PROFILE: Default - Standard N64 Layout
// ============================================================================
// Maps modern controllers to N64 naturally
// Right stick → C-buttons, LT → Z, L1/R1 → L/R shoulders

static const button_map_entry_t n64_default_map[] = {
    // Face buttons
    MAP_BUTTON(JP_BUTTON_B1, N64_BUTTON_B),      // Cross/B → N64 B
    MAP_BUTTON(JP_BUTTON_B2, N64_BUTTON_A),      // Circle/A → N64 A

    // C-buttons from face buttons (for controllers without a good right stick)
    MAP_BUTTON(JP_BUTTON_B3, N64_BUTTON_CD),     // Square/X → C-Down
    MAP_BUTTON(JP_BUTTON_B4, N64_BUTTON_CU),     // Triangle/Y → C-Up

    // Shoulders
    MAP_BUTTON(JP_BUTTON_L1, N64_BUTTON_L),      // L1/LB → L
    MAP_BUTTON(JP_BUTTON_R1, N64_BUTTON_R),      // R1/RB → R

    // Z trigger from left trigger
    MAP_BUTTON(JP_BUTTON_L2, N64_BUTTON_Z),      // LT/L2 → Z

    // Right trigger → Z as well (both triggers = Z for comfort)
    MAP_BUTTON(JP_BUTTON_R2, N64_BUTTON_Z),      // RT/R2 → Z

    // System
    MAP_BUTTON(JP_BUTTON_S2, N64_BUTTON_START),  // Start → Start
    MAP_DISABLED(JP_BUTTON_S1),                   // Select → nothing (profile switch)

    // L3/R3 unused
    MAP_DISABLED(JP_BUTTON_L3),
    MAP_DISABLED(JP_BUTTON_R3),
};

static const profile_t n64_profile_default = {
    .name = "default",
    .description = "Standard N64 mapping, right stick→C-buttons",
    .button_map = n64_default_map,
    .button_map_count = sizeof(n64_default_map) / sizeof(n64_default_map[0]),
    .l2_behavior = TRIGGER_PASSTHROUGH,
    .r2_behavior = TRIGGER_PASSTHROUGH,
    .l2_threshold = 128,
    .r2_threshold = 128,
    .l2_analog_value = 0,
    .r2_analog_value = 0,
    .left_stick_sensitivity = 1.0f,
    .right_stick_sensitivity = 1.0f,       // Right stick → C-buttons via threshold
    .left_stick_modifiers = NULL,
    .left_stick_modifier_count = 0,
    .right_stick_modifiers = NULL,
    .right_stick_modifier_count = 0,
    .adaptive_triggers = false,
    .socd_mode = SOCD_PASSTHROUGH,
};

// ============================================================================
// PROFILE: FPS - GoldenEye / Perfect Dark Style
// ============================================================================
// Optimized for N64 FPS games
// Right stick → C-buttons for strafing/looking
// R1 → Z (easier reach for shooting)

static const button_map_entry_t n64_fps_map[] = {
    MAP_BUTTON(JP_BUTTON_B1, N64_BUTTON_B),
    MAP_BUTTON(JP_BUTTON_B2, N64_BUTTON_A),
    MAP_BUTTON(JP_BUTTON_B3, N64_BUTTON_CD),
    MAP_BUTTON(JP_BUTTON_B4, N64_BUTTON_CU),

    // R1 → Z (more natural for FPS shooting)
    MAP_BUTTON(JP_BUTTON_R1, N64_BUTTON_Z),

    // L1 → R shoulder (aim/targeting)
    MAP_BUTTON(JP_BUTTON_L1, N64_BUTTON_R),

    // Triggers → L shoulder (useful for GoldenEye aim mode)
    MAP_BUTTON(JP_BUTTON_L2, N64_BUTTON_L),
    MAP_BUTTON(JP_BUTTON_R2, N64_BUTTON_Z),

    MAP_BUTTON(JP_BUTTON_S2, N64_BUTTON_START),
    MAP_DISABLED(JP_BUTTON_S1),
    MAP_DISABLED(JP_BUTTON_L3),
    MAP_DISABLED(JP_BUTTON_R3),
};

static const profile_t n64_profile_fps = {
    .name = "fps",
    .description = "FPS: R1→Z(shoot), L1→R(aim), right stick→C",
    .button_map = n64_fps_map,
    .button_map_count = sizeof(n64_fps_map) / sizeof(n64_fps_map[0]),
    .l2_behavior = TRIGGER_PASSTHROUGH,
    .r2_behavior = TRIGGER_PASSTHROUGH,
    .l2_threshold = 128,
    .r2_threshold = 128,
    .l2_analog_value = 0,
    .r2_analog_value = 0,
    .left_stick_sensitivity = 1.0f,
    .right_stick_sensitivity = 1.0f,
    .left_stick_modifiers = NULL,
    .left_stick_modifier_count = 0,
    .right_stick_modifiers = NULL,
    .right_stick_modifier_count = 0,
    .adaptive_triggers = false,
    .socd_mode = SOCD_PASSTHROUGH,
};

// ============================================================================
// PROFILE: Mario - Super Mario 64 / Platform Games
// ============================================================================
// B3/B4 → C-buttons for camera, L3 → walk modifier

static const button_map_entry_t n64_mario_map[] = {
    MAP_BUTTON(JP_BUTTON_B1, N64_BUTTON_B),      // Punch/Attack
    MAP_BUTTON(JP_BUTTON_B2, N64_BUTTON_A),      // Jump
    MAP_BUTTON(JP_BUTTON_B3, N64_BUTTON_CD),     // Camera down
    MAP_BUTTON(JP_BUTTON_B4, N64_BUTTON_CU),     // Camera up

    MAP_BUTTON(JP_BUTTON_L1, N64_BUTTON_L),      // L for crouch camera
    MAP_BUTTON(JP_BUTTON_R1, N64_BUTTON_R),      // R for shoulder camera
    MAP_BUTTON(JP_BUTTON_L2, N64_BUTTON_Z),      // Z for crouch/ground pound
    MAP_BUTTON(JP_BUTTON_R2, N64_BUTTON_Z),      // Alternate Z

    MAP_BUTTON(JP_BUTTON_S2, N64_BUTTON_START),
    MAP_DISABLED(JP_BUTTON_S1),
    MAP_DISABLED(JP_BUTTON_R3),
};

// L3 = walk modifier (reduces stick to ~50% for precise Mario movement)
static const stick_modifier_t n64_mario_left_modifiers[] = {
    STICK_MODIFIER(JP_BUTTON_L3, 0.50f),
};

static const profile_t n64_profile_mario = {
    .name = "mario",
    .description = "Mario: L3→walk, LT/RT→Z, right stick→C",
    .button_map = n64_mario_map,
    .button_map_count = sizeof(n64_mario_map) / sizeof(n64_mario_map[0]),
    .l2_behavior = TRIGGER_PASSTHROUGH,
    .r2_behavior = TRIGGER_PASSTHROUGH,
    .l2_threshold = 128,
    .r2_threshold = 128,
    .l2_analog_value = 0,
    .r2_analog_value = 0,
    .left_stick_sensitivity = 1.0f,
    .right_stick_sensitivity = 1.0f,
    .left_stick_modifiers = n64_mario_left_modifiers,
    .left_stick_modifier_count = sizeof(n64_mario_left_modifiers) / sizeof(n64_mario_left_modifiers[0]),
    .right_stick_modifiers = NULL,
    .right_stick_modifier_count = 0,
    .adaptive_triggers = false,
    .socd_mode = SOCD_PASSTHROUGH,
};

// ============================================================================
// PROFILE SET
// ============================================================================

static const profile_t n64_profiles[] = {
    n64_profile_default,
    n64_profile_fps,
    n64_profile_mario,
};

static const profile_set_t n64_profile_set = {
    .profiles = n64_profiles,
    .profile_count = sizeof(n64_profiles) / sizeof(n64_profiles[0]),
    .default_index = 0,
};

#endif // USB2N64_PROFILES_H
