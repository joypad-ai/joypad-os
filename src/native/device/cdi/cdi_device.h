// cdi_device.h - Philips CD-i Output Device
//
// Emulates a CD-i controller (MANEUVER type) connected to a Philips CD-i
// console via inverted 1200 baud 7N2 serial over Mini-DIN connector.

#ifndef CDI_DEVICE_H
#define CDI_DEVICE_H

#include <stdint.h>
#include <stdbool.h>

// CD-i device type IDs (sent on connect)
#define CDI_DEVICE_RELATIVE  0xCD  // Mouse, trackball
#define CDI_DEVICE_MANEUVER  0xCA  // Joystick, joypad
#define CDI_DEVICE_ABSOLUTE  0xD4  // Graphics tablet
#define CDI_DEVICE_SCREEN    0xD3  // Touchscreen
#define CDI_DEVICE_KEYBOARD  0xCB  // Keyboard
#define CDI_DEVICE_EXT_KEYB  0xD8  // Extended keyboard

// Pin defaults (overridable per-app)
#ifndef CDI_TX_PIN
#define CDI_TX_PIN 2    // Serial TX to CD-i console (inverted)
#endif

#ifndef CDI_RTS_PIN
#define CDI_RTS_PIN 3   // RTS from CD-i console (HIGH = ready)
#endif

// Function declarations
void cdi_init(void);
void cdi_task(void);

#endif // CDI_DEVICE_H
