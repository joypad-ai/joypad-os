// snes_host.h - Native SNES/NES Controller Host Driver
//
// Polls native SNES/NES controllers via the SNESpad library and submits
// input events to the router. Supports SNES controllers, NES controllers,
// SNES mouse, and Xband keyboard.

#ifndef SNES_HOST_H
#define SNES_HOST_H

#include <stdint.h>
#include <stdbool.h>
#include "core/input_interface.h"

// ============================================================================
// CONFIGURATION
// ============================================================================

// Default GPIO pins for SNES controller (can be overridden by app)
#ifndef SNES_PIN_CLOCK
#define SNES_PIN_CLOCK  2   // Clock output
#endif

#ifndef SNES_PIN_LATCH
#define SNES_PIN_LATCH  3   // Latch output
#endif

#ifndef SNES_PIN_DATA0
#define SNES_PIN_DATA0  4   // Data input (directly from controller)
#endif

#ifndef SNES_PIN_DATA1
#define SNES_PIN_DATA1  5   // Data1 input (for multitap/keyboard)
#endif

#ifndef SNES_PIN_IOBIT
#define SNES_PIN_IOBIT  6   // I/O bit (for mouse/keyboard)
#endif

// Maximum number of SNES ports
// Port 0: DATA0 directly (single controller or multitap port 1)
// Ports 1-3: Reserved for future multitap support
// Note: Actual multitap support requires SNESpad library extension
#define SNES_MAX_PORTS 4

// ============================================================================
// PUBLIC API
// ============================================================================

// Initialize SNES host driver
// Sets up GPIO pins and initializes SNESpad library
void snes_host_init(void);

// Initialize with custom pin configuration
void snes_host_init_pins(uint8_t clock, uint8_t latch, uint8_t data0,
                         uint8_t data1, uint8_t iobit);

// Poll SNES controllers and submit events to router
// Call this regularly from main loop (typically from app's task function)
void snes_host_task(void);

// Get detected device type for a port
// Returns: -1=none, 0=SNES controller, 1=NES, 2=mouse, 3=keyboard
int8_t snes_host_get_device_type(uint8_t port);

// Set rumble motor intensities for a SNES rumble controller (LRG protocol)
// Values are 0-255, scaled internally to 0-15. Safe on non-rumble controllers.
void snes_host_set_rumble(uint8_t port, uint8_t left, uint8_t right);

// Check if any SNES controller is connected
bool snes_host_is_connected(void);

// SNES input interface (implements InputInterface pattern for app declaration)
extern const InputInterface snes_input_interface;

#endif // SNES_HOST_H
