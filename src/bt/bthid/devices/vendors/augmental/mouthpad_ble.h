// mouthpad_ble.h - Augmental MouthPad BLE driver
//
// The Augmental MouthPad is a BLE mouth-controlled input device. Over
// HID-over-GATT (HOGP) it presents as a mouse + keyboard + consumer-control
// device across 4 report IDs (see mouthpad_ble.c for the report map). This
// driver turns those HOGP reports into JoypadOS input_event_t values so the
// MouthPad flows through the router/profile pipeline like any other input —
// remappable and combinable with other controllers.
//
// The custom Nordic UART Service (NUS) stream the MouthPad also exposes (for
// the desktop utility) is handled separately by the NUS GATT client + CDC
// relay, not by this HID driver.

#ifndef MOUTHPAD_BLE_H
#define MOUTHPAD_BLE_H

#include "bt/bthid/bthid.h"

// Driver instance (registered in bthid_registry.c)
extern const bthid_driver_t mouthpad_ble_driver;

// Register the MouthPad BLE driver with the BTHID layer
void mouthpad_ble_register(void);

#endif // MOUTHPAD_BLE_H
