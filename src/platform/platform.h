// platform.h - Platform Hardware Abstraction Layer
//
// Thin abstraction over platform-specific APIs (time, identity, reboot).
// Implementations: rp2040/platform_rp2040.c, esp32/platform_esp32.c

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <stddef.h>

// RP2040 __not_in_flash_func places functions in RAM for timing.
// On ESP32 this is not needed — define as no-op if not already defined.
#ifdef PLATFORM_ESP32
  #ifndef __not_in_flash_func
    #define __not_in_flash_func(func) func
  #endif
#endif

// Get current time in milliseconds since boot
uint32_t platform_time_ms(void);

// Get current time in microseconds since boot (may wrap at 32 bits)
uint32_t platform_time_us(void);

// Sleep for specified milliseconds
void platform_sleep_ms(uint32_t ms);

// Get unique board serial string (hex)
void platform_get_serial(char* buf, size_t len);

// Get raw unique board ID bytes (up to 8 bytes)
void platform_get_unique_id(uint8_t* buf, size_t len);

// Reboot the device
void platform_reboot(void);

// Reboot into bootloader (UF2/DFU mode)
void platform_reboot_bootloader(void);

#endif // PLATFORM_H
