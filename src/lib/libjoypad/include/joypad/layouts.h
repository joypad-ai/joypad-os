// joypad/layouts.h
// Physical button layout classification.
//
// Used by glyph-picking code in games (PlayStation circle vs Xbox B vs Switch A)
// and by console output code (transform mapping to match target console layout).
//
// Originally lived in input_event.h; extracted here so layout-only consumers
// don't need the full input_event_t surface. input_event.h transitively
// includes this so existing code keeps working.

#ifndef JOYPAD_LAYOUTS_H
#define JOYPAD_LAYOUTS_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// Controller Button Layout Classification
// ============================================================================
// GP2040-CE canonical mapping (libjoypad internal standard):
//   Top row:    [B3][B4][R1]
//   Bottom row: [B1][B2][R2]
//
// Physical layouts:
//   SEGA_6BUTTON:  Top [X][Y][Z],   Bottom [A][B][C]
//   PCE_6BUTTON:   Top [IV][V][VI], Bottom [III][II][I]
//   ASTROCITY:     Top [A][B][C],   Bottom [D][E][F]
//   3DO_3BUTTON:   Single row [A][B][C]

typedef enum {
    LAYOUT_UNKNOWN = 0,         // Unknown or default (4-face button modern gamepad)
    LAYOUT_MODERN_4FACE,        // SNES/PlayStation style (no 6-button row)
    LAYOUT_NINTENDO_4FACE,      // Nintendo SNES: BAYX face style
    LAYOUT_NINTENDO_N64,        // Nintendo N64: A/B + C-buttons + Z
    LAYOUT_GAMECUBE,            // GameCube: AXBY face style
    LAYOUT_SEGA_6BUTTON,        // Genesis/Saturn: Bottom [A][B][C], Top [X][Y][Z]
    LAYOUT_PCE_6BUTTON,         // PCEngine Avenue Pad: Bottom [III][II][I], Top [IV][V][VI]
    LAYOUT_ASTROCITY,           // Astrocity: Bottom [D][E][F], Top [A][B][C]
    LAYOUT_3DO_3BUTTON,         // 3DO: Single row [A][B][C] (maps to bottom row only)
    LAYOUT_WII_NUNCHUCK,        // Wii Nunchuck: C/Z + stick + accel
    LAYOUT_WII_CLASSIC,         // Wii Classic: SNES-like faces + 2 sticks + analog L/R
    LAYOUT_WII_CLASSIC_PRO,     // Wii Classic Pro: same as Classic but digital L/R only
    LAYOUT_WII_GUITAR,          // Guitar Hero 3 / World Tour guitar
    LAYOUT_WII_DRUMS,           // Rock Band / Guitar Hero drums
    LAYOUT_WII_TURNTABLE,       // DJ Hero turntable
    LAYOUT_WII_TAIKO,           // Taiko no Tatsujin TaTaCon
    LAYOUT_WII_UDRAW,           // THQ uDraw tablet
    LAYOUT_WII_MOTIONPLUS,      // MotionPlus standalone (gyro only)
    LAYOUT_WII_DUAL_NUNCHUCK,   // Two nunchucks: left C/Z+stick, right C/Z+stick
    LAYOUT_PSX_DIGITAL,         // PS1 digital pad (ID 0x41): Sony faces, no sticks
    LAYOUT_PSX_DUALSHOCK,       // PS1/PS2 analog DualShock (ID 0x73)
    LAYOUT_PSX_DUALSHOCK2,      // PS2 DualShock 2 (ID 0x79): pressure-sensitive
    LAYOUT_PSX_NEGCON,          // Namco neGcon (ID 0x23): twist + analog I/II/L
    LAYOUT_PSX_FLIGHTSTICK,     // Analog Joystick / Dual Analog flight mode (ID 0x53)
    LAYOUT_PSX_GUNCON,          // Namco GunCon light gun (ID 0x63): aim on right stick
    LAYOUT_PSX_JOGCON,          // Namco JogCon (ID 0xE3): paddle wheel on left stick X
    LAYOUT_PSX_MOUSE,           // PlayStation Mouse (ID 0x12): 2 buttons + dx/dy
} controller_layout_t;

// ============================================================================
// Layout Transform Helpers
// ============================================================================
// Console output code uses these transforms to match a target console's expected
// physical layout. Drivers always output canonical GP2040-CE mapping; transforms
// happen at the output side.
//
// GP2040-CE Canonical (libjoypad internal standard):
//   Top row:    [B3][B4][R1]   (USBR: B3, B4, R1)
//   Bottom row: [B1][B2][R2]   (USBR: B1, B2, R2)
//
// For 6-button layouts:
//   Position:    Left-Bot  Mid-Bot  Right-Bot  Left-Top  Mid-Top  Right-Top
//   GP2040-CE:   B1        B2       R2         B3        B4       R1
//   PCEngine:    III       II       I          IV        V        VI
//   Genesis:     A         B        C          X         Y        Z
//   Astrocity:   D         E        F          A         B        C

// Button masks for 6-button face buttons (excludes D-pad, Start, Select, etc.)
#define LAYOUT_6BTN_MASK (0x0B230)  // B1|B2|B3|B4|R1|R2

// Helper to extract a button, returning its state (active-high: 1 = pressed)
#define EXTRACT_BTN(buttons, mask) (((buttons) & (mask)) ? 1 : 0)

// Transform buttons from source layout to PCEngine 6-button layout.
// PCEngine expects: Bottom [III][II][I], Top [IV][V][VI]
// where III=leftmost, I=rightmost (numbers decrease left to right).
//
// Since libjoypad uses GP2040-CE naming and PCE uses the same physical positions,
// the button bits are already correct — no transformation needed for SEGA_6BUTTON
// or ASTROCITY when targeting PCEngine.
static inline uint32_t transform_to_pce_layout(uint32_t buttons, controller_layout_t source) {
    if (source == LAYOUT_PCE_6BUTTON || source == LAYOUT_UNKNOWN || source == LAYOUT_MODERN_4FACE) {
        return buttons;
    }

    if (source == LAYOUT_3DO_3BUTTON) {
        // 3DO A/B/C maps to PCE III/II/I (bottom row); top row buttons ignored.
        return buttons;
    }

    return buttons;
}

// Check if a controller has a 6-button layout (two rows of 3)
static inline bool layout_has_6_buttons(controller_layout_t layout) {
    return (layout == LAYOUT_SEGA_6BUTTON ||
            layout == LAYOUT_PCE_6BUTTON ||
            layout == LAYOUT_ASTROCITY);
}

// Check if a controller has a 3-button single row layout
static inline bool layout_has_3_buttons(controller_layout_t layout) {
    return (layout == LAYOUT_3DO_3BUTTON);
}

#endif // JOYPAD_LAYOUTS_H
