// app.h - Universal Controller App Manifest
// GPIO buttons/analog → USB HID Gamepad output
//
// Supports multiple controller types via build-time configuration.
// Each controller type defines its board and GPIO pin mapping.

#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "controller"
#define APP_DESCRIPTION "GPIO controller to USB gamepad"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CONTROLLER TYPE SELECTION
// ============================================================================
// Define ONE of these at build time:
//   CONTROLLER_TYPE_FISHERPRICE        - Fisher Price mod (KB2040)
//   CONTROLLER_TYPE_FISHERPRICE_ANALOG - Fisher Price with analog (KB2040)
//   CONTROLLER_TYPE_MACROPAD           - Adafruit MacroPad RP2040

// ============================================================================
// CORE DEPENDENCIES
// ============================================================================

// Input drivers
#define REQUIRE_USB_HOST 0              // No USB host input
#define REQUIRE_GPIO_INPUT 1            // GPIO button input
#define MAX_GPIO_DEVICES 1

// Output drivers
#define REQUIRE_USB_DEVICE 1            // USB device output
#define USB_OUTPUT_PORTS 1              // Single gamepad

// Services
#define REQUIRE_FLASH_SETTINGS 0
#define REQUIRE_PROFILE_SYSTEM 0
#define REQUIRE_PLAYER_MANAGEMENT 0     // Single player, no management needed

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_SIMPLE
#define MERGE_MODE MERGE_PRIORITY
#define APP_MAX_ROUTES 1

// Input transformations
#define TRANSFORM_FLAGS 0

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS 1
#define AUTO_ASSIGN_ON_PRESS 0

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
// Board is defined per controller type in CMakeLists.txt
#define CPU_OVERCLOCK_KHZ 0
#define UART_DEBUG 0

// ============================================================================
// APP INTERFACE
// ============================================================================
void app_init(void);
void app_task(void);

#endif // APP_CONTROLLER_H
