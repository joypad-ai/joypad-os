// n64_host.h - Native N64 Controller Host Driver
//
// Polls native N64 controllers via the joybus-pio library and submits
// input events to the router.

#ifndef N64_HOST_H
#define N64_HOST_H

#include <stdint.h>
#include <stdbool.h>
#include "core/input_interface.h"

// ============================================================================
// CONFIGURATION
// ============================================================================

// Default GPIO pin for N64 controller data line
#ifndef N64_PIN_DATA
#define N64_PIN_DATA  4   // Data I/O (directly to controller)
#endif

// Default polling rate (Hz) - N64 console polls at 60Hz
#ifndef N64_POLLING_RATE
#define N64_POLLING_RATE  60
#endif

// Maximum number of N64 controllers (1 for now, future: multitap)
#define N64_MAX_PORTS 1

// ============================================================================
// PUBLIC API
// ============================================================================

// Initialize N64 host driver with default pin
void n64_host_init(void);

// Initialize with custom pin configuration
void n64_host_init_pin(uint8_t data_pin);

// Poll N64 controllers and submit events to router
// Call this regularly from main loop (typically from app's task function)
void n64_host_task(void);

// Check if N64 controller is connected
bool n64_host_is_connected(void);

// Get detected device type for a port
// Returns: -1=none, 0=standard controller, 1=with rumble pak, 2=with controller pak
int8_t n64_host_get_device_type(uint8_t port);

// Set rumble state for a port (non-blocking, defers actual send)
void n64_host_set_rumble(uint8_t port, bool enabled);

// Flush pending rumble commands (blocking joybus write)
// Call AFTER time-critical tasks like Dreamcast Maple response
void n64_host_flush_rumble(void);

// N64 input interface (implements InputInterface pattern for app declaration)
extern const InputInterface n64_input_interface;

#endif // N64_HOST_H
