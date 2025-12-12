// loopy_buttons.h - Casio Loopy Button Aliases
//
// Aliases for JP_BUTTON_* constants that map to Loopy button names.
// These are purely for readability in profile definitions.
//
// Example usage in a profile:
//   { .input = JP_BUTTON_B1, .output = LOOPY_BUTTON_A }
//   This reads: "USBR B1 maps to Loopy A button"

#ifndef LOOPY_BUTTONS_H
#define LOOPY_BUTTONS_H

#include "core/buttons.h"

// ============================================================================
// LOOPY BUTTON ALIASES
// ============================================================================
// These aliases equal the JP_BUTTON_* values they represent on Loopy.
// The mapping reflects the default/natural position on a Loopy controller.
//
// Loopy Controller Layout:
//   A - Primary action (bottom)
//   B - Secondary action (right)
//   C - Tertiary (top)
//   D - Quaternary (left)
//   L/R - Shoulder triggers
//   Start - Start/Pause

// Face buttons (diamond layout)
#define LOOPY_BUTTON_A      JP_BUTTON_B1  // A (bottom - Cross position)
#define LOOPY_BUTTON_B      JP_BUTTON_B2  // B (right - Circle position)
#define LOOPY_BUTTON_C      JP_BUTTON_B3  // C (top - Square position)
#define LOOPY_BUTTON_D      JP_BUTTON_B4  // D (left - Triangle position)

// Shoulder buttons
#define LOOPY_BUTTON_L      JP_BUTTON_L1  // L trigger
#define LOOPY_BUTTON_R      JP_BUTTON_R1  // R trigger

// System buttons
#define LOOPY_BUTTON_START  JP_BUTTON_S2  // Start

// D-pad
#define LOOPY_BUTTON_DU     JP_BUTTON_DU  // D-pad Up
#define LOOPY_BUTTON_DD     JP_BUTTON_DD  // D-pad Down
#define LOOPY_BUTTON_DL     JP_BUTTON_DL  // D-pad Left
#define LOOPY_BUTTON_DR     JP_BUTTON_DR  // D-pad Right

#endif // LOOPY_BUTTONS_H
