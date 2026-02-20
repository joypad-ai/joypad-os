// app.h - NEOGEO2USB App Manifest
// NEOGEO controller to USB HID gamepad adapter
//
// This app reads native NEOGEO controllers and outputs USB HID gamepad.
// Supports NEOGEO controllers/sticks 4/6 buttons.

#ifndef APP_NEOGEO2USB_H
#define APP_NEOGEO2USB_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "NEOGEO2USB"
#define APP_VERSION "1.0.0"
#define APP_DESCRIPTION "NEOGEO controller to USB HID gamepad adapter"
#define APP_AUTHOR "herzmx"

// ============================================================================
// CORE DEPENDENCIES
// ============================================================================

// Input drivers - Native Arcade host (NOT USB)
#define REQUIRE_NATIVE_NEOGEO_HOST 1
#define NEOGEO_MAX_CONTROLLERS 1          // Single NEOGEO port

// Output drivers
#define REQUIRE_USB_DEVICE 1
#define USB_OUTPUT_PORTS 1              // Single USB gamepad output

// Services
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// PIN CONFIGURATION
// ============================================================================
// Default KB2040 GPIO pins for NEOGEO controller
// These can be customized for different boards

#ifdef PICO_RP2040_ZERO_BUILD
    #define NEOGEO_PIN_DU  8    // Dpad Up
    #define NEOGEO_PIN_DD  2    // Dpad Down
    #define NEOGEO_PIN_DR  3    // Dpad Right
    #define NEOGEO_PIN_DL  9    // Dpad Left
    #define NEOGEO_PIN_B1  10   // B1/P1
    #define NEOGEO_PIN_B2  4    // B2/P2
    #define NEOGEO_PIN_B3  11   // B3/P3
    #define NEOGEO_PIN_B4  27   // B4/K1
    #define NEOGEO_PIN_B5  13   // B5/K2
    #define NEOGEO_PIN_B6  29   // B6/K3
    #define NEOGEO_PIN_S1  28   // Coin
    #define NEOGEO_PIN_S2  12   // Start
#else
    #define NEOGEO_PIN_DU  10   // Dpad Up
    #define NEOGEO_PIN_DD  19   // Dpad Down
    #define NEOGEO_PIN_DR  18   // Dpad Right
    #define NEOGEO_PIN_DL  20   // Dpad Left
    #define NEOGEO_PIN_B1  2    // B1/P1
    #define NEOGEO_PIN_B2  3    // B2/P2
    #define NEOGEO_PIN_B3  4    // B3/P3
    #define NEOGEO_PIN_B4  5    // B4/K1
    #define NEOGEO_PIN_B5  8    // B5/K2
    #define NEOGEO_PIN_B6  9    // B6/K3
    #define NEOGEO_PIN_S1  7    // Coin
    #define NEOGEO_PIN_S2  6    // Start
#endif


// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_SIMPLE   // Simple 1:1 (NEOGEO → USB)
#define MERGE_MODE MERGE_ALL

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_FIXED  // Fixed slots (no shifting)
#define MAX_PLAYER_SLOTS 1                   // Single player for now
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#ifdef PICO_RP2040_ZERO_BUILD
    #define BOARD "rp2040zero"
#else
    #define BOARD "ada_kb2040"               // KB2040 default
#endif
#define CPU_OVERCLOCK_KHZ 0                 // No overclock needed
#define UART_DEBUG 1
#define ARCADE_PAD_DEBUG 1

// ============================================================================
// APP INTERFACE
// ============================================================================
void app_init(void);

#endif // APP_NEOGEO2USB_H
