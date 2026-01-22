// app.h - USB2NEOGEO App Manifest
// USB to NEOGEO+ adapter
//
// This manifest declares what drivers and services this app needs.
// The build system uses these flags to conditionally compile only required code.

#ifndef APP_USB2NEOGEO_H
#define APP_USB2NEOGEO_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "USB2NEOGEO"
#define APP_VERSION "1.0.0"
#define APP_DESCRIPTION "USB to NEOGEO adapter"
#define APP_AUTHOR "herzmx"

// ============================================================================
// CORE DEPENDENCIES (What drivers to compile in)
// ============================================================================

// Input drivers
#define REQUIRE_USB_HOST 1
#define MAX_USB_DEVICES 6              // Support up to 6 USB devices

// Output drivers
#define REQUIRE_NATIVE_NEOGEO_OUTPUT 1
#define NEOGEO_OUTPUT_PORTS 1        // NEOGEO adapter support 1 player

// Services
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_SIMPLE   // Simple 1:1 routing (USB → NEOGEO+ adapter)
#define MERGE_MODE MERGE_ALL
#define MAX_ROUTES 5

// Input transformations - NONE for NEOGEO
#define TRANSFORM_FLAGS 0

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_SHIFT // NEOGEO: shift players on disconnect
#define MAX_PLAYER_SLOTS 1                 // NEOGEO adapter support 1 player
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "ada_kb2040"
#define CPU_OVERCLOCK_KHZ 0             // No overclock needed for NEOGEO
#define UART_DEBUG 1

// ============================================================================
// APP FEATURES
// ============================================================================
#define FEATURE_MOUSE_SUPPORT 0         // Mouse-to-analog
#define FEATURE_MULTITAP 0              // 1 player support

// ============================================================================
// APP INTERFACE (OS calls these)
// ============================================================================
void app_init(void);                    // Called after OS init
void app_task(void);                    // Called in main loop (optional)

#endif // APP_USB2NEOGEO_H
