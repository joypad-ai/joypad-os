// stubs_peripheral.c - Weak stubs for peripheral-only nRF52840 builds
//
// When building in peripheral mode (universal), btstack_host.c and
// bthid are not linked. Shared code (router.c, cdc_commands.c, sinput_mode.c)
// references symbols from those modules. These weak stubs satisfy the linker.

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// bthid_get_device (used by router.c and sinput_mode.c)
typedef struct { int dummy; } bthid_device_t;
__attribute__((weak)) bthid_device_t* bthid_get_device(uint8_t conn_index)
{
    (void)conn_index;
    return NULL;
}

// wiimote functions (used by cdc_commands.c)
__attribute__((weak)) uint8_t wiimote_get_orient_mode(void) { return 0; }
__attribute__((weak)) const char* wiimote_get_orient_mode_name(uint8_t mode) { (void)mode; return "N/A"; }
__attribute__((weak)) void wiimote_set_orient_mode(uint8_t mode) { (void)mode; }

// btstack_host functions (used by cdc_commands.c)
__attribute__((weak)) void btstack_host_delete_all_bonds(void) {}
__attribute__((weak)) bool btstack_host_is_initialized(void) { return false; }
__attribute__((weak)) int btstack_host_list_classic_bonds(uint8_t addrs_out[][6], int max_count)
{
    (void)addrs_out; (void)max_count;
    return 0;
}
