// 3do_buttons.h - 3DO Button Aliases
//
// Aliases for JP_BUTTON_* constants that map to 3DO button names.
// These are purely for readability in profile definitions.
//
// Example usage in a profile:
//   { .input = JP_BUTTON_B1, .output = TDO_BUTTON_B }
//   This reads: "USBR B1 maps to 3DO B button"

#ifndef _3DO_BUTTONS_H
#define _3DO_BUTTONS_H

#include "core/buttons.h"

// ============================================================================
// 3DO BUTTON ALIASES
// ============================================================================
// These aliases equal the JP_BUTTON_* values they represent on 3DO.
// The mapping reflects the default/natural position on a 3DO controller.

// Face buttons (3DO has A, B, C arranged vertically on right side)
#define TDO_BUTTON_A        JP_BUTTON_B3  // A (top)
#define TDO_BUTTON_B        JP_BUTTON_B1  // B (middle)
#define TDO_BUTTON_C        JP_BUTTON_B2  // C (bottom)

// Shoulder buttons
#define TDO_BUTTON_L        JP_BUTTON_L1  // L shoulder
#define TDO_BUTTON_R        JP_BUTTON_R1  // R shoulder

// System buttons
#define TDO_BUTTON_X        JP_BUTTON_S1  // X (Stop/Select)
#define TDO_BUTTON_P        JP_BUTTON_S2  // P (Play/Start)

// D-pad
#define TDO_BUTTON_DU       JP_BUTTON_DU  // D-pad Up
#define TDO_BUTTON_DD       JP_BUTTON_DD  // D-pad Down
#define TDO_BUTTON_DL       JP_BUTTON_DL  // D-pad Left
#define TDO_BUTTON_DR       JP_BUTTON_DR  // D-pad Right

// ============================================================================
// 3DO-SPECIFIC OUTPUT ACTIONS
// ============================================================================
// Special outputs for 3DO joystick mode

typedef enum {
    TDO_ACTION_NONE = 0,

    // Joystick mode has a FIRE button (8th button)
    TDO_ACTION_FIRE,

} tdo_action_t;

#endif // _3DO_BUTTONS_H
