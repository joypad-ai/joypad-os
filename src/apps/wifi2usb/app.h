// app.h - WiFi2USB App Manifest
// WiFi to USB adapter (HID Gamepad output) for Pico W
//
// Uses Pico W's CYW43 WiFi in AP mode to receive JOCP controller packets,
// outputs as USB HID device.
//
// This manifest declares what drivers and services this app needs.
// The build system uses these flags to conditionally compile only required code.

#ifndef APP_WIFI2USB_H
#define APP_WIFI2USB_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "WiFi2USB"
#define APP_VERSION "0.1.0"
#define APP_DESCRIPTION "WiFi to USB HID gamepad adapter (Pico W)"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES (What drivers to compile in)
// ============================================================================

// Input drivers - WiFi via JOCP protocol
#define REQUIRE_WIFI_CYW43 1            // CYW43 WiFi (Pico W built-in)
#define REQUIRE_BT_CYW43 0              // No Bluetooth needed
#define REQUIRE_USB_HOST 0              // No USB host needed
#define MAX_USB_DEVICES 0

// Output drivers
#define REQUIRE_USB_DEVICE 1
#define USB_OUTPUT_PORTS 1              // Single gamepad for now

// Services
#define REQUIRE_FLASH_SETTINGS 0        // No profile persistence yet
#define REQUIRE_PROFILE_SYSTEM 0        // No profiles yet
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_MERGE
#define MERGE_MODE MERGE_BLEND          // Blend all WiFi inputs
#define APP_MAX_ROUTES 4

// Input transformations
#define TRANSFORM_FLAGS 0               // No transformations

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS 4
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "pico_w"                  // Raspberry Pi Pico W
#define CPU_OVERCLOCK_KHZ 0             // No overclock needed
#define UART_DEBUG 1

// ============================================================================
// WIFI CONFIGURATION
// ============================================================================
#define WIFI_AP_SSID_PREFIX "JOYPAD-"   // AP SSID will be JOYPAD-XXXX
#define WIFI_AP_PASSWORD "joypad1234"   // Default WPA2 password
#define WIFI_AP_CHANNEL 6               // WiFi channel (1-11)
#define WIFI_MAX_CONNECTIONS 4          // Max simultaneous controllers

// JOCP Protocol Ports
#define JOCP_UDP_PORT 30100             // UDP input port
#define JOCP_TCP_PORT 30101             // TCP control port

// ============================================================================
// APP FEATURES
// ============================================================================
#define FEATURE_PROFILES 0              // No profiles yet
#define FEATURE_OUTPUT_MODE_SELECT 1    // Allow switching USB output modes

// ============================================================================
// APP INTERFACE (OS calls these)
// ============================================================================
void app_init(void);
void app_task(void);

#endif // APP_WIFI2USB_H
