// app.h - JVS2USB App Manifest
// JVS controller to USB HID gamepad adapter
//
// This app reads native JVS I/O Boards and outputs USB HID gamepad.
// Supports JVS I/O Boards

#ifndef APP_JVS2USB_H
#define APP_JVS2USB_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "JVS2USB"
#define APP_DESCRIPTION "JVS controller to USB HID gamepad adapter"
#define APP_AUTHOR "herzmx"

// ============================================================================
// CORE DEPENDENCIES
// ============================================================================

// Input drivers - Native JVS host (NOT USB)
#define REQUIRE_NATIVE_JVS_HOST 1
#define JVS_MAX_CONTROLLERS 2          // 2 controller from 1 JVS I/O Board

// Output drivers
#define REQUIRE_USB_DEVICE 1
#define USB_OUTPUT_PORTS   2           // 2 USB gamepad output

// Services
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_MERGE   // Simple 1:1 (JVS → USB)
#define MERGE_MODE MERGE_ALL

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_FIXED  // Fixed slots (no shifting)
#define MAX_PLAYER_SLOTS 2
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "rp2040zero"

#define CPU_OVERCLOCK_KHZ 0                 // No overclock needed
#define UART_DEBUG 1
#define ARCADE_PAD_DEBUG 1

// ============================================================================
// APP INTERFACE
// ============================================================================
void app_init(void);

#endif // APP_JVS2USB_H
