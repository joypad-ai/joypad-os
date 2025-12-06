// buttons.h
// USBRetro canonical button definitions (GP2040-CE compatible)
#ifndef BUTTONS_H
#define BUTTONS_H

#define MAX_DEVICES 6

// USBRetro Buttons -> Xinput/Switch/PlayStation
#define USBR_BUTTON_B1 0x00020 // A/B/Cross
#define USBR_BUTTON_B2 0x00010 // B/A/Circle
#define USBR_BUTTON_B3 0x02000 // X/Y/Square
#define USBR_BUTTON_B4 0x01000 // Y/X/Triangle
#define USBR_BUTTON_L1 0x04000 // LB/L/L1
#define USBR_BUTTON_R1 0x08000 // RB/R/R1
#define USBR_BUTTON_L2 0x00100 // LT/ZL/L2
#define USBR_BUTTON_R2 0x00200 // RT/ZR/R2
#define USBR_BUTTON_S1 0x00040 // Back/Minus/Share/Select
#define USBR_BUTTON_S2 0x00080 // Start/Plus/Options
#define USBR_BUTTON_L3 0x10000 // LS/L3
#define USBR_BUTTON_R3 0x20000 // RS/R3
#define USBR_BUTTON_A1 0x00400  // Guide/Home/PS
#define USBR_BUTTON_A2 0x00800  // Capture/Touchpad
#define USBR_BUTTON_A3 0x100000 // Mute (DS5)
#define USBR_BUTTON_L4 0x40000  // Rear Left/Paddle/Touchpad Left
#define USBR_BUTTON_R4 0x80000  // Rear Right/Paddle/Touchpad Right

// USBRetro Dpad Directions
#define USBR_BUTTON_DU 0x00001 // Dpad Up
#define USBR_BUTTON_DD 0x00004 // Dpad Down
#define USBR_BUTTON_DL 0x00008 // Dpad Left
#define USBR_BUTTON_DR 0x00002 // Dpad Right

#endif // BUTTONS_H
