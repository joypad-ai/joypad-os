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

// Translation mode — how the driver maps MouthPad input into JoypadOS.
// Default is RIGHT_STICK (gamepad) so the MouthPad is a usable controller in
// every BT-capable app. The dedicated `mouthpad-relay` build selects PASSTHROUGH
// (plain mouse + keyboard + NUS relay, no gamepad translation) via
// mouthpad_ble_set_mode() in its app_init.
typedef enum {
    MP_MODE_PASSTHROUGH = 0,  // mouse + keyboard passthrough (cursor/clicks/keys)
    MP_MODE_RIGHT_STICK = 1,  // head pointer -> RIGHT stick (hold) + left-click clutch
    MP_MODE_LEFT_STICK  = 2,  // head pointer -> LEFT stick (hold) + left-click clutch
} mp_mode_t;

// Driver instance (registered in bthid_registry.c)
extern const bthid_driver_t mouthpad_ble_driver;

// Register the MouthPad BLE driver with the BTHID layer
void mouthpad_ble_register(void);

// Select the translation mode (call from app_init). Default: MP_MODE_RIGHT_STICK.
void mouthpad_ble_set_mode(mp_mode_t mode);

#endif // MOUTHPAD_BLE_H
