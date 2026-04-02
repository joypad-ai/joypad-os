# Platform HAL

The platform hardware abstraction layer provides a thin interface over platform-specific APIs for time, identity, and reboot operations. This allows core services and shared code to work identically across RP2040, ESP32-S3, and nRF52840 without conditional compilation.

Source: `src/platform/platform.h`

## API

```c
// Time
uint32_t platform_time_ms(void);     // Milliseconds since boot
uint32_t platform_time_us(void);     // Microseconds since boot (may wrap at 32 bits)
void platform_sleep_ms(uint32_t ms); // Sleep (may yield to scheduler on RTOS)
void platform_sleep_us(uint32_t us); // Busy-wait (does not yield)

// Identity
void platform_get_serial(char* buf, size_t len);      // Hex serial string
void platform_get_unique_id(uint8_t* buf, size_t len); // Raw unique ID bytes (up to 8)

// Reboot
void platform_reboot(void);             // Normal reboot
void platform_reboot_bootloader(void);  // Reboot into UF2/DFU bootloader mode
```

## Implementations

| Platform | Source File | Notes |
|----------|-----------|-------|
| RP2040 / RP2350 | `src/platform/rp2040/platform_rp2040.c` | Uses pico-sdk (`to_ms_since_boot`, `get_absolute_time`, `pico_get_unique_board_id`, `watchdog_reboot`) |
| ESP32-S3 | `src/platform/esp32/platform_esp32.c` | Uses ESP-IDF (`esp_timer_get_time`, `esp_efuse_mac_get_default`, `esp_restart`) |
| nRF52840 | `nrf/src/` (inline or Zephyr wrappers) | Uses Zephyr (`k_uptime_get`, `k_msleep`, `sys_reboot`) |

## Why It Exists

Without the HAL, shared code would need `#ifdef PICO_PLATFORM` / `#ifdef PLATFORM_ESP32` / `#ifdef PLATFORM_NRF` around every time or reboot call. The HAL eliminates this: core services, the router, storage, profiles, and player management all call `platform_time_ms()` instead of platform-specific APIs.

## `__not_in_flash_func` Compatibility

RP2040 uses `__not_in_flash_func(name)` to place timing-critical functions in RAM (avoiding flash access latency). On ESP32 and nRF, this macro is defined as a no-op:

```c
#if defined(PLATFORM_ESP32) || defined(PLATFORM_NRF)
  #define __not_in_flash_func(func) func
#endif
```

This allows shared code to use `__not_in_flash_func` without breaking non-RP2040 builds.

## Sleep Semantics

- `platform_sleep_ms()` on RP2040 calls `sleep_ms()` (busy-wait). On ESP32/nRF with RTOS, it calls the scheduler-aware sleep (`vTaskDelay` / `k_msleep`) which yields CPU time to other tasks.
- `platform_sleep_us()` is always a busy-wait on all platforms. Use sparingly -- it blocks the calling thread/core entirely.

## See Also

- [Storage](storage.md) -- Uses `platform_time_ms()` for write debouncing
- [LEDs](leds.md) -- Uses `platform_time_ms()` for blink timing
