// app.h - SNES23DO App Manifest
// SNES/NES controller to 3DO adapter
//
// This app reads native SNES/NES controllers and outputs to 3DO.
// Supports SNES controller, NES controller, SNES mouse, and Xband keyboard.

#ifndef APP_SNES23DO_H
#define APP_SNES23DO_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "SNES23DO"
#define APP_VERSION "1.0.0"
#define APP_DESCRIPTION "SNES/NES controller to 3DO adapter"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES
// ============================================================================

// Input drivers - Native SNES host (NOT USB)
#define REQUIRE_NATIVE_SNES_HOST 1
#define SNES_MAX_CONTROLLERS 1          // Single SNES port for now

// Output drivers
#define REQUIRE_NATIVE_3DO_OUTPUT 1
#define TDO_OUTPUT_PORTS 1              // Single 3DO output port

// Services
#define REQUIRE_PLAYER_MANAGEMENT 1
#define REQUIRE_PROFILE_SYSTEM 1        // Button mapping profiles

// ============================================================================
// PIN CONFIGURATION
// ============================================================================
// SNES controller pins (directly from controller port)
#define SNES_PIN_CLOCK  2   // CLK - output to controller
#define SNES_PIN_LATCH  3   // LATCH - output to controller
#define SNES_PIN_DATA0  4   // DATA - input from controller
#define SNES_PIN_DATA1  5   // DATA1 - input (for multitap/keyboard)
#define SNES_PIN_IOBIT  6   // IOBIT - output (for mouse/keyboard)

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_SIMPLE   // Simple 1:1 (SNES → 3DO)
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
#define BOARD "waveshare_rp2040_zero"       // Same as USB2-3DO
#define CPU_OVERCLOCK_KHZ 0                 // No overclock needed
#define UART_DEBUG 1

// ============================================================================
// APP FEATURES
// ============================================================================
#define FEATURE_MOUSE_SUPPORT 1             // SNES mouse support
#define FEATURE_PROFILE_SWITCHING 1         // Runtime button mapping

// ============================================================================
// APP INTERFACE
// ============================================================================
void app_init(void);

#endif // APP_SNES23DO_H
