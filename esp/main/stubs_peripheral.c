// stubs_peripheral.c - Weak stubs for peripheral-only ESP32 builds
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
__attribute__((weak)) bool btstack_host_is_scanning(void) { return false; }
__attribute__((weak)) uint8_t btstack_classic_get_connection_count(void) { return 0; }
__attribute__((weak)) bool btstack_host_get_last_connected(uint8_t bd_addr_out[6], char name_out[48])
{
    (void)bd_addr_out; (void)name_out;
    return false;
}
__attribute__((weak)) int btstack_host_list_classic_bonds(uint8_t addrs_out[][6], int max_count)
{
    (void)addrs_out; (void)max_count;
    return 0;
}
__attribute__((weak)) void btstack_host_forget_device(const uint8_t bd_addr[6]) { (void)bd_addr; }
__attribute__((weak)) void btstack_host_suppress_scan(bool suppress) { (void)suppress; }

// flash_factory_reset (used by cdc_commands.c SETTINGS.RESET)
__attribute__((weak)) void flash_factory_reset(void) {}

// neopixel functions (ESP32 uses ws2812_esp32.c which may not have these)
__attribute__((weak)) void neopixel_disable(void) {}
__attribute__((weak)) void neopixel_set_pin(int8_t pin) { (void)pin; }
