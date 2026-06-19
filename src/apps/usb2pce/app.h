// app.h - USB2PCE App Manifest
// USB to PCEngine/TurboGrafx-16 adapter
//
// This manifest declares what drivers and services this app needs.
// The build system uses these flags to conditionally compile only required code.

#ifndef APP_USB2PCE_H
#define APP_USB2PCE_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "USB2PCE"
#define APP_DESCRIPTION "USB to PCEngine/TurboGrafx-16 adapter with multitap"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES (What drivers to compile in)
// ============================================================================

// Input drivers
#define REQUIRE_USB_HOST 1
#define MAX_USB_DEVICES 6              // Support up to 6 USB devices

// Output drivers
#define REQUIRE_NATIVE_PCENGINE_OUTPUT 1
#define PCENGINE_OUTPUT_PORTS 5        // PCEngine multitap supports 5 players

// Services
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_SIMPLE   // Simple 1:1 routing (USB → PCE multitap)
#define MERGE_MODE MERGE_ALL
#define MAX_ROUTES 5

// Input transformations - NONE for PCE, we output native mouse protocol directly
#define TRANSFORM_FLAGS 0

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_SHIFT // PCEngine: shift players on disconnect
#define MAX_PLAYER_SLOTS 5                  // Multitap supports 5 players
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "ada_kb2040"
#define CPU_OVERCLOCK_KHZ 0             // No overclock needed for PCEngine
#define UART_DEBUG 1

// ============================================================================
// APP FEATURES
// ============================================================================
#define FEATURE_MOUSE_SUPPORT 1         // Mouse-to-analog for Populous
#define FEATURE_MULTITAP 1              // 5-player multitap support

// ============================================================================
// APP INTERFACE (OS calls these)
// ============================================================================
void app_init(void);                    // Called after OS init
void app_task(void);                    // Called in main loop (optional)

#endif // APP_USB2PCE_H
