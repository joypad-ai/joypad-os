// app.h - USB2USB App Manifest
// USB to USB adapter (HID Gamepad output)
//
// This manifest is a human-readable summary of what this app uses.
// It is NOT consumed by the build system. The authoritative per-target
// configuration lives in src/CMakeLists.txt. Only #ifndef-guarded flags are
// read by code (see REQUIRE_BT_INPUT / REQUIRE_BLE_OUTPUT in universal);
// every other flag here is descriptive only and changing it has no effect.
// See issue #198.

#ifndef APP_USB2USB_H
#define APP_USB2USB_H

// ============================================================================
// APP METADATA
// ============================================================================
#ifndef APP_NAME
#define APP_NAME "usb2usb"
#endif
#define APP_DESCRIPTION "USB to USB HID gamepad adapter"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES (What drivers to compile in)
// ============================================================================

// Input drivers
#define REQUIRE_USB_HOST 1
#define MAX_USB_DEVICES 4

// Output drivers
#define REQUIRE_USB_DEVICE 1
#define USB_OUTPUT_PORTS 1             // Single gamepad for now (future: 4)

// Services
#define REQUIRE_FLASH_SETTINGS 0       // Descriptive only. Profiles DO persist:
                                        // flash_init() at usbd.c:574, loaded by
                                        // profile_load_from_flash(), saved via storage_task().
#define REQUIRE_PROFILE_SYSTEM 0       // No profiles yet
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_MERGE
#define MERGE_MODE MERGE_BLEND          // Blend all USB inputs
#define APP_MAX_ROUTES 4

// Input transformations
// Mouse-to-analog: Maps mouse X to right stick X for accessibility (mouthpad, head tracker)
#define TRANSFORM_FLAGS TRANSFORM_MOUSE_TO_ANALOG

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS 4
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "ada_feather_usbhost"     // Feather has dual USB ports
#define CPU_OVERCLOCK_KHZ 0             // No overclock needed
#define UART_DEBUG 1

// ============================================================================
// APP FEATURES
// ============================================================================
#define FEATURE_PROFILES 0              // No profiles yet
#define FEATURE_OUTPUT_MODE_SELECT 0    // Future: Switch between HID/XInput/PS3/etc

// ============================================================================
// APP INTERFACE (OS calls these)
// ============================================================================
void app_init(void);
void app_task(void);

#endif // APP_USB2USB_H
