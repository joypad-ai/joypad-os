// app.h - NUON2USB App Header
// Nuon controller to USB HID gamepad adapter

#ifndef APP_H
#define APP_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// APP VERSION
// ============================================================================

#define APP_NAME "NUON2USB"
#define APP_VERSION "1.0.0"

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

// Nuon Polyface pins (bidirectional data + clock)
// KB2040: GPIO2 (data), GPIO3 (clock)
#ifndef NUON_PIN_DATA
#define NUON_PIN_DATA  2
#endif
#define NUON_DATA_PIN  NUON_PIN_DATA  // Alias for display

// ============================================================================
// OUTPUT CONFIGURATION
// ============================================================================

#define REQUIRE_USB_DEVICE 1
#define USB_OUTPUT_PORTS 1              // Single USB gamepad output

// ============================================================================
// ROUTER CONFIGURATION
// ============================================================================

// Routing mode: Simple 1:1 (single Nuon -> single USB port)
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
