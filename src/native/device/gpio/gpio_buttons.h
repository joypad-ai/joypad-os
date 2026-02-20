// gpio_buttons.h - GPIO Button Aliases
//
// Aliases for JP_BUTTON_* constants that map to GPIO button names.
// These are purely for readability in profile definitions.
//
// Example usage in a profile:
//   { .input = JP_BUTTON_B1, .output = GPIO_B1 }
//   This reads: "USBR B1 maps to GPIO B1"

#ifndef GPIO_BUTTONS_H
#define GPIO_BUTTONS_H

#include "core/buttons.h"

// ============================================================================
// GPIO BUTTON ALIASES
// ============================================================================
// These aliases equal the JP_BUTTON_* values they represent on GPIO.

// D-pad
#define GPIO_BUTTON_DU     JP_BUTTON_DU  // D-pad Up
#define GPIO_BUTTON_DD     JP_BUTTON_DD  // D-pad Down
#define GPIO_BUTTON_DL     JP_BUTTON_DL  // D-pad Left
#define GPIO_BUTTON_DR     JP_BUTTON_DR  // D-pad Right

// GPIO Buttons
#define GPIO_BUTTON_B1     JP_BUTTON_B1
#define GPIO_BUTTON_B2     JP_BUTTON_B2
#define GPIO_BUTTON_B3     JP_BUTTON_B3
#define GPIO_BUTTON_B4     JP_BUTTON_B4
#define GPIO_BUTTON_L1     JP_BUTTON_L1
#define GPIO_BUTTON_R1     JP_BUTTON_R1
#define GPIO_BUTTON_L2     JP_BUTTON_L2
#define GPIO_BUTTON_R2     JP_BUTTON_R2

// System Buttons
#define GPIO_BUTTON_S1     JP_BUTTON_S1
#define GPIO_BUTTON_S2     JP_BUTTON_S2
#define GPIO_BUTTON_A1     JP_BUTTON_A1
#define GPIO_BUTTON_A2     JP_BUTTON_A2

// Extra Buttons
#define GPIO_BUTTON_L3     JP_BUTTON_L3
#define GPIO_BUTTON_R3     JP_BUTTON_R3
#define GPIO_BUTTON_L4     JP_BUTTON_L4
#define GPIO_BUTTON_R4     JP_BUTTON_R4


#endif // GPIO_BUTTONS_H
