// app.h - SNES2USB App Manifest
// SNES/NES controller to USB HID gamepad adapter
//
// This app reads native SNES/NES controllers and outputs USB HID gamepad.
// Supports SNES controller, NES controller, SNES mouse, and Xband keyboard.

#ifndef APP_SNES2USB_H
#define APP_SNES2USB_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "SNES2USB"
#define APP_VERSION "1.0.0"
#define APP_DESCRIPTION "SNES/NES controller to USB HID gamepad adapter"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES
// ============================================================================

// Input drivers - Native SNES host (NOT USB)
#define REQUIRE_NATIVE_SNES_HOST 1
#define SNES_MAX_CONTROLLERS 1          // Single SNES port for now

// Output drivers
#define REQUIRE_USB_DEVICE 1
#define USB_OUTPUT_PORTS 1              // Single USB gamepad output

// Services
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// PIN CONFIGURATION
// ============================================================================
// SNES controller pins (directly from controller port)
// These can be customized for different boards
#define SNES_PIN_CLOCK  5   // CLK - output to controller
#define SNES_PIN_LATCH  6   // LATCH - output to controller
#define SNES_PIN_DATA0  7   // DATA - input from controller
#define SNES_PIN_DATA1  8   // DATA1 - input (for multitap/keyboard)
#define SNES_PIN_IOBIT  9   // IOBIT - output (for mouse/keyboard)

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_SIMPLE   // Simple 1:1 (SNES → USB)
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

// ============================================================================
// APP FEATURES
// ============================================================================
#define FEATURE_MOUSE_SUPPORT 1             // SNES mouse support

// ============================================================================
// APP INTERFACE
// ============================================================================
void app_init(void);

#endif // APP_SNES2USB_H
