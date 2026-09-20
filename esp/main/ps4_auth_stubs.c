// PS4 auth flash + event-log stubs for ESP32-S3.
//
// The shared cdc_commands.c always registers the PS4AUTH.* CDC handlers,
// in the RP2040 build (they use pico-sdk flash APIs) and are not built here. PS4
// controller emulation is RP2040-only anyway (ESP32-S3 is BLE→USB HID), so provide
// no-op stubs: PS4AUTH.STATUS reports "not installed" and the config UI hides the
// PS4 Auth page.
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "core/services/storage/ps4_auth_flash.h"

// save/erase report false: there is no backing store here, so claiming success
// would be the exact defect #228 fixes on the RP2040 side.
bool ps4_auth_flash_load(ps4_auth_data_t *out) { (void)out; return false; }
bool ps4_auth_flash_save(const ps4_auth_data_t *data) { (void)data; return false; }
bool ps4_auth_flash_erase(void) { return false; }
bool ps4_auth_flash_is_valid(const ps4_auth_data_t *data) { (void)data; return false; }

