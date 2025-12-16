// btstack_hal.c - HAL implementations for BTstack embedded run loop
// Provides timing and interrupt control functions for RP2040

#include "btstack_config.h"
#include "pico/time.h"
#include "hardware/sync.h"

// Return current time in milliseconds
uint32_t hal_time_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

// Disable interrupts
void hal_cpu_disable_irqs(void)
{
    // On RP2040, we just save the interrupt state but don't fully disable
    // This is called from the run loop to check for pending work
}

// Enable interrupts
void hal_cpu_enable_irqs(void)
{
    // Re-enable interrupts
}

// Enable interrupts and sleep until next interrupt
void hal_cpu_enable_irqs_and_sleep(void)
{
    // Don't actually sleep - we're polling in our main loop
    // __wfe() would block forever on console targets without USB device events
}
