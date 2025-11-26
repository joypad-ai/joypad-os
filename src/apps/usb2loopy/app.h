// app.h - LoopyUSB App Manifest
// USB to Casio Loopy adapter
//
// This manifest declares what drivers and services this app needs.
// The build system uses these flags to conditionally compile only required code.

#ifndef APP_LOOPYUSB_H
#define APP_LOOPYUSB_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "LoopyUSB"
#define APP_VERSION "2.0.0-experimental"
#define APP_DESCRIPTION "USB to Casio Loopy adapter (experimental)"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES (What drivers to compile in)
// ============================================================================

// Input drivers
#define REQUIRE_USB_HOST 1
#define MAX_USB_DEVICES 4              // Support up to 4 USB devices

// Output drivers
#define REQUIRE_NATIVE_LOOPY_OUTPUT 1
#define LOOPY_OUTPUT_PORTS 4           // Loopy supports 4 players

// Services
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_SIMPLE   // Simple 1:1 routing (USB → Loopy ports)
#define MERGE_MODE MERGE_ALL
#define APP_MAX_ROUTES 4                   // App-specific route limit (router uses MAX_ROUTES)

// Input transformations
#define TRANSFORM_FLAGS 0  // No transformations needed

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_SHIFT // Loopy: shift players on disconnect
#define MAX_PLAYER_SLOTS 4                  // Supports 4 players
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "ada_kb2040"
#define CPU_OVERCLOCK_KHZ 0             // No overclock needed for Loopy
#define UART_DEBUG 1

// ============================================================================
// APP FEATURES
// ============================================================================
#define FEATURE_EXPERIMENTAL 1          // Loopy is experimental

// ============================================================================
// APP INTERFACE (OS calls these)
// ============================================================================
void app_init(void);                    // Called after OS init

#endif // APP_LOOPYUSB_H
