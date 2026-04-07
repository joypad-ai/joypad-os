// app.h - GC2USB App Header
// GameCube controller to USB HID gamepad adapter

#ifndef APP_H
#define APP_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// APP VERSION
// ============================================================================

#define APP_NAME "GC2USB"
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

// GC data pins (joybus single-wire protocol)
// KB2040 single-port: GPIO 29
// HHL GC Pocket 4-port: GPIO 22, 23, 24, 25
#ifndef GC_PIN_DATA
#define GC_PIN_DATA  29
#endif

// Additional port pins for multi-port adapters (set via CMakeLists.txt)
// GC_PIN_DATA_1, GC_PIN_DATA_2, GC_PIN_DATA_3 defined externally when GC_MAX_PORTS > 1

// ============================================================================
// OUTPUT CONFIGURATION
// ============================================================================

#define REQUIRE_USB_DEVICE 1
#ifndef USB_OUTPUT_PORTS
#define USB_OUTPUT_PORTS 1              // Single USB gamepad output (4 for multi-port)
#endif

// ============================================================================
// ROUTER CONFIGURATION
// ============================================================================

// Routing mode: Simple 1:1 (single GC -> single USB port)
#define ROUTING_MODE         ROUTING_MODE_SIMPLE
#define MERGE_MODE           MERGE_ALL

// No input transformations needed
#define TRANSFORM_FLAGS      TRANSFORM_NONE

// ============================================================================
// PLAYER CONFIGURATION
// ============================================================================

#define PLAYER_SLOT_MODE        PLAYER_SLOT_FIXED
#ifndef MAX_PLAYER_SLOTS
#define MAX_PLAYER_SLOTS        1
#endif
#define AUTO_ASSIGN_ON_PRESS    false

// ============================================================================
// APP FUNCTIONS
// ============================================================================

// Initialize the app
void app_init(void);

// Main loop task (called from main.c)
void app_task(void);

#endif // APP_H
