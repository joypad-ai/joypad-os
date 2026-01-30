// app.h - N64USB App Manifest
// USB to N64 adapter
//
// This manifest declares what drivers and services this app needs.
// The build system uses these flags to conditionally compile only required code.

#ifndef APP_N64USB_H
#define APP_N64USB_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "N64USB"
#define APP_VERSION "1.0.0"
#define APP_DESCRIPTION "USB to N64 adapter"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES (What drivers to compile in)
// ============================================================================

// Input drivers
#define REQUIRE_USB_HOST 1
#define MAX_USB_DEVICES 4

// Output drivers
#define REQUIRE_NATIVE_N64_OUTPUT 1
#define N64_OUTPUT_PORTS 1             // Single port (N64 has no multitap in adapter)

// Services
#define REQUIRE_FLASH_SETTINGS 1
#define REQUIRE_PROFILE_SYSTEM 1
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_MERGE
#define MERGE_MODE MERGE_BLEND             // Blend all USB inputs
#define APP_MAX_ROUTES 4

// Input transformations
#define TRANSFORM_FLAGS (TRANSFORM_MOUSE_TO_ANALOG)  // Mouse → analog stick

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS 1                 // N64 = single player per adapter
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "ada_kb2040"
#define CPU_OVERCLOCK_KHZ 0                // N64 joybus works at default clock
#define UART_DEBUG 1

// ============================================================================
// APP FEATURES
// ============================================================================
#define FEATURE_PROFILES 1                 // N64 profile system
#define FEATURE_KEYBOARD_MODE 0            // N64 has no keyboard support
#define FEATURE_ADAPTIVE_TRIGGERS 0        // N64 has no analog triggers

// ============================================================================
// APP INTERFACE (OS calls these)
// ============================================================================
void app_init(void);
void app_task(void);

#endif // APP_N64USB_H
