// app.h - NES2USB App Manifest
// NES controller to USB HID gamepad adapter
//
// This app reads native NES controllers and outputs USB HID gamepad.
// Supports NES controller.

#ifndef APP_NES2USB_H
#define APP_NES2USB_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "NES2USB"
#define APP_VERSION "1.0.0"
#define APP_DESCRIPTION "NES controller to USB HID gamepad adapter"
#define APP_AUTHOR "JamesIanMurchison"

// ============================================================================
// CORE DEPENDENCIES
// ============================================================================

// Output drivers
#define REQUIRE_USB_DEVICE 1
#define USB_OUTPUT_PORTS 1              // Single USB gamepad output

// Services
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// PIN CONFIGURATION
// ============================================================================
// NES controller pins (directly from controller port)
// These can be customized for different boards
#define NES_PIN_CLOCK  5   // CLK - output to controller
#define NES_PIN_LATCH  6   // LATCH - output to controller
#define NES_PIN_DATA0  8   // DATA - input from controller

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_SIMPLE   // Simple 1:1 (NES → USB)
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
// APP INTERFACE
// ============================================================================
void app_init(void);
void app_task(void);

#endif // APP_NES2USB_H
