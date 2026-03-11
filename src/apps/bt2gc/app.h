// app.h - BT2GC App Manifest
// Bluetooth to GameCube console adapter for Pico W
//
// Uses Pico W's built-in CYW43 Bluetooth to receive controllers,
// outputs to GameCube via joybus PIO protocol.

#ifndef APP_BT2GC_H
#define APP_BT2GC_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "BT2GC"
#define APP_VERSION "0.1.0"
#define APP_DESCRIPTION "Bluetooth to GameCube console adapter (Pico W)"
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
#define REQUIRE_NATIVE_GAMECUBE_OUTPUT 1
#define GAMECUBE_OUTPUT_PORTS 1         // Single port

// Services
#define REQUIRE_FLASH_SETTINGS 1
#define REQUIRE_PROFILE_SYSTEM 1
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_MERGE
#define MERGE_MODE MERGE_BLEND          // Blend all BT inputs
#define APP_MAX_ROUTES 4

// Input transformations
#define TRANSFORM_FLAGS (TRANSFORM_MOUSE_TO_ANALOG)

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_SHIFT
#define MAX_PLAYER_SLOTS 1              // Single player (single GC port)
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "pico_w"
#define CPU_OVERCLOCK_KHZ 130000        // GameCube needs 130MHz for joybus timing
#define UART_DEBUG 1

// ============================================================================
// BLUETOOTH CONFIGURATION
// ============================================================================
#define BT_MAX_CONNECTIONS 4
#define BT_SCAN_ON_STARTUP 1

// ============================================================================
// APP FEATURES
// ============================================================================
#define FEATURE_PROFILES 1
#define FEATURE_KEYBOARD_MODE 1

// ============================================================================
// APP INTERFACE (OS calls these)
// ============================================================================
void app_init(void);
void app_task(void);

#endif // APP_BT2GC_H
