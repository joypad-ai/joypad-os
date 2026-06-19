// app.h - BT2NUON App Manifest
// Bluetooth to Nuon DVD player adapter for Pico W
//
// Uses Pico W's built-in CYW43 Bluetooth to receive controllers,
// outputs to Nuon via Polyface PIO protocol.
//
// This manifest declares what drivers and services this app needs.
// The build system uses these flags to conditionally compile only required code.

#ifndef APP_BT2NUON_H
#define APP_BT2NUON_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "BT2NUON"
#define APP_DESCRIPTION "Bluetooth to Nuon DVD player adapter (Pico W)"
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
#define REQUIRE_NATIVE_NUON_OUTPUT 1
#define NUON_OUTPUT_PORTS 1             // Nuon single player

// Services
#define REQUIRE_FLASH_SETTINGS 0        // No profile persistence yet
#define REQUIRE_PROFILE_SYSTEM 1
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_MERGE
#define MERGE_MODE MERGE_BLEND          // Blend all BT inputs
#define APP_MAX_ROUTES 4

// Input transformations
#define TRANSFORM_FLAGS 0               // No transformations

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_SHIFT
#define MAX_PLAYER_SLOTS 1              // Nuon is single player
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
#define FEATURE_SPINNER_SUPPORT 1       // Right stick to spinner conversion
#define FEATURE_SOFT_RESET 1            // Soft reset via button combo

// ============================================================================
// APP INTERFACE (OS calls these)
// ============================================================================
void app_init(void);
void app_task(void);

#endif // APP_BT2NUON_H
