// app.h - PS2KBD2USB App Header
// PS/2 keyboard -> USB HID gamepad adapter
//
// Reads a PS/2 keyboard via two GPIO pins (DATA + CLOCK) and submits
// keyboard-as-gamepad events to a USB HID gamepad output.

#ifndef APP_H
#define APP_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// APP METADATA
// ============================================================================

#define APP_NAME         "PS2KBD2USB"
#define APP_VERSION      "1.0.0"
#define APP_DESCRIPTION  "PS/2 keyboard to USB HID gamepad adapter"
#define APP_AUTHOR       "RobertDaleSmith"

// ============================================================================
// BOARD CONFIGURATION
// ============================================================================

#ifndef BOARD
#define BOARD "kb2040"
#endif

// ============================================================================
// INPUT CONFIGURATION
// ============================================================================
// PS/2 keyboard uses two consecutive GPIO pins. DATA must be the lower pin
// number; CLOCK is DATA + 1 (required by the PIO program).
// Default pins follow PicoGamepadConverter convention for wiring compatibility.

#ifndef PS2KBD_PIN_DATA
#define PS2KBD_PIN_DATA  19
#endif

// ============================================================================
// OUTPUT CONFIGURATION
// ============================================================================

#define REQUIRE_USB_DEVICE  1
#define USB_OUTPUT_PORTS    1

// ============================================================================
// ROUTER CONFIGURATION
// ============================================================================

#define ROUTING_MODE        ROUTING_MODE_SIMPLE
#define MERGE_MODE          MERGE_ALL
#define TRANSFORM_FLAGS     TRANSFORM_NONE

// ============================================================================
// PLAYER CONFIGURATION
// ============================================================================

#define PLAYER_SLOT_MODE     PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS     1
#define AUTO_ASSIGN_ON_PRESS false

// ============================================================================
// APP INTERFACE
// ============================================================================

void app_init(void);
void app_task(void);

#endif // APP_H
