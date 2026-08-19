// PS4 auth flash stubs for the CH32V307 (usb2usb).
//
// The shared cdc_commands.c registers the PS4AUTH.* CDC handlers unconditionally
// and calls ps4_auth_flash_save/load/erase outside every #ifdef, but the real
// implementation (core/services/storage/ps4_auth_flash.c) is pico-sdk flash code
// listed only in src/CMakeLists.txt. esp/ and nrf/ each carry a copy of this file
// for that reason; wch/ did not, so the port had three unresolved references.
// PS4 controller emulation is RP2040-only regardless (ENABLE_PS4_LOCAL_AUTH is
// defined only by src/CMakeLists.txt, so ps4_mode.c's RSA path is compiled out
// here), which is why no-ops are the right answer rather than a CH32 flash driver.
//
// Picked up automatically by the build: wch/Makefile does
// `EXAMPLE_SOURCE += $(wildcard src/*.c)`, so no Makefile change is needed.
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
