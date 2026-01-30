// n64_buttons.h - N64 Button Aliases
//
// Aliases for JP_BUTTON_* constants that map to N64 button names.
// These are purely for readability in profile definitions.
//
// The N64 controller has:
//   - A, B face buttons
//   - Z trigger (under controller)
//   - L, R shoulder buttons
//   - Start button
//   - D-pad
//   - C-buttons (C-Up, C-Down, C-Left, C-Right)
//   - Single analog stick

#ifndef N64_BUTTONS_H
#define N64_BUTTONS_H

#include "core/buttons.h"

// ============================================================================
// N64 BUTTON ALIASES
// ============================================================================
// These aliases equal the JP_BUTTON_* values they represent on N64.
// The mapping reflects the default/natural position on an N64 controller.

// Face buttons
#define N64_BUTTON_A         JP_BUTTON_B2  // A (large blue button)
#define N64_BUTTON_B         JP_BUTTON_B1  // B (small green button)

// Trigger/Shoulder buttons
#define N64_BUTTON_Z         JP_BUTTON_L2  // Z trigger (under controller grip)
#define N64_BUTTON_L         JP_BUTTON_L1  // L shoulder
#define N64_BUTTON_R         JP_BUTTON_R1  // R shoulder

// System
#define N64_BUTTON_START     JP_BUTTON_S2  // Start

// D-pad
#define N64_BUTTON_DU        JP_BUTTON_DU  // D-pad Up
#define N64_BUTTON_DD        JP_BUTTON_DD  // D-pad Down
#define N64_BUTTON_DL        JP_BUTTON_DL  // D-pad Left
#define N64_BUTTON_DR        JP_BUTTON_DR  // D-pad Right

// C-buttons (mapped to right stick directions and face buttons)
// When using modern controllers, C-buttons can be mapped to right stick
// or to face buttons (B3/B4 for C-Left/C-Up, R2/R1 for C-Down/C-Right)
#define N64_BUTTON_CU        JP_BUTTON_B4  // C-Up    (Y/Triangle on modern)
#define N64_BUTTON_CD        JP_BUTTON_B3  // C-Down  (X/Square on modern)
#define N64_BUTTON_CL        JP_BUTTON_R2  // C-Left  (RT/R2 on modern)
#define N64_BUTTON_CR        JP_BUTTON_R1  // C-Right (RB/R1 on modern)
// Note: Alternative profile can map C-buttons to right stick instead

#endif // N64_BUTTONS_H
