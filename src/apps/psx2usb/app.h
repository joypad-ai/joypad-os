// app.h - PSX2USB App Header
// PS1 / PS2 controller -> USB HID gamepad adapter

#ifndef APP_H
#define APP_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// APP METADATA
// ============================================================================

#define APP_NAME         "PSX2USB"
#define APP_VERSION      "1.0.0"
#define APP_DESCRIPTION  "PS1/PS2 controller to USB HID gamepad adapter"
#define APP_AUTHOR       "RobertDaleSmith"

// ============================================================================
// BOARD
// ============================================================================

#ifndef BOARD
#define BOARD "kb2040"
#endif

// ============================================================================
// INPUT PINS
// ============================================================================
// Defaults mirror PicoGamepadConverter so existing wiring is re-usable.

#ifndef PSX_PIN_CMD
#define PSX_PIN_CMD  19
#endif
#ifndef PSX_PIN_CLK
#define PSX_PIN_CLK  20
#endif
#ifndef PSX_PIN_ATT
#define PSX_PIN_ATT  21
#endif
#ifndef PSX_PIN_DAT
#define PSX_PIN_DAT  22
#endif

// ============================================================================
// OUTPUT
// ============================================================================

#define REQUIRE_USB_DEVICE  1
#define USB_OUTPUT_PORTS    1

// ============================================================================
// ROUTER
// ============================================================================

#define ROUTING_MODE     ROUTING_MODE_SIMPLE
#define MERGE_MODE       MERGE_ALL
#define TRANSFORM_FLAGS  TRANSFORM_NONE

// ============================================================================
// PLAYERS
// ============================================================================

#define PLAYER_SLOT_MODE     PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS     1
#define AUTO_ASSIGN_ON_PRESS false

void app_init(void);
void app_task(void);

#endif // APP_H
