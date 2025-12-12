// buttons.h
// USBRetro canonical button definitions (W3C Gamepad API order)
#ifndef BUTTONS_H
#define BUTTONS_H

#define MAX_DEVICES 6

// W3C Gamepad API standard button order
// Bit position = W3C button index (trivial conversion: 1 << index)

// Face buttons (right cluster)
#define USBR_BUTTON_B1 (1 << 0)   // 0 - Bottom (Cross/A)
#define USBR_BUTTON_B2 (1 << 1)   // 1 - Right (Circle/B)
#define USBR_BUTTON_B3 (1 << 2)   // 2 - Left (Square/X)
#define USBR_BUTTON_B4 (1 << 3)   // 3 - Top (Triangle/Y)

// Shoulder buttons (front)
#define USBR_BUTTON_L1 (1 << 4)   // 4 - Top left front (L1/LB)
#define USBR_BUTTON_R1 (1 << 5)   // 5 - Top right front (R1/RB)
#define USBR_BUTTON_L2 (1 << 6)   // 6 - Bottom left front (L2/LT)
#define USBR_BUTTON_R2 (1 << 7)   // 7 - Bottom right front (R2/RT)

// Center cluster
#define USBR_BUTTON_S1 (1 << 8)   // 8 - Left center (Select/Back/Share)
#define USBR_BUTTON_S2 (1 << 9)   // 9 - Right center (Start/Options)

// Stick clicks
#define USBR_BUTTON_L3 (1 << 10)  // 10 - Left stick click
#define USBR_BUTTON_R3 (1 << 11)  // 11 - Right stick click

// D-pad (left cluster)
#define USBR_BUTTON_DU (1 << 12)  // 12 - D-pad Up
#define USBR_BUTTON_DD (1 << 13)  // 13 - D-pad Down
#define USBR_BUTTON_DL (1 << 14)  // 14 - D-pad Left
#define USBR_BUTTON_DR (1 << 15)  // 15 - D-pad Right

// Auxiliary (center)
#define USBR_BUTTON_A1 (1 << 16)  // 16 - Center (Guide/Home/PS)

// Extended buttons (beyond W3C standard)
#define USBR_BUTTON_A2 (1 << 17)  // 17 - Touchpad/Capture
#define USBR_BUTTON_A3 (1 << 18)  // 18 - Mute (DualSense)
#define USBR_BUTTON_L4 (1 << 19)  // 19 - Rear left paddle
#define USBR_BUTTON_R4 (1 << 20)  // 20 - Rear right paddle

#endif // BUTTONS_H
