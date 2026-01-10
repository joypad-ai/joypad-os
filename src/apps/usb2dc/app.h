// app.h - USB2DC App Manifest
// USB/Bluetooth to Dreamcast adapter
//
// This manifest declares what drivers and services this app needs.

#ifndef APP_USB2DC_H
#define APP_USB2DC_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "USB2DC"
#define APP_VERSION "1.0.0"
#define APP_DESCRIPTION "USB/BT to Dreamcast adapter"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES
// ============================================================================

// Input drivers
#define REQUIRE_USB_HOST 1
#define MAX_USB_DEVICES 4

// Output drivers
#define REQUIRE_NATIVE_DREAMCAST_OUTPUT 1
#define DREAMCAST_OUTPUT_PORTS 1        // Single port (future: 4-port multitap)

// Services
#define REQUIRE_FLASH_SETTINGS 1
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_MERGE
#define MERGE_MODE MERGE_BLEND             // Blend all USB inputs
#define APP_MAX_ROUTES 4

// Input transformations
#define TRANSFORM_FLAGS (TRANSFORM_MOUSE_TO_ANALOG)

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS 4
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "ada_kb2040"
#define UART_DEBUG 1

// ============================================================================
// APP INTERFACE
// ============================================================================
void app_init(void);
void app_task(void);

#endif // APP_USB2DC_H
