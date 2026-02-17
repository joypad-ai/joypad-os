// bthid_gamepad.h - Generic Bluetooth Gamepad Driver
// Handles basic HID gamepads over Bluetooth

#ifndef BTHID_GAMEPAD_H
#define BTHID_GAMEPAD_H

#include "bt/bthid/bthid.h"

// Generic gamepad driver
extern const bthid_driver_t bthid_gamepad_driver;

// Register the generic gamepad driver
void bthid_gamepad_register(void);

// Parse BLE HID descriptor for dynamic report field extraction
void bthid_gamepad_set_descriptor(bthid_device_t* device, const uint8_t* desc, uint16_t desc_len);

#endif // BTHID_GAMEPAD_H
