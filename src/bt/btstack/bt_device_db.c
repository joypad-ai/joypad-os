// bt_device_db.c - Bluetooth device profile database
//
// Device identification table — order matters (most specific first).

#include "bt_device_db.h"
#include <string.h>

// ============================================================================
// PROFILE CONSTANTS
// ============================================================================

const bt_device_profile_t BT_PROFILE_DEFAULT = {
    .name = "Generic",
    .classic = BT_CLASSIC_HID_HOST,
    .ble = BT_BLE_GATT_HIDS,
    .hid_mode = BT_HID_MODE_REPORT,
    .pin_type = BT_PIN_NONE,
};

const bt_device_profile_t BT_PROFILE_WIIMOTE = {
    .name = "Wiimote",
    .classic = BT_CLASSIC_DIRECT_L2CAP,
    .ble = BT_BLE_NONE,
    .hid_mode = BT_HID_MODE_REPORT,
    .pin_type = BT_PIN_BDADDR,
    .classic_only = true,
    .default_vid = 0x057E,
    .default_pid = 0x0306,
};

const bt_device_profile_t BT_PROFILE_WII_U_PRO = {
    .name = "Wii U Pro",
    .classic = BT_CLASSIC_DIRECT_L2CAP,
    .ble = BT_BLE_NONE,
    .hid_mode = BT_HID_MODE_REPORT,
    .pin_type = BT_PIN_BDADDR,
    .classic_only = true,
    .default_vid = 0x057E,
    .default_pid = 0x0330,
};

const bt_device_profile_t BT_PROFILE_XBOX = {
    .name = "Xbox",
    .classic = BT_CLASSIC_HID_HOST,
    .ble = BT_BLE_DIRECT_ATT,
    .hid_mode = BT_HID_MODE_FALLBACK,
    .pin_type = BT_PIN_NONE,
};

const bt_device_profile_t BT_PROFILE_DS3 = {
    .name = "DS3",
    .classic = BT_CLASSIC_HID_HOST,
    .ble = BT_BLE_NONE,
    .hid_mode = BT_HID_MODE_REPORT,
    .pin_type = BT_PIN_NONE,
    .classic_only = true,
};

const bt_device_profile_t BT_PROFILE_SONY = {
    .name = "Sony",
    .classic = BT_CLASSIC_HID_HOST,
    .ble = BT_BLE_NONE,
    .hid_mode = BT_HID_MODE_REPORT,
    .pin_type = BT_PIN_NONE,
    .classic_only = true,
};

const bt_device_profile_t BT_PROFILE_SWITCH = {
    .name = "Switch",
    .classic = BT_CLASSIC_HID_HOST,
    .ble = BT_BLE_GATT_HIDS,
    .hid_mode = BT_HID_MODE_REPORT,
    .pin_type = BT_PIN_NONE,
};

const bt_device_profile_t BT_PROFILE_SWITCH2 = {
    .name = "Switch 2",
    .classic = BT_CLASSIC_HID_HOST,
    .ble = BT_BLE_CUSTOM,
    .hid_mode = BT_HID_MODE_REPORT,
    .pin_type = BT_PIN_NONE,
};

const bt_device_profile_t BT_PROFILE_STADIA = {
    .name = "Stadia",
    .classic = BT_CLASSIC_HID_HOST,
    .ble = BT_BLE_GATT_HIDS,
    .hid_mode = BT_HID_MODE_REPORT,
    .pin_type = BT_PIN_NONE,
};

// ============================================================================
// NAME-BASED DEVICE TABLE
// ============================================================================

typedef struct {
    const char* substring;             // Name substring to match (strstr)
    const bt_device_profile_t* profile;
} bt_device_name_entry_t;

// Order matters — most specific first (e.g., "-UC" before "RVL-CNT")
static const bt_device_name_entry_t name_table[] = {
    { "Nintendo RVL-CNT-01-UC", &BT_PROFILE_WII_U_PRO },
    { "Nintendo RVL-CNT-01",    &BT_PROFILE_WIIMOTE },
    { "Xbox",                   &BT_PROFILE_XBOX },
    { "PLAYSTATION",            &BT_PROFILE_DS3 },
    { "DUALSHOCK",              &BT_PROFILE_DS3 },
    { "Wireless Controller",    &BT_PROFILE_SONY },
    { "DualSense",              &BT_PROFILE_SONY },
    { "Pro Controller",         &BT_PROFILE_SWITCH },
    { "Joy-Con",                &BT_PROFILE_SWITCH },
    { "Stadia",                 &BT_PROFILE_STADIA },
};

#define NAME_TABLE_SIZE (sizeof(name_table) / sizeof(name_table[0]))

// ============================================================================
// LOOKUP FUNCTIONS
// ============================================================================

const bt_device_profile_t* bt_device_lookup(const char* name, uint16_t company_id) {
    // Company ID match (Switch 2 — no name in advertising)
    if (company_id == 0x0553) {
        return &BT_PROFILE_SWITCH2;
    }

    // Name-based match
    if (name && name[0]) {
        for (unsigned i = 0; i < NAME_TABLE_SIZE; i++) {
            if (strstr(name, name_table[i].substring) != NULL) {
                return name_table[i].profile;
            }
        }
    }

    return &BT_PROFILE_DEFAULT;
}

const bt_device_profile_t* bt_device_lookup_by_name(const char* name) {
    return bt_device_lookup(name, 0);
}

uint16_t bt_device_wiimote_pid_from_name(const char* name) {
    if (!name || !name[0]) return 0;
    if (strstr(name, "-UC") != NULL) return 0x0330;           // Wii U Pro
    if (strstr(name, "RVL-CNT-01") != NULL) return 0x0306;    // Wiimote
    return 0;
}
