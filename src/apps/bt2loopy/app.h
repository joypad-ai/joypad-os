// app.h - BT2LOOPY App Manifest
// Bluetooth to Casio Loopy adapter for Pico W
//
// Uses Pico W's built-in CYW43 Bluetooth to receive controllers,
// outputs to Casio Loopy via PIO protocol.
//
// This manifest declares what drivers and services this app needs.
// The build system uses these flags to conditionally compile only required code.

#ifndef APP_BT2LOOPY_H
#define APP_BT2LOOPY_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "BT2LOOPY"
#define APP_VERSION "1.0.0"
#define APP_DESCRIPTION "Bluetooth to Casio Loopy adapter (Pico W)"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES (What drivers to compile in)
// ============================================================================

// Input drivers - Pico W built-in Bluetooth
#define REQUIRE_BT_CYW43 1              // CYW43 Bluetooth (Pico W built-in)
#define REQUIRE_USB_HOST 0              // No USB host needed
#define MAX_USB_DEVICES 0

// Output drivers
#define REQUIRE_USB_DEVICE 0            // No USB device output
#define REQUIRE_NATIVE_LOOPY_OUTPUT 1
#define LOOPY_OUTPUT_PORTS 4            // Loopy supports 4 players

// Services
#define REQUIRE_FLASH_SETTINGS 0        // No profile persistence yet
#define REQUIRE_PROFILE_SYSTEM 1
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_SIMPLE // Simple 1:1 routing (BT → Loopy ports)
#define MERGE_MODE MERGE_ALL
#define APP_MAX_ROUTES 4

// Input transformations
#define TRANSFORM_FLAGS 0               // No transformations

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_SHIFT
#define MAX_PLAYER_SLOTS 4              // Loopy supports 4 players
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "pico_w"                  // Raspberry Pi Pico W
#define CPU_OVERCLOCK_KHZ 0             // No overclock needed
#define UART_DEBUG 1

// ============================================================================
// BLUETOOTH CONFIGURATION
// ============================================================================
#define BT_MAX_CONNECTIONS 4            // Max BT controllers
#define BT_SCAN_ON_STARTUP 1            // Start scanning for controllers on boot

// ============================================================================
// APP FEATURES
// ============================================================================
#define FEATURE_PROFILES 1
#define FEATURE_SOFT_RESET 1            // Soft reset via button combo

// ============================================================================
// APP INTERFACE (OS calls these)
// ============================================================================
void app_init(void);
void app_task(void);

#endif // APP_BT2LOOPY_H
