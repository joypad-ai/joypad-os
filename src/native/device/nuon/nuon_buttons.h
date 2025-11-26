// nuon_buttons.h - Nuon Button Aliases
//
// Aliases for USBR_BUTTON_* constants that map to Nuon button names.
// These are purely for readability in profile definitions.
//
// Example usage in a profile:
//   { .input = USBR_BUTTON_B1, .output = NUON_BUTTON_A }
//   This reads: "USBR B1 maps to Nuon A button"

#ifndef NUON_BUTTONS_H
#define NUON_BUTTONS_H

#include "core/buttons.h"

// ============================================================================
// NUON BUTTON ALIASES
// ============================================================================
// These aliases equal the USBR_BUTTON_* values they represent on Nuon.
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
#define NUON_BTN_A          USBR_BUTTON_B1  // A (Cross position)
#define NUON_BTN_B          USBR_BUTTON_B3  // B (Square position)

// C-pad buttons (similar to N64 C-buttons)
#define NUON_BTN_C_DOWN     USBR_BUTTON_B2  // C-Down (Circle position)
#define NUON_BTN_C_LEFT     USBR_BUTTON_B4  // C-Left (Triangle position)
#define NUON_BTN_C_UP       USBR_BUTTON_L2  // C-Up (L2 position)
#define NUON_BTN_C_RIGHT    USBR_BUTTON_R2  // C-Right (R2 position)

// Shoulder buttons
#define NUON_BTN_L          USBR_BUTTON_L1  // L shoulder
#define NUON_BTN_R          USBR_BUTTON_R1  // R shoulder

// System buttons
#define NUON_BTN_START      USBR_BUTTON_S2  // Start
#define NUON_BTN_NUON       USBR_BUTTON_S1  // Nuon/Z button

// D-pad
#define NUON_BTN_DU         USBR_BUTTON_DU  // D-pad Up
#define NUON_BTN_DD         USBR_BUTTON_DD  // D-pad Down
#define NUON_BTN_DL         USBR_BUTTON_DL  // D-pad Left
#define NUON_BTN_DR         USBR_BUTTON_DR  // D-pad Right

#endif // NUON_BUTTONS_H
