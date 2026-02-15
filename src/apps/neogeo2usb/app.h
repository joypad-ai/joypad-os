// app.h - NEOGEO2USB App Manifest
// NEOGEO controller to USB HID gamepad adapter
//
// This app reads native NEOGEO controllers and outputs USB HID gamepad.
// Supports NEOGEO controllers/sticks 4/6 buttons.

#ifndef APP_NEOGEO2USB_H
#define APP_NEOGEO2USB_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "NEOGEO2USB"
#define APP_VERSION "1.0.0"
#define APP_DESCRIPTION "NEOGEO controller to USB HID gamepad adapter"
#define APP_AUTHOR "herzmx"

// ============================================================================
// CORE DEPENDENCIES
// ============================================================================

// Input drivers - Native Arcade host (NOT USB)
#define REQUIRE_NATIVE_NEOGEO_HOST 1
#define NEOGEO_MAX_CONTROLLERS 1          // Single NEOGEO port

// Output drivers
#define REQUIRE_USB_DEVICE 1
#define USB_OUTPUT_PORTS 1              // Single USB gamepad output

// Services
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// PIN CONFIGURATION
// ============================================================================
// Default GPIO pins for NEOGEO controller
// These can be customized for different boards

#define ARCADE_PIN_DU  10   // Dpad Up
#define ARCADE_PIN_DD  19   // Dpad Down
#define ARCADE_PIN_DR  18   // Dpad Right
#define ARCADE_PIN_DL  20   // Dpad Left

#define ARCADE_PIN_P1  2    // B1/P1
#define ARCADE_PIN_P2  3    // B2/P2
#define ARCADE_PIN_P3  4    // B3/P3
#define ARCADE_PIN_P4  -1
#define ARCADE_PIN_K1  5    // B4/K1
#define ARCADE_PIN_K2  8    // B5/K2
#define ARCADE_PIN_K3  9    // B6/K3
#define ARCADE_PIN_K4  -1

#define ARCADE_PIN_S1  7    // Coin
#define ARCADE_PIN_S2  6    // Start
#define ARCADE_PIN_A1  -1
#define ARCADE_PIN_A2  -1

#define ARCADE_PIN_L3  -1
#define ARCADE_PIN_R3  -1

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_SIMPLE   // Simple 1:1 (NEOGEO → USB)
#define MERGE_MODE MERGE_ALL

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_FIXED  // Fixed slots (no shifting)
#define MAX_PLAYER_SLOTS 1                   // Single player for now
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "ada_kb2040"                  // KB2040 default
#define CPU_OVERCLOCK_KHZ 0                 // No overclock needed
#define UART_DEBUG 1
#define ARCADE_PAD_DEBUG 1

// ============================================================================
// APP INTERFACE
// ============================================================================
void app_init(void);

#endif // APP_NEOGEO2USB_H
