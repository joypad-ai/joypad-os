// nuon_buttons.h - Nuon Button Aliases
//
// Aliases for JP_BUTTON_* constants that map to Nuon button names.
// These are purely for readability in profile definitions.
//
// Example usage in a profile:
//   { .input = JP_BUTTON_B1, .output = NUON_BUTTON_A }
//   This reads: "USBR B1 maps to Nuon A button"

#ifndef NUON_BUTTONS_H
#define NUON_BUTTONS_H

#include "core/buttons.h"

// ============================================================================
// NUON BUTTON ALIASES
// ============================================================================
// These aliases equal the JP_BUTTON_* values they represent on Nuon.
// The mapping reflects the default/natural position on a Nuon controller.
//
// Nuon Controller Layout:
//   A - Main action button
//   B - Secondary action
//   C-Up/Down/Left/Right - C-pad (like N64)
//   L/R - Shoulder buttons
//   Start - Start/Pause
//   Nuon/Z - System button

// Face buttons
#define NUON_BTN_A          JP_BUTTON_B1  // A (Cross position)
#define NUON_BTN_B          JP_BUTTON_B3  // B (Square position)

// C-pad buttons (similar to N64 C-buttons)
#define NUON_BTN_C_DOWN     JP_BUTTON_B2  // C-Down (Circle position)
#define NUON_BTN_C_LEFT     JP_BUTTON_B4  // C-Left (Triangle position)
#define NUON_BTN_C_UP       JP_BUTTON_L2  // C-Up (L2 position)
#define NUON_BTN_C_RIGHT    JP_BUTTON_R2  // C-Right (R2 position)

// Shoulder buttons
#define NUON_BTN_L          JP_BUTTON_L1  // L shoulder
#define NUON_BTN_R          JP_BUTTON_R1  // R shoulder

// System buttons
#define NUON_BTN_START      JP_BUTTON_S2  // Start
#define NUON_BTN_NUON       JP_BUTTON_S1  // Nuon/Z button

// D-pad
#define NUON_BTN_DU         JP_BUTTON_DU  // D-pad Up
#define NUON_BTN_DD         JP_BUTTON_DD  // D-pad Down
#define NUON_BTN_DL         JP_BUTTON_DL  // D-pad Left
#define NUON_BTN_DR         JP_BUTTON_DR  // D-pad Right

#endif // NUON_BUTTONS_H
