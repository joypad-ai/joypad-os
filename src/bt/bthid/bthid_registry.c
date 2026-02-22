// bthid_registry.c - BTHID Driver Registration
// Registers all Bluetooth HID device drivers

#include "bthid_registry.h"
#include "bthid.h"

// Include all BT HID drivers
#include "devices/generic/bthid_gamepad.h"
#include "devices/vendors/sony/ds3_bt.h"
#include "devices/vendors/sony/ds4_bt.h"
#include "devices/vendors/sony/ds5_bt.h"
#include "devices/vendors/nintendo/switch_pro_bt.h"
#include "devices/vendors/nintendo/switch2_ble.h"
#include "devices/vendors/nintendo/wii_u_pro_bt.h"
#include "devices/vendors/nintendo/wiimote_bt.h"
// xbox_bt.h and xbox_ble.h no longer registered — generic driver handles all Xbox
#include "devices/vendors/google/stadia_bt.h"

void bthid_registry_init(void)
{
    // Initialize BTHID layer
    bthid_init();

    // Register vendor-specific drivers first (higher priority)
    // Order matters - first match wins

    // Sony controllers
    ds3_bt_register();
    ds4_bt_register();
    ds5_bt_register();

    // Nintendo controllers
    switch_pro_bt_register();
    switch2_ble_register();  // Switch 2 BLE controllers (Pro2, Joy-Con 2, GC NSO)
    wii_u_pro_bt_register();  // Must be before wiimote (Wii U Pro has "-UC" suffix)
    wiimote_bt_register();

    // Microsoft controllers — handled by generic gamepad driver via HID descriptor
    // parsing (like BlueRetro). Covers all Xbox variants without layout assumptions.

    // Google controllers
    stadia_bt_register();

    // Generic gamepad driver (fallback, lowest priority)
    bthid_gamepad_register();
}
