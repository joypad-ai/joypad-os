// pcengine_buttons.h - PCEngine/TurboGrafx-16 Button Aliases
//
// Aliases for JP_BUTTON_* constants that map to PCEngine button names.
// These are purely for readability in profile definitions.
//
// Example usage in a profile:
//   { .input = JP_BUTTON_B1, .output = PCE_BUTTON_I }
//   This reads: "USBR B1 maps to PCEngine button I"

#ifndef PCENGINE_BUTTONS_H
#define PCENGINE_BUTTONS_H

#include "core/buttons.h"

// ============================================================================
// PCENGINE BUTTON ALIASES
// ============================================================================
// These aliases equal the JP_BUTTON_* values they represent on PCEngine.
// The mapping reflects the default/natural position on a PCEngine controller.

// Face buttons (2-button mode)
#define PCE_BUTTON_I        JP_BUTTON_B1  // I (primary action)
#define PCE_BUTTON_II       JP_BUTTON_B2  // II (secondary action)

// 6-button mode (Avenue Pad 6)
#define PCE_BUTTON_III      JP_BUTTON_B3  // III
#define PCE_BUTTON_IV       JP_BUTTON_B4  // IV
#define PCE_BUTTON_V        JP_BUTTON_L1  // V
#define PCE_BUTTON_VI       JP_BUTTON_R1  // VI

// System buttons
#define PCE_BUTTON_SELECT   JP_BUTTON_S1  // Select
#define PCE_BUTTON_RUN      JP_BUTTON_S2  // Run (Start)

// D-pad
#define PCE_BUTTON_DU       JP_BUTTON_DU  // D-pad Up
#define PCE_BUTTON_DD       JP_BUTTON_DD  // D-pad Down
#define PCE_BUTTON_DL       JP_BUTTON_DL  // D-pad Left
#define PCE_BUTTON_DR       JP_BUTTON_DR  // D-pad Right

// ============================================================================
// PCENGINE-SPECIFIC OUTPUT ACTIONS
// ============================================================================
// Special outputs for PCEngine turbo modes

typedef enum {
    PCE_ACTION_NONE = 0,

    // Turbo button variants
    PCE_ACTION_TURBO_I,         // Button I with turbo
    PCE_ACTION_TURBO_II,        // Button II with turbo

} pce_action_t;

#endif // PCENGINE_BUTTONS_H
