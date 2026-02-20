// neogeo_buttons.h - NEOGEO+ Button Aliases
//
// Aliases for JP_BUTTON_* constants that map to NEOGEO+ button names.
// These are purely for readability in profile definitions.
//
// Example usage in a profile:
//   { .input = JP_BUTTON_B1, .output = NEOGEO_B1 }
//   This reads: "USBR B1 maps to NEOGEO+ button 1"

#ifndef NEOGEO_BUTTONS_H
#define NEOGEO_BUTTONS_H

#include "core/buttons.h"

// ============================================================================
// NEOGEO BUTTON ALIASES
// ============================================================================
// These aliases equal the JP_BUTTON_* values they represent on NEOGEO+.
// The mapping reflects the default/natural position on a NEOGEO+ controller.

// D-pad
#define NEOGEO_BUTTON_DU       JP_BUTTON_DU  // D-pad Up
#define NEOGEO_BUTTON_DD       JP_BUTTON_DD  // D-pad Down
#define NEOGEO_BUTTON_DL       JP_BUTTON_DL  // D-pad Left
#define NEOGEO_BUTTON_DR       JP_BUTTON_DR  // D-pad Right

// NEOGEO+ 6 buttons
#define NEOGEO_BUTTON_B1       JP_BUTTON_B3  // P1/A
#define NEOGEO_BUTTON_B2       JP_BUTTON_B4  // P2/B
#define NEOGEO_BUTTON_B3       JP_BUTTON_R1  // P3/C
#define NEOGEO_BUTTON_B4       JP_BUTTON_B1  // K1/D
#define NEOGEO_BUTTON_B5       JP_BUTTON_B2  // K2/Select
#define NEOGEO_BUTTON_B6       JP_BUTTON_R2  // K3

// System buttons
#define NEOGEO_BUTTON_COIN     JP_BUTTON_S1  // Coin
#define NEOGEO_BUTTON_START    JP_BUTTON_S2  // Start


#endif // NEOGEO_BUTTONS_H
