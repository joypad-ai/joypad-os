// app.h - NUONUSB App Manifest
// USB to Nuon DVD player adapter
//
// This manifest declares what drivers and services this app needs.
// The build system uses these flags to conditionally compile only required code.

#ifndef APP_NUONUSB_H
#define APP_NUONUSB_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "NUONUSB"
#define APP_VERSION "2.0.0"
#define APP_DESCRIPTION "USB to Nuon DVD player adapter"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES (What drivers to compile in)
// ============================================================================

// Input drivers
#define REQUIRE_USB_HOST 1
#define MAX_USB_DEVICES 1              // Nuon typically supports 1 controller

// Output drivers
#define REQUIRE_NATIVE_NUON_OUTPUT 1
#define NUON_OUTPUT_PORTS 1            // Nuon single player

// Services
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_SIMPLE   // Simple 1:1 routing (USB → Nuon)
#define MERGE_MODE MERGE_ALL
#define APP_MAX_ROUTES 1                   // App-specific route limit (router uses MAX_ROUTES)

// Input transformations
#define TRANSFORM_FLAGS (TRANSFORM_NONE)  // No transformations needed

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_SHIFT // Nuon: shift players on disconnect (single player)
#define MAX_PLAYER_SLOTS 1                  // Nuon is single player
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "ada_kb2040"
#define CPU_OVERCLOCK_KHZ 0             // No overclock needed for Nuon
#define UART_DEBUG 1

// ============================================================================
// APP FEATURES
// ============================================================================
#define FEATURE_SPINNER_SUPPORT 1       // Right stick to spinner conversion
#define FEATURE_SOFT_RESET 1            // Soft reset via button combo

// ============================================================================
// APP INTERFACE (OS calls these)
// ============================================================================
void app_init(void);                    // Called after OS init

#endif // APP_NUONUSB_H
