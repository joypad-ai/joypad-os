// app.h - GCUSB App Manifest
// USB to GameCube adapter
//
// This manifest declares what drivers and services this app needs.
// The build system uses these flags to conditionally compile only required code.

#ifndef APP_GCUSB_H
#define APP_GCUSB_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "usb2gc"
#define APP_DESCRIPTION "USB to GameCube adapter"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES (What drivers to compile in)
// ============================================================================

// Input drivers
#define REQUIRE_USB_HOST 1
#define MAX_USB_DEVICES 4

// Output drivers
#define REQUIRE_NATIVE_GAMECUBE_OUTPUT 1
#define GAMECUBE_OUTPUT_PORTS 1        // Single port for now (future: 4)
#define REQUIRE_USB_DEVICE 1           // CDC config mode when not connected to GameCube

// Services
#define REQUIRE_FLASH_SETTINGS 1
#define REQUIRE_PROFILE_SYSTEM 1
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_MERGE
#define MERGE_MODE MERGE_BLEND             // Blend all USB inputs (OR buttons together)
#define APP_MAX_ROUTES 4                   // App-specific route limit (router uses MAX_ROUTES)

// Input transformations
#define TRANSFORM_FLAGS (TRANSFORM_MOUSE_TO_ANALOG)  // Mouse → analog stick

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_FIXED // Future 4-port needs fixed slots
#define MAX_PLAYER_SLOTS 4
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "ada_kb2040"
#define CPU_OVERCLOCK_KHZ 130000        // GameCube needs 130MHz for joybus timing
#define UART_DEBUG 1

// ============================================================================
// APP FEATURES
// ============================================================================
#define FEATURE_PROFILES 1              // GameCube profile system
#define FEATURE_KEYBOARD_MODE 1         // GameCube keyboard support
#define FEATURE_ADAPTIVE_TRIGGERS 1     // DualSense trigger support

// ============================================================================
// APP INTERFACE (OS calls these)
// ============================================================================
void app_init(void);                    // Called after OS init
void app_task(void);                    // Called in main loop (optional)

#endif // APP_GCUSB_H
