// platform.h - Platform Hardware Abstraction Layer
//
// Thin abstraction over platform-specific APIs (time, identity, reboot).
// Implementations: rp2040/platform_rp2040.c, esp32/platform_esp32.c, nrf/platform_nrf.c

#ifndef PLATFORM_H
#define PLATFORM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// RP2040 __not_in_flash_func places functions in RAM for timing.
// On non-RP2040 platforms this is not needed — define as no-op.
#if defined(PLATFORM_ESP32) || defined(PLATFORM_NRF) || defined(PLATFORM_CH32)
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

// Busy-wait for specified microseconds (does not yield to scheduler)
void platform_sleep_us(uint32_t us);

// Get unique board serial string (hex)
void platform_get_serial(char* buf, size_t len);

// Get raw unique board ID bytes (up to 8 bytes)
void platform_get_unique_id(uint8_t* buf, size_t len);

// Reboot the device
void platform_reboot(void);

// Reboot into bootloader (UF2/DFU mode)
void platform_reboot_bootloader(void);

// True if the device is currently powered from USB (VBUS present).
bool platform_usb_powered(void);

// Raw, platform-specific reset/wake reason latched at the last boot (e.g. nRF
// POWER->RESETREAS). 0 if unknown. Read once and cached; used to diagnose
// why the SoC woke (System OFF GPIO wake, VBUS, watchdog, soft reset, ...).
uint32_t platform_last_reset_reason(void);

// Battery voltage in millivolts, or -1 if this board has no battery-sense
// circuit / it isn't supported. Board-specific (e.g. XIAO nRF52840: AIN7 via
// the P0.14-gated divider).
int platform_battery_millivolts(void);

// Enter the deepest sleep state the SoC supports (e.g. nRF System OFF), waking
// when wake_gpio (raw chip GPIO number) reaches its pressed level. The pin is
// held at its idle level with the appropriate internal pull so it only wakes
// on a press: wake_active_high → pull-down + sense-high (idle low, press high);
// else → pull-up + sense-low (idle high, press low). Wake performs a full
// reboot. Returns false WITHOUT sleeping if the platform can't/shouldn't sleep
// right now (e.g. USB-powered, or unsupported); on success it does not return.
bool platform_deep_sleep(uint8_t wake_gpio, bool wake_active_high);

#endif // PLATFORM_H
