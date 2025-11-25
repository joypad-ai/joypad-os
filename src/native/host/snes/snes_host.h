// snes_host.h - Native SNES/NES Controller Host Driver
//
// Polls native SNES/NES controllers via the SNESpad library and submits
// input events to the router. Supports SNES controllers, NES controllers,
// SNES mouse, and Xband keyboard.
//
// Used by Super3D0USB to read native SNES controllers for 3DO output.

#ifndef SNES_HOST_H
#define SNES_HOST_H

#include <stdint.h>
#include <stdbool.h>

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

// Maximum number of SNES ports (for multitap support in future)
#define SNES_MAX_PORTS 1

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

// Check if any SNES controller is connected
bool snes_host_is_connected(void);

#endif // SNES_HOST_H
