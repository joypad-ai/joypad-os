// app.h - LodgeNet2USB App Manifest
// LodgeNet hotel gaming controller to USB HID gamepad adapter
//
// Reads LodgeNet N64/GameCube controllers via proprietary 3-wire serial
// protocol and outputs USB HID gamepad. Supports both N64 and GC variants
// (same protocol, 64-bit unified data format).

#ifndef APP_LODGENET2USB_H
#define APP_LODGENET2USB_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "LodgeNet2USB"
#define APP_VERSION "0.1.0"
#define APP_DESCRIPTION "LodgeNet controller to USB HID gamepad adapter"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES
// ============================================================================

// Input drivers
#define REQUIRE_NATIVE_LODGENET_HOST 1

// Output drivers
#define REQUIRE_USB_DEVICE 1
#define USB_OUTPUT_PORTS 1

// Services
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// PIN CONFIGURATION
// ============================================================================
// LodgeNet 3-wire protocol (RJ11 connector)
//   RJ11 Pin 1: +5V (from board VBUS or 5V rail)
//   RJ11 Pin 2: CLOCK → GPIO output
//   RJ11 Pin 3: DATA  → GPIO input
//   RJ11 Pin 4: GND
#define LODGENET_PIN_CLOCK  5   // Clock output to controller
#define LODGENET_PIN_DATA   7   // Data input from controller

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_SIMPLE
#define MERGE_MODE MERGE_ALL

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS 1
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "ada_kb2040"
#define CPU_OVERCLOCK_KHZ 0
#define UART_DEBUG 1

// ============================================================================
// APP INTERFACE
// ============================================================================
void app_init(void);

#endif // APP_LODGENET2USB_H
