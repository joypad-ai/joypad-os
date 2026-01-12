// app.h - N642DC App Header
// N64 controller to Dreamcast adapter

#ifndef APP_H
#define APP_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// APP VERSION
// ============================================================================

#define APP_NAME "N642DC"
#define APP_VERSION "0.1.0"

// ============================================================================
// BOARD CONFIGURATION
// ============================================================================

// Default to KB2040 if not specified
#ifndef BOARD
#define BOARD "kb2040"
#endif

// ============================================================================
// INPUT/OUTPUT CONFIGURATION
// ============================================================================

// N64 data pin (joybus single-wire protocol)
// KB2040 A3 = GPIO29
#define N64_PIN_DATA  29
#define N64_DATA_PIN  N64_PIN_DATA  // Alias for display

// Dreamcast Maple Bus pins (must be consecutive)
#define DC_MAPLE_PIN1  2  // Data line A
#define DC_MAPLE_PIN5  3  // Data line B (PIN1 + 1)

// ============================================================================
// ROUTER CONFIGURATION
// ============================================================================

// Routing mode: Simple 1:1 (single N64 -> single DC port)
#define ROUTING_MODE         ROUTING_MODE_SIMPLE
#define MERGE_MODE           MERGE_BLEND

// Dreamcast supports 4 ports, but N642DC uses port 0 only
#define DREAMCAST_OUTPUT_PORTS  1

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
