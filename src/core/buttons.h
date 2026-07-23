// buttons.h
// Joypad canonical button definitions (W3C Gamepad API order)
#ifndef BUTTONS_H
#define BUTTONS_H

/*
 *  JOYPAD BUTTON LAYOUT
 *
 *            ____________________________              __
 *           / [__L2__]          [__R2__] \               |
 *          / [__ L1 __]        [__ R1 __] \              | Triggers
 *       __/________________________________\__         __|
 *      /                                  _   \          |
 *     /      /\           __             (B4)  \         |
 *    /       ||      __  |A1|  __     _       _ \        | Main Pad
 *   |    <===DP===> |S1|      |S2|  (B3) -|- (B2)|       |
 *    \       ||      ¯¯        ¯¯       _       /        |
 *    /\      \/   /   \        /   \   (B1)   /\       __|
 *   /  \________ | LS  | ____ | RS  | _______/  \        |
 *  |         /  \ \___/ /    \ \___/ /  \         |      | Sticks
 *  |        /    \_____/      \_____/    \        |    __|
 *  |       /       L3            R3       \       |
 *   \_____/                                \_____/
 *
 *     |________|______|    |______|___________|
 *       D-Pad    Left       Right    Face
 *               Stick      Stick    Buttons
 *
 *  Extended: A2=Touchpad/Capture  A3=Mute  L4/R4=Paddles
 */

// Array floor: must cover the highest USB dev_addr the host can assign, which
// is CFG_TUH_DEVICE_MAX + CFG_TUH_HUB + 1 (devices AND hubs both take addresses,
// and these arrays are indexed by dev_addr — undersize it and you OOB). With
// the defaults (device cap 10, hub depth 4) that's 15. Overridable per-app:
// RAM-constrained single-output targets (usb2dc) lower it with a smaller cap.
#ifndef MAX_DEVICES
#define MAX_DEVICES 15
#endif

// W3C Gamepad API standard button order
// Bit position = W3C button index (trivial conversion: 1 << index)
//
// Joypad    XInput    Switch    PS3/4/5    DInput
// ------    ------    ------    -------    ------

// Face buttons (right cluster)
#define JP_BUTTON_B1 (1 << 0)   // A         B         Cross      2
#define JP_BUTTON_B2 (1 << 1)   // B         A         Circle     3
#define JP_BUTTON_B3 (1 << 2)   // X         Y         Square     1
#define JP_BUTTON_B4 (1 << 3)   // Y         X         Triangle   4

// Shoulder buttons
#define JP_BUTTON_L1 (1 << 4)   // LB        L         L1         5
#define JP_BUTTON_R1 (1 << 5)   // RB        R         R1         6
#define JP_BUTTON_L2 (1 << 6)   // LT        ZL        L2         7
#define JP_BUTTON_R2 (1 << 7)   // RT        ZR        R2         8

// Center cluster
#define JP_BUTTON_S1 (1 << 8)   // Back      -         Select     9
#define JP_BUTTON_S2 (1 << 9)   // Start     +         Start      10

// Stick clicks
#define JP_BUTTON_L3 (1 << 10)  // LS        LS        L3         11
#define JP_BUTTON_R3 (1 << 11)  // RS        RS        R3         12

// D-pad
#define JP_BUTTON_DU (1 << 12)  // D-Up      D-Up      D-Up       Hat
#define JP_BUTTON_DD (1 << 13)  // D-Down    D-Down    D-Down     Hat
#define JP_BUTTON_DL (1 << 14)  // D-Left    D-Left    D-Left     Hat
#define JP_BUTTON_DR (1 << 15)  // D-Right   D-Right   D-Right    Hat

// Auxiliary
#define JP_BUTTON_A1 (1 << 16)  // Guide     Home      PS         13
#define JP_BUTTON_A2 (1 << 17)  // -         Capture   Touchpad   14
#define JP_BUTTON_A3 (1 << 18)  // -         -         Mute       -
#define JP_BUTTON_A4 (1 << 19)  // -         -         -          -

// Paddles (extended)
#define JP_BUTTON_L4 (1 << 20)  // P1        -         -          -  (upper-left / paddle 1)
#define JP_BUTTON_R4 (1 << 21)  // P2        -         -          -  (upper-right / paddle 1)

// Function keys (internal only — never output to host, only used in hotkey combos)
#define JP_BUTTON_F1 (1 << 22)
#define JP_BUTTON_F2 (1 << 23)

// Second paddle pair (extended) — controllers with four back paddles
// (e.g. Steam Controller 2, Xbox Elite). Map to SInput L/R paddle 2.
#define JP_BUTTON_L5 (1 << 24)  // lower-left  / paddle 2
#define JP_BUTTON_R5 (1 << 25)  // lower-right / paddle 2

// Mask of all function keys (suppressed from output)
#define JP_BUTTON_FN_MASK (JP_BUTTON_F1 | JP_BUTTON_F2)

#endif // BUTTONS_H
