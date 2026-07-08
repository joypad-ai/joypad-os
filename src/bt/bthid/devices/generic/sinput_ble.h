// sinput_ble.h - BLE SInput controller input driver
// SPDX-License-Identifier: Apache-2.0
//
// Reads a JoypadOS SInput controller (controller_btusb on nRF/ESP32/Pico W)
// over BLE HID (HOGP) and submits it to the router. BLE counterpart to the USB
// sinput_host.c driver.

#ifndef SINPUT_BLE_H
#define SINPUT_BLE_H

// Register the BLE SInput driver with the BTHID registry.
void sinput_ble_register(void);

#endif // SINPUT_BLE_H
