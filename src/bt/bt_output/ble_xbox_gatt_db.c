// ble_xbox_gatt_db.c - Xbox GATT database wrapper
// Wraps the generated ble_xbox.h (from ble_xbox.gatt) to expose the ATT
// database under a different symbol name, avoiding conflict with the
// standard composite GATT's 'profile_data'.

#include <stdint.h>
#include <stddef.h>

// Rename profile_data before including the generated header
#define profile_data xbox_profile_data_storage
#include "ble_xbox.h"
#undef profile_data

const uint8_t *ble_xbox_profile_data = xbox_profile_data_storage;
