// app.h - USB2UART App Manifest
// USB to UART bridge for ESP32 communication
//
// This app reads USB controllers and sends their state over UART to an ESP32.
// The ESP32 can send feedback (rumble, LED) commands back.

#ifndef APP_USB2UART_H
#define APP_USB2UART_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "USB2UART"
#define APP_DESCRIPTION "USB to UART bridge for ESP32 AI platform"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES (What drivers to compile in)
// ============================================================================

// Input drivers
#define REQUIRE_USB_HOST 1
#define MAX_USB_DEVICES 6              // Support up to 6 USB devices

// Output drivers
#define REQUIRE_UART_OUTPUT 1
#define UART_OUTPUT_PLAYERS 8          // UART supports 8 players

// Services
#define REQUIRE_PLAYER_MANAGEMENT 1
#define REQUIRE_FEEDBACK 1

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_SIMPLE   // Simple 1:1 routing (USB → UART)
#define MERGE_MODE MERGE_PRIORITY
// MAX_ROUTES is defined in router.h

// Input transformations
#define TRANSFORM_FLAGS (TRANSFORM_NONE)

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_FIXED // Fixed slots (ESP32 expects consistent mapping)
#define MAX_PLAYER_SLOTS 8                  // Support up to 8 players
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "ada_kb2040"
#define CPU_OVERCLOCK_KHZ 0             // No overclock needed

// UART pins (Qwiic cable compatible)
#define UART_TX_PIN 4                   // TX to ESP32 RX
#define UART_RX_PIN 5                   // RX from ESP32 TX
#define UART_BAUD 1000000               // 1Mbaud

// Debug UART (separate from bridge UART)
#define UART_DEBUG 1
#define UART_DEBUG_TX_PIN 12
#define UART_DEBUG_RX_PIN 13

// ============================================================================
// APP FEATURES
// ============================================================================
#define FEATURE_UART_BRIDGE 1           // UART bridge to ESP32
#define FEATURE_FEEDBACK 1              // Per-player rumble/LED feedback

// ============================================================================
// APP INTERFACE (OS calls these)
// ============================================================================
void app_init(void);                    // Called after OS init

#endif // APP_USB2UART_H
