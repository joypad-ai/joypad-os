// app.h - Xbox Adapter App Manifest
// USB to Xbox One adapter (hardware passthrough)
//
// This manifest declares what drivers and services this app needs.
// The build system uses these flags to conditionally compile only required code.

#ifndef APP_XBOXADAPTER_H
#define APP_XBOXADAPTER_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "Xbox-Adapter"
#define APP_VERSION "2.0.0"
#define APP_DESCRIPTION "USB to Xbox One adapter (I2C/DAC passthrough)"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES (What drivers to compile in)
// ============================================================================

// Input drivers
#define REQUIRE_USB_HOST 1
#define MAX_USB_DEVICES 1              // Xbox One supports 1 controller

// Output drivers
#define REQUIRE_NATIVE_XBOXONE_OUTPUT 1
#define XBOXONE_OUTPUT_PORTS 1         // Xbox One single player

// Services
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_SIMPLE   // Simple 1:1 routing (USB → Xbox One)
#define MERGE_MODE MERGE_ALL
#define MAX_ROUTES 1

// Input transformations
#define TRANSFORM_FLAGS (TRANSFORM_MOUSE_TO_ANALOG)  // Mouse → analog stick

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_SHIFT // Xbox One: shift players on disconnect (single player)
#define MAX_PLAYER_SLOTS 1                  // Xbox One is single player
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "ada_qtpy"               // Uses Adafruit QT Py RP2040
#define CPU_OVERCLOCK_KHZ 0            // No overclock needed
#define UART_DEBUG 1

// ============================================================================
// APP FEATURES
// ============================================================================
#define FEATURE_I2C_PASSTHROUGH 1      // I2C slave to emulate Xbox One controller
#define FEATURE_DAC_ANALOG 1           // MCP4728 DAC for analog sticks/triggers
#define FEATURE_MOUSE_SUPPORT 1        // Mouse-to-analog transformation

// ============================================================================
// APP INTERFACE (OS calls these)
// ============================================================================
void app_init(void);                    // Called after OS init

#endif // APP_XBOXADAPTER_H
