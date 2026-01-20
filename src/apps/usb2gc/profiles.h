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
// PROFILE: Mario Kart Wii - PS5 -> GameCube (Competitive)
// ============================================================================
// PS5 bindings (via USB2GC):
//   Circle    -> A (accelerate)
//   Square    -> B
//   Triangle  -> X, Y
//   L2        -> D-pad Up (wheelie / up trick)
//   L1        -> L (drift - analog + digital)
//   R1        -> B + R digital
//   Cross (X) -> R analog
//   OPTIONS   -> Start
//   SHARE     -> disabled

static const button_map_entry_t gc_mkwii_map[] = {
    // Face buttons
    MAP_BUTTON(JP_BUTTON_B2, GC_BUTTON_A),   // Circle -> A (accelerate)
    MAP_BUTTON(JP_BUTTON_B3, GC_BUTTON_B),   // Square -> B
    MAP_BUTTON(JP_BUTTON_B4, GC_BUTTON_X),   // Triangle -> X
    MAP_BUTTON(JP_BUTTON_B4, GC_BUTTON_Y),   // Triangle -> Y (also)

    // L2 -> D-pad Up (wheelie/trick)
    MAP_BUTTON(JP_BUTTON_L2, GC_BUTTON_DU),

    // L1 -> L trigger (drift) with full analog
    MAP_BUTTON_ANALOG(JP_BUTTON_L1, GC_BUTTON_L, ANALOG_TARGET_L2_FULL, 0),

    // R1 -> B + R digital
    MAP_BUTTON(JP_BUTTON_R1, GC_BUTTON_B),
    MAP_BUTTON_ANALOG(JP_BUTTON_R1, GC_BUTTON_R, ANALOG_TARGET_NONE, 0),

    // Cross (X) -> R analog only (disable button passthrough to prevent B)
    MAP_ANALOG_ONLY(JP_BUTTON_B1, ANALOG_TARGET_R2_FULL),
    MAP_DISABLED(JP_BUTTON_B1),

    // System
    MAP_BUTTON(JP_BUTTON_S2, GC_BUTTON_START), // Options -> Start
    MAP_DISABLED(JP_BUTTON_S1),                // Share -> disabled
};

static const profile_t gc_profile_mkwii = {
    .name = "mkwii_ps5_comp",
    .description = "MKWii PS5: L1=Drift, R1=B+R, X=R analog, L2=Wheelie",
    .button_map = gc_mkwii_map,
    .button_map_count = sizeof(gc_mkwii_map) / sizeof(gc_mkwii_map[0]),

    // Triggers - L2 used as button, disable analog passthrough
    .l2_behavior = TRIGGER_DIGITAL_ONLY,
    .r2_behavior = TRIGGER_DISABLED,

    .l2_threshold = 10,
    .r2_threshold = 0,

    .l2_analog_value = 0,
    .r2_analog_value = 0,

    // Sticks
    .left_stick_sensitivity = 1.0f,
    .right_stick_sensitivity = 1.0f,
    .left_stick_modifiers = NULL,
    .left_stick_modifier_count = 0,
    .right_stick_modifiers = NULL,
    .right_stick_modifier_count = 0,

    .adaptive_triggers = false,
};

// ============================================================================
// PROFILE SET
// ============================================================================

static const profile_t gc_profiles[] = {
    gc_profile_mkwii,
};

static const profile_set_t gc_profile_set = {
    .profiles = gc_profiles,
    .profile_count = sizeof(gc_profiles) / sizeof(gc_profiles[0]),
    .default_index = 0,
};

#endif // USB2GC_PROFILES_H
