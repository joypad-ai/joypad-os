// bt_device_db.h - Bluetooth device profile database
//
// Centralizes BT device identification so event handlers route on
// a stored profile pointer instead of repeated strstr() checks.

#ifndef BT_DEVICE_DB_H
#define BT_DEVICE_DB_H

#include <stdint.h>
#include <stdbool.h>

// Classic BT connection strategy
typedef enum {
    BT_CLASSIC_HID_HOST,     // Standard HID Host (SDP + L2CAP via BTstack)
    BT_CLASSIC_DIRECT_L2CAP, // Direct L2CAP channels (Wiimote/Wii U Pro)
} bt_classic_strategy_t;

// BLE connection strategy
typedef enum {
    BT_BLE_NONE,       // No BLE support (classic-only device)
    BT_BLE_GATT_HIDS,  // Standard GATT HID Service discovery
    BT_BLE_DIRECT_ATT, // Direct ATT notification (Xbox - known handles)
    BT_BLE_CUSTOM,     // Custom protocol (Switch 2)
} bt_ble_strategy_t;

// PIN code type for legacy pairing
typedef enum {
    BT_PIN_NONE,   // No PIN (uses SSP)
    BT_PIN_BDADDR, // PIN = host BD_ADDR reversed (Wiimote/Wii U Pro)
} bt_pin_type_t;

// HID protocol mode for hid_host_connect()
typedef enum {
    BT_HID_MODE_REPORT,   // HID_PROTOCOL_MODE_REPORT
    BT_HID_MODE_FALLBACK, // HID_PROTOCOL_MODE_REPORT_WITH_FALLBACK_TO_BOOT
} bt_hid_mode_t;

// Device profile — describes how to connect to a BT device type
typedef struct {
    const char* name;              // Human-readable profile name (for logging)
    bt_classic_strategy_t classic;
    bt_ble_strategy_t ble;
    bt_hid_mode_t hid_mode;
    bt_pin_type_t pin_type;
    bool classic_only;             // True = skip BLE advertising (connect via classic only)
    uint16_t default_vid;          // Default VID (0 = use SDP/advertising)
    uint16_t default_pid;          // Default PID (0 = use SDP/advertising)
} bt_device_profile_t;

// Profile constants
extern const bt_device_profile_t BT_PROFILE_DEFAULT;
extern const bt_device_profile_t BT_PROFILE_WIIMOTE;
extern const bt_device_profile_t BT_PROFILE_WII_U_PRO;
extern const bt_device_profile_t BT_PROFILE_XBOX;
extern const bt_device_profile_t BT_PROFILE_DS3;
extern const bt_device_profile_t BT_PROFILE_SONY;
extern const bt_device_profile_t BT_PROFILE_SWITCH;
extern const bt_device_profile_t BT_PROFILE_SWITCH2;
extern const bt_device_profile_t BT_PROFILE_STADIA;

// Lookup device profile by name and/or company ID.
// Returns matching profile, or &BT_PROFILE_DEFAULT if no match.
const bt_device_profile_t* bt_device_lookup(const char* name, uint16_t company_id);

// Lookup device profile by name only.
// Returns matching profile, or &BT_PROFILE_DEFAULT if no match.
const bt_device_profile_t* bt_device_lookup_by_name(const char* name);

// Get Wiimote PID from name suffix.
// "Nintendo RVL-CNT-01-UC" → 0x0330 (Wii U Pro)
// "Nintendo RVL-CNT-01"    → 0x0306 (Wiimote)
// Returns 0 if name doesn't match.
uint16_t bt_device_wiimote_pid_from_name(const char* name);

#endif // BT_DEVICE_DB_H
