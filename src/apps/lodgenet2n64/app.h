// app.h - LodgeNet2N64 App Manifest
// LodgeNet hotel gaming controller to N64 console adapter
//
// Reads LodgeNet N64/GameCube/SNES controllers via proprietary serial
// protocol and outputs to N64 console via joybus PIO protocol.

#ifndef APP_LODGENET2N64_H
#define APP_LODGENET2N64_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "LodgeNet2N64"
#define APP_DESCRIPTION "LodgeNet controller to N64 console adapter"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES
// ============================================================================

// Input drivers
#define REQUIRE_NATIVE_LODGENET_HOST 1
#define REQUIRE_USB_HOST 0
#define MAX_USB_DEVICES 0

// Output drivers
#define REQUIRE_USB_DEVICE 0
#define REQUIRE_NATIVE_N64_OUTPUT 1
#define N64_OUTPUT_PORTS 1

// Services
#define REQUIRE_PLAYER_MANAGEMENT 1
#define REQUIRE_PROFILE_SYSTEM 1

// ============================================================================
// PIN CONFIGURATION
// ============================================================================
// LodgeNet RJ11 connector
//   Pin 1: +5V (from VBUS)
//   Pin 2: CLOCK -> GPIO output
//   Pin 3: DATA  -> GPIO input
//   Pin 4: GND
#define LODGENET_PIN_CLOCK  3   // CLK1 output to controller
#define LODGENET_PIN_DATA   2   // Data input from controller
#define LODGENET_PIN_VCC    4   // VCC output (drives controller power)
#define LODGENET_PIN_CLOCK2 5   // CLK2 output (SNES SR protocol only)

// N64 joybus data line
#define N64_DATA_PIN 7

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_MERGE
#define MERGE_MODE MERGE_BLEND

// Input transformations
#define TRANSFORM_FLAGS 0

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS 1
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "rpi_pico"
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

#endif // APP_LODGENET2N64_H
