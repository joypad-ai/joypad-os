// btstack_hal_esp32.c - BTstack embedded HAL for ESP32
//
// Provides hal_time_ms() and IRQ enable/disable stubs required
// by BTstack's embedded platform layer.

#include "platform/platform.h"
#include <stdint.h>

// BTstack embedded HAL: time in milliseconds
// Note: btstack_port_esp32.c also defines hal_time_ms() using esp_log_timestamp().
// This file is only needed if we're NOT linking btstack_port_esp32.c's version,
// or if BTstack's embedded layer needs it separately. Since we link
// btstack_port_esp32.c which defines this, we make it weak here.
__attribute__((weak)) uint32_t hal_time_ms(void)
{
    return platform_time_ms();
}

// IRQ enable/disable - not needed on ESP32 (FreeRTOS handles synchronization)
// These are weak in case BTstack defines them elsewhere
__attribute__((weak)) void hal_cpu_disable_irqs(void)
{
    // No-op on ESP32 (FreeRTOS run loop handles this)
}

__attribute__((weak)) void hal_cpu_enable_irqs(void)
{
    // No-op on ESP32
}

__attribute__((weak)) void hal_cpu_enable_irqs_and_sleep(void)
{
    // No-op on ESP32
}
