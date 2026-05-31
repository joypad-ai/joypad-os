// app.h - GC2DC App Header
// GameCube controller to Dreamcast adapter

#ifndef APP_H
#define APP_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// APP VERSION
// ============================================================================

#define APP_NAME    "GC2DC"
#define APP_VERSION "0.1.0"

// ============================================================================
// BOARD CONFIGURATION
// ============================================================================

#ifndef BOARD
#define BOARD "kb2040"
#endif

// ============================================================================
// INPUT/OUTPUT CONFIGURATION
// ============================================================================

// GameCube data pin (joybus single-wire protocol). KB2040 A3 = GPIO29 to
// match gc2usb_kb2040 / n642dc_kb2040 wiring convention.
#ifndef GC_PIN_DATA
#define GC_PIN_DATA  29
#endif

// Dreamcast Maple Bus pins — must be consecutive (SDCKA on PIN1, SDCKB on
// PIN1+1). Mirrors n642dc_kb2040.
#ifndef DC_MAPLE_PIN1
#define DC_MAPLE_PIN1  2
#endif
#ifndef DC_MAPLE_PIN5
#define DC_MAPLE_PIN5  3
#endif

// ============================================================================
// ROUTER CONFIGURATION
// ============================================================================

#define ROUTING_MODE         ROUTING_MODE_SIMPLE
#define MERGE_MODE           MERGE_BLEND

#define DREAMCAST_OUTPUT_PORTS  1

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

void app_init(void);
void app_task(void);

#endif // APP_H
