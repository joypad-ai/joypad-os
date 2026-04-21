// ps2kbd_host.h - Native PS/2 Keyboard Host Driver
//
// Reads a PS/2 keyboard via two GPIO pins (DATA + CLOCK) using a PIO state machine
// to capture 11-bit frames. Translates PS/2 Set 2 scan codes to USB HID usage IDs,
// maintains a 6-key-rollover pressed-key set, and submits input events via the router.

#ifndef PS2KBD_HOST_H
#define PS2KBD_HOST_H

#include <stdint.h>
#include <stdbool.h>
#include "core/input_interface.h"

// ============================================================================
// CONFIGURATION
// ============================================================================

// Default GPIO pins (consecutive; data must be the lower pin number).
// Override by calling ps2kbd_host_init_pins() before ps2kbd_host_init().
#ifndef PS2KBD_PIN_DATA
#define PS2KBD_PIN_DATA  19
#endif

#ifndef PS2KBD_PIN_CLOCK
#define PS2KBD_PIN_CLOCK (PS2KBD_PIN_DATA + 1)
#endif

// ============================================================================
// PUBLIC API
// ============================================================================

// Initialise with default pins.
void ps2kbd_host_init(void);

// Initialise with a custom DATA pin. CLOCK pin is always DATA + 1.
void ps2kbd_host_init_pin(uint8_t pin_data);

// Drain pending PIO frames, decode scan codes, and submit router events when
// the pressed-key set changes. Call from the app task loop.
void ps2kbd_host_task(void);

// Returns true once the keyboard has completed its power-on self-test (AA byte seen).
bool ps2kbd_host_is_connected(void);

// Exported InputInterface for app declaration.
extern const InputInterface ps2kbd_input_interface;

#endif // PS2KBD_HOST_H
