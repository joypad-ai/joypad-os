// xboxone_buttons.h - Xbox One Button Aliases
//
// Aliases for USBR_BUTTON_* constants that map to Xbox One button names.
// These are purely for readability in profile definitions.
//
// Example usage in a profile:
//   { .input = USBR_BUTTON_B1, .output = XB1_BUTTON_A }
//   This reads: "USBR B1 maps to Xbox One A button"

#ifndef XBOXONE_BUTTONS_H
#define XBOXONE_BUTTONS_H

#include "core/buttons.h"

// ============================================================================
// XBOX ONE BUTTON ALIASES
// ============================================================================
// These aliases equal the USBR_BUTTON_* values they represent on Xbox One.
// The mapping reflects the default/natural position on an Xbox One controller.
//
// Xbox One Controller Layout:
//   A - Green button (bottom)
//   B - Red button (right)
//   X - Blue button (left)
//   Y - Yellow button (top)
//   LB/RB - Bumpers
//   LT/RT - Triggers
//   L3/R3 - Stick clicks
//   View/Menu - System buttons
//   Guide - Xbox button

// Face buttons
#define XB1_BUTTON_A        USBR_BUTTON_B1  // A (green, bottom)
#define XB1_BUTTON_B        USBR_BUTTON_B2  // B (red, right)
#define XB1_BUTTON_X        USBR_BUTTON_B3  // X (blue, left)
#define XB1_BUTTON_Y        USBR_BUTTON_B4  // Y (yellow, top)

// Bumpers
#define XB1_BUTTON_LB       USBR_BUTTON_L1  // Left bumper
#define XB1_BUTTON_RB       USBR_BUTTON_R1  // Right bumper

// Triggers (digital component)
#define XB1_BUTTON_LT       USBR_BUTTON_L2  // Left trigger
#define XB1_BUTTON_RT       USBR_BUTTON_R2  // Right trigger

// Stick clicks
#define XB1_BUTTON_L3       USBR_BUTTON_L3  // Left stick click
#define XB1_BUTTON_R3       USBR_BUTTON_R3  // Right stick click

// System buttons
#define XB1_BUTTON_VIEW     USBR_BUTTON_S1  // View (back/select)
#define XB1_BUTTON_MENU     USBR_BUTTON_S2  // Menu (start)
#define XB1_BUTTON_GUIDE    USBR_BUTTON_A1  // Guide (Xbox button)

// D-pad
#define XB1_BUTTON_DU       USBR_BUTTON_DU  // D-pad Up
#define XB1_BUTTON_DD       USBR_BUTTON_DD  // D-pad Down
#define XB1_BUTTON_DL       USBR_BUTTON_DL  // D-pad Left
#define XB1_BUTTON_DR       USBR_BUTTON_DR  // D-pad Right

#endif // XBOXONE_BUTTONS_H
