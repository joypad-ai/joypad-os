// app.h - N642USB App Header
// N64 controller to USB HID gamepad adapter

#ifndef APP_H
#define APP_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// APP VERSION
// ============================================================================

#define APP_NAME "N642USB"

// ============================================================================
// BOARD CONFIGURATION
// ============================================================================

// Default to KB2040 if not specified
#ifndef BOARD
#define BOARD "kb2040"
#endif

// ============================================================================
// INPUT CONFIGURATION
// ============================================================================

// N64 data pin (joybus single-wire protocol)
// KB2040: A3 = GPIO29
#ifndef N64_PIN_DATA
#define N64_PIN_DATA  29
#endif
#define N64_DATA_PIN  N64_PIN_DATA  // Alias for display

// ============================================================================
// OUTPUT CONFIGURATION
// ============================================================================

#define REQUIRE_USB_DEVICE 1
#define USB_OUTPUT_PORTS 1              // Single USB gamepad output

// ============================================================================
// ROUTER CONFIGURATION
// ============================================================================

// Routing mode: Simple 1:1 (single N64 -> single USB port)
#define ROUTING_MODE         ROUTING_MODE_SIMPLE
#define MERGE_MODE           MERGE_ALL

// No input transformations needed
#define TRANSFORM_FLAGS      TRANSFORM_NONE

// ============================================================================
// PLAYER CONFIGURATION
// ============================================================================

#define PLAYER_SLOT_MODE        PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS        1
#define AUTO_ASSIGN_ON_PRESS    false

// ============================================================================
// APP FUNCTIONS
// ============================================================================

// Initialize the app
void app_init(void);

// Main loop task (called from main.c)
void app_task(void);

#endif // APP_H
