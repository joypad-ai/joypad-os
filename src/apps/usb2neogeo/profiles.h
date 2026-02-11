// profiles.h - USB2NEOGEO Profile Definitions
//
// Button mapping profiles for USB to NEOGEO adapter.
// Uses console-specific button aliases for readability.
//
// NEOGEO adapter button layout:
//   B1 (B1) - P1/A
//   B2 (B2) - P2/B
//   B3 (B3) - P3/C
//   B4 (B4) - K1/D
//   B5 (B5) - K2/SELECT
//   B6 (B6) - K3
//   Coin  (S1)
//   Start (S2)
//   D-pad

#ifndef USB2NEOGEO_PROFILES_H
#define USB2NEOGEO_PROFILES_H

#include "core/services/profiles/profile.h"
#include "native/device/gpio/neogeo_buttons.h"

// ============================================================================
// PROFILE: Default - Standard Six Button Layout
// ============================================================================
// Maps NEOGEO+ six buttons to arcade stick layout
//
//  ( )    
//   |      (P1) (P2) (P3) ( )
//          (K1) (K2) (K3) ( )
//

static const button_map_entry_t neogeo_default_map[] = {
    MAP_BUTTON(JP_BUTTON_B3, NEOGEO_BUTTON_B1),    // Square   → P1/A
    MAP_BUTTON(JP_BUTTON_B4, NEOGEO_BUTTON_B2),    // Triangle → P2/B
    MAP_BUTTON(JP_BUTTON_R1, NEOGEO_BUTTON_B3),    // R1       → P3/C
    MAP_BUTTON(JP_BUTTON_B1, NEOGEO_BUTTON_B4),    // Cross    → K1/D
    MAP_BUTTON(JP_BUTTON_B2, NEOGEO_BUTTON_B5),    // Circle   → K2/SELECT
    MAP_BUTTON(JP_BUTTON_R2, NEOGEO_BUTTON_B6),    // R2       → K3

    MAP_DISABLED(JP_BUTTON_L1),
    MAP_DISABLED(JP_BUTTON_L2),
};

static const profile_t neogeo_profile_default = {
    .name = "default",
    .description = "Standard six button layout",
    .button_map = neogeo_default_map,
    .button_map_count = sizeof(neogeo_default_map) / sizeof(neogeo_default_map[0]),
    .l2_behavior = TRIGGER_PASSTHROUGH,
    .r2_behavior = TRIGGER_PASSTHROUGH,
    .l2_threshold = 128,
    .r2_threshold = 128,
    .l2_analog_value = 0,
    .r2_analog_value = 0,
    .left_stick_sensitivity = 1.0f,
    .right_stick_sensitivity = 1.0f,
    .adaptive_triggers = false,
    .socd_mode = SOCD_UP_PRIORITY,
};

// ============================================================================
// PROFILE: Standard Six Button Layout Right
// ============================================================================
// Maps NEOGEO+ six buttons to arcade stick layout at rigth
//
//  ( )    
//   |      ( ) (P1) (P2) (P3)
//          ( ) (K1) (K2) (K3)
//

static const button_map_entry_t neogeo_typea_map[] = {
    MAP_BUTTON(JP_BUTTON_B4, NEOGEO_BUTTON_B1),    // Triangle → P1/A
    MAP_BUTTON(JP_BUTTON_R1, NEOGEO_BUTTON_B2),    // R1       → P2/B
    MAP_BUTTON(JP_BUTTON_L1, NEOGEO_BUTTON_B3),    // L1       → P3/C
    MAP_BUTTON(JP_BUTTON_B2, NEOGEO_BUTTON_B4),    // Circle   → K1/D
    MAP_BUTTON(JP_BUTTON_R2, NEOGEO_BUTTON_B5),    // R2       → K2/SELECT
    MAP_BUTTON(JP_BUTTON_L2, NEOGEO_BUTTON_B6),    // L2       → K3

    MAP_DISABLED(JP_BUTTON_B1),
    MAP_DISABLED(JP_BUTTON_B3),
};

static const profile_t neogeo_profile_typea = {
    .name = "typea",
    .description = "Standard six button layout right",
    .button_map = neogeo_typea_map,
    .button_map_count = sizeof(neogeo_typea_map) / sizeof(neogeo_typea_map[0]),
    .l2_behavior = TRIGGER_PASSTHROUGH,
    .r2_behavior = TRIGGER_PASSTHROUGH,
    .l2_threshold = 128,
    .r2_threshold = 128,
    .l2_analog_value = 0,
    .r2_analog_value = 0,
    .left_stick_sensitivity = 1.0f,
    .right_stick_sensitivity = 1.0f,
    .adaptive_triggers = false,
    .socd_mode = SOCD_UP_PRIORITY,
};

// ============================================================================
// PROFILE: NEOGEO Six Button Type 1
// ============================================================================
// Maps NEOGEO classic buttons to arcade stick layout type 1
//
//  ( )    
//   |      (B) (C ) (D ) ( )
//          (A) (K2) (K3) ( )
//

static const button_map_entry_t neogeo_typeb_map[] = {
    MAP_BUTTON(JP_BUTTON_B1, NEOGEO_BUTTON_B1),    // Cross    → P1/A
    MAP_BUTTON(JP_BUTTON_B3, NEOGEO_BUTTON_B2),    // Square   → P2/B
    MAP_BUTTON(JP_BUTTON_B4, NEOGEO_BUTTON_B3),    // Triangle → P3/C
    MAP_BUTTON(JP_BUTTON_R1, NEOGEO_BUTTON_B4),    // R1       → K1/D
    MAP_BUTTON(JP_BUTTON_B2, NEOGEO_BUTTON_B5),    // Circle   → K2/SELECT
    MAP_BUTTON(JP_BUTTON_R2, NEOGEO_BUTTON_B6),    // R2       → K3

    MAP_DISABLED(JP_BUTTON_L1),
    MAP_DISABLED(JP_BUTTON_L2),
};

static const profile_t neogeo_profile_typeb = {
    .name = "typeb",
    .description = "NEOGEO Six Button Type 1",
    .button_map = neogeo_typeb_map,
    .button_map_count = sizeof(neogeo_typeb_map) / sizeof(neogeo_typeb_map[0]),
    .l2_behavior = TRIGGER_PASSTHROUGH,
    .r2_behavior = TRIGGER_PASSTHROUGH,
    .l2_threshold = 128,
    .r2_threshold = 128,
    .l2_analog_value = 0,
    .r2_analog_value = 0,
    .left_stick_sensitivity = 1.0f,
    .right_stick_sensitivity = 1.0f,
    .adaptive_triggers = false,
    .socd_mode = SOCD_UP_PRIORITY,
};

// ============================================================================
// PROFILE: NEOGEO Six Button Type 2
// ============================================================================
// Maps NEOGEO classic buttons to arcade stick layout type 2
//
//  ( )    
//   |      ( ) (B ) (C ) (D)
//          (A) (K2) (K3) ( )
//

static const button_map_entry_t neogeo_typec_map[] = {
    MAP_BUTTON(JP_BUTTON_B1, NEOGEO_BUTTON_B1),    // Cross    → P1/A
    MAP_BUTTON(JP_BUTTON_B4, NEOGEO_BUTTON_B2),    // Triangle → P2/B
    MAP_BUTTON(JP_BUTTON_R1, NEOGEO_BUTTON_B3),    // R1       → P3/C
    MAP_BUTTON(JP_BUTTON_L1, NEOGEO_BUTTON_B4),    // L1       → K1/D
    MAP_BUTTON(JP_BUTTON_B2, NEOGEO_BUTTON_B5),    // Circle   → K2/SELECT
    MAP_BUTTON(JP_BUTTON_R2, NEOGEO_BUTTON_B6),    // L2       → K3

    MAP_DISABLED(JP_BUTTON_B3),
    MAP_DISABLED(JP_BUTTON_L2),
};

static const profile_t neogeo_profile_typec = {
    .name = "typec",
    .description = "NEOGEO Six Button Type 2",
    .button_map = neogeo_typec_map,
    .button_map_count = sizeof(neogeo_typec_map) / sizeof(neogeo_typec_map[0]),
    .l2_behavior = TRIGGER_PASSTHROUGH,
    .r2_behavior = TRIGGER_PASSTHROUGH,
    .l2_threshold = 128,
    .r2_threshold = 128,
    .l2_analog_value = 0,
    .r2_analog_value = 0,
    .left_stick_sensitivity = 1.0f,
    .right_stick_sensitivity = 1.0f,
    .adaptive_triggers = false,
    .socd_mode = SOCD_UP_PRIORITY,
};

// ============================================================================
// PROFILE: NEOGEO Six Button Type 3
// ============================================================================
// Maps NEOGEO classic buttons to arcade stick layout type 3
//
//  ( )    
//   |      (A ) (B ) (C) (D)
//          (K2) (K3) ( ) ( )
//

static const button_map_entry_t neogeo_typed_map[] = {
    MAP_BUTTON(JP_BUTTON_B3, NEOGEO_BUTTON_B1),    // Square   → P1/A
    MAP_BUTTON(JP_BUTTON_B4, NEOGEO_BUTTON_B2),    // Triangle → P2/B
    MAP_BUTTON(JP_BUTTON_R1, NEOGEO_BUTTON_B3),    // R1       → P3/C
    MAP_BUTTON(JP_BUTTON_L1, NEOGEO_BUTTON_B4),    // L1       → K1/D
    MAP_BUTTON(JP_BUTTON_B1, NEOGEO_BUTTON_B5),    // Cross    → K2/SELECT
    MAP_BUTTON(JP_BUTTON_B2, NEOGEO_BUTTON_B6),    // Circle   → K3

    MAP_DISABLED(JP_BUTTON_L2),
    MAP_DISABLED(JP_BUTTON_R2),
};

static const profile_t neogeo_profile_typed= {
    .name = "typed",
    .description = "NEOGEO Six Button Type 3",
    .button_map = neogeo_typed_map,
    .button_map_count = sizeof(neogeo_typed_map) / sizeof(neogeo_typed_map[0]),
    .l2_behavior = TRIGGER_PASSTHROUGH,
    .r2_behavior = TRIGGER_PASSTHROUGH,
    .l2_threshold = 128,
    .r2_threshold = 128,
    .l2_analog_value = 0,
    .r2_analog_value = 0,
    .left_stick_sensitivity = 1.0f,
    .right_stick_sensitivity = 1.0f,
    .adaptive_triggers = false,
    .socd_mode = SOCD_UP_PRIORITY,
};

// ============================================================================
// PROFILE: NEOGEO Pad Type 1
// ============================================================================
// Maps modern controllers to NEOGEO pad classic style
//
// [ K3 ]       [ K2 ]
//    ▲           (D)
// ◄ ( ) ►     (C)   (B)
//    ▼           (A) 
//

static const button_map_entry_t neogeo_pada_map[] = {
    MAP_BUTTON(JP_BUTTON_B1, NEOGEO_BUTTON_B1),    // Cross    → A
    MAP_BUTTON(JP_BUTTON_B2, NEOGEO_BUTTON_B2),    // Circle   → B
    MAP_BUTTON(JP_BUTTON_B3, NEOGEO_BUTTON_B3),    // Square   → C
    MAP_BUTTON(JP_BUTTON_B4, NEOGEO_BUTTON_B4),    // Triangle → D
    MAP_BUTTON(JP_BUTTON_R1, NEOGEO_BUTTON_B5),    // R1       → K2/SELECT
    MAP_BUTTON(JP_BUTTON_L1, NEOGEO_BUTTON_B6),    // L1       → K3

    MAP_DISABLED(JP_BUTTON_L2),
    MAP_DISABLED(JP_BUTTON_R2),
};

static const profile_t neogeo_profile_pada = {
    .name = "pada",
    .description = "NEOGEO Pad Type 1",
    .button_map = neogeo_pada_map,
    .button_map_count = sizeof(neogeo_pada_map) / sizeof(neogeo_pada_map[0]),
    .l2_behavior = TRIGGER_PASSTHROUGH,
    .r2_behavior = TRIGGER_PASSTHROUGH,
    .l2_threshold = 128,
    .r2_threshold = 128,
    .l2_analog_value = 0,
    .r2_analog_value = 0,
    .left_stick_sensitivity = 1.0f,
    .right_stick_sensitivity = 1.0f,
    .adaptive_triggers = false,
    .socd_mode = SOCD_UP_PRIORITY,
};

// ============================================================================
// PROFILE: NEOGEO Pad Type 2
// ============================================================================
// Maps modern controllers to NEOGEO pad modern/KOF style
//
// [ K3 ]       [ K2 ]
//    ▲           (C)
// ◄ ( ) ►     (A)   (D)
//    ▼           (B) 
//

static const button_map_entry_t neogeo_padb_map[] = {
    MAP_BUTTON(JP_BUTTON_B3, NEOGEO_BUTTON_B1),    // Square   → A
    MAP_BUTTON(JP_BUTTON_B1, NEOGEO_BUTTON_B2),    // Cross    → B
    MAP_BUTTON(JP_BUTTON_B4, NEOGEO_BUTTON_B3),    // Triangle → C
    MAP_BUTTON(JP_BUTTON_B2, NEOGEO_BUTTON_B4),    // Circle   → D
    MAP_BUTTON(JP_BUTTON_R1, NEOGEO_BUTTON_B5),    // R1       → K2/SELECT
    MAP_BUTTON(JP_BUTTON_L1, NEOGEO_BUTTON_B6),    // L1       → K3

    MAP_DISABLED(JP_BUTTON_L2),
    MAP_DISABLED(JP_BUTTON_R2),
};

static const profile_t neogeo_profile_padb = {
    .name = "padb",
    .description = "NEOGEO Pad Type 2",
    .button_map = neogeo_padb_map,
    .button_map_count = sizeof(neogeo_padb_map) / sizeof(neogeo_padb_map[0]),
    .l2_behavior = TRIGGER_PASSTHROUGH,
    .r2_behavior = TRIGGER_PASSTHROUGH,
    .l2_threshold = 128,
    .r2_threshold = 128,
    .l2_analog_value = 0,
    .r2_analog_value = 0,
    .left_stick_sensitivity = 1.0f,
    .right_stick_sensitivity = 1.0f,
    .adaptive_triggers = false,
    .socd_mode = SOCD_UP_PRIORITY,
};

// ============================================================================
// PROFILE SET
// ============================================================================

static const profile_t neogeo_profiles[] = {
    neogeo_profile_default,
    neogeo_profile_typea,
    neogeo_profile_typeb,
    neogeo_profile_typec,
    neogeo_profile_typed,
    neogeo_profile_pada,
    neogeo_profile_padb,
};

static const profile_set_t neogeo_profile_set = {
    .profiles = neogeo_profiles,
    .profile_count = sizeof(neogeo_profiles) / sizeof(neogeo_profiles[0]),
    .default_index = 0,
};

#endif // USB2NEOGEO_PROFILES_H
