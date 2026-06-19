// app.h - 3DOUSB App Manifest
// USB to 3DO adapter with 8-player support
//
// This manifest declares what drivers and services this app needs.
// The build system uses these flags to conditionally compile only required code.

#ifndef APP_3DOUSB_H
#define APP_3DOUSB_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "3DOUSB"
#define APP_DESCRIPTION "USB to 3DO adapter with 8-player multitap and passthrough"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES (What drivers to compile in)
// ============================================================================

// Input drivers
#define REQUIRE_USB_HOST 1
#define MAX_USB_DEVICES 8              // Support up to 8 USB devices

// Output drivers
#define REQUIRE_NATIVE_3DO_OUTPUT 1
#define TDO_OUTPUT_PORTS 8             // 3DO supports up to 8 players (USB + extension passthrough)

// Services
#define REQUIRE_PLAYER_MANAGEMENT 1
#define REQUIRE_PROFILE_SYSTEM 1       // 3DO has button mapping profiles

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_SIMPLE   // Simple 1:1 routing (USB → 3DO multitap)
#define MERGE_MODE MERGE_ALL
#define APP_MAX_ROUTES 8                   // App-specific route limit (router uses MAX_ROUTES)

// Input transformations
// No mouse-to-analog: mouse input now routes per-slot to a 3DO mouse output
// (ID 0x49) via update_3do_report; the transform would zero delta_x/delta_y
// before the 3DO output could read them.
#define TRANSFORM_FLAGS 0

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_SHIFT // 3DO: shift players on disconnect
#define MAX_PLAYER_SLOTS 8                  // 3DO supports up to 8 players
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "ada_kb2040"
#define CPU_OVERCLOCK_KHZ 0             // No overclock needed for 3DO
#define UART_DEBUG 1

// ============================================================================
// APP FEATURES
// ============================================================================
#define FEATURE_MOUSE_SUPPORT 1         // Mouse-to-analog transformation
#define FEATURE_MULTITAP 1              // 8-player support
#define FEATURE_EXTENSION_PASSTHROUGH 1 // Pass through native 3DO controllers
#define FEATURE_PROFILE_SWITCHING 1     // Runtime button mapping profiles

// ============================================================================
// APP INTERFACE (OS calls these)
// ============================================================================
void app_init(void);                    // Called after OS init

#endif // APP_3DOUSB_H
