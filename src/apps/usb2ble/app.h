// app.h - USB2BLE App Manifest
// USB to BLE HID Gamepad adapter (Pico W)
//
// USB controllers → BLE gamepad output via HOGP peripheral.
// Appears as a wireless gamepad to PCs, phones, and consoles.

#ifndef APP_USB2BLE_H
#define APP_USB2BLE_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "USB2BLE"
#define APP_VERSION "1.0.0"
#define APP_DESCRIPTION "USB to BLE HID gamepad adapter (Pico W)"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES
// ============================================================================

// Input drivers
#define REQUIRE_USB_HOST 1
#define MAX_USB_DEVICES 4

// Output drivers
#define REQUIRE_BLE_OUTPUT 1

// Services
#define REQUIRE_FLASH_SETTINGS 0
#define REQUIRE_PROFILE_SYSTEM 0
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_MERGE
#define MERGE_MODE MERGE_BLEND
#define APP_MAX_ROUTES 4

// Input transformations
#define TRANSFORM_FLAGS 0

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS 4
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "pico_w"
#define CPU_OVERCLOCK_KHZ 0
#define UART_DEBUG 1

// ============================================================================
// APP FEATURES
// ============================================================================
#define FEATURE_PROFILES 0

// ============================================================================
// APP INTERFACE (OS calls these)
// ============================================================================
void app_init(void);
void app_task(void);

#endif // APP_USB2BLE_H
