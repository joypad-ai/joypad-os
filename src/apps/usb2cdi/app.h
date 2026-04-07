// app.h - USB2CDI App Manifest
// USB/BT controllers to Philips CD-i console adapter

#ifndef APP_USB2CDI_H
#define APP_USB2CDI_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "USB2CDI"
#define APP_VERSION "0.1.0"
#define APP_DESCRIPTION "USB/BT controllers to CD-i console adapter"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES
// ============================================================================
#define REQUIRE_USB_HOST 1
#define MAX_USB_DEVICES 4
#define REQUIRE_NATIVE_CDI_OUTPUT 1
#define CDI_OUTPUT_PORTS 1
#define REQUIRE_PLAYER_MANAGEMENT 1
#define REQUIRE_PROFILE_SYSTEM 1

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_SIMPLE
#define MERGE_MODE MERGE_ALL
#define TRANSFORM_FLAGS (TRANSFORM_MOUSE_TO_ANALOG)

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_SHIFT
#define MAX_PLAYER_SLOTS 1
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "ada_kb2040"
#define CPU_OVERCLOCK_KHZ 0
#define UART_DEBUG 1

// ============================================================================
// APP FEATURES
// ============================================================================
#define FEATURE_PROFILES 1

// ============================================================================
// APP INTERFACE
// ============================================================================
void app_init(void);
void app_task(void);

#endif // APP_USB2CDI_H
