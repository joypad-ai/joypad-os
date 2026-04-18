// app.h - WII2GC App Manifest
// Wii Nunchuck / Classic / Classic Pro to GameCube adapter.
//
// Input: Wii extension via I2C (STEMMA QT at GP12/GP13 on KB2040)
// Output: GameCube joybus (same pins / timing as usb2gc)

#ifndef APP_WII2GC_H
#define APP_WII2GC_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "wii2gc"
#define APP_VERSION "0.1.0"
#define APP_DESCRIPTION "Wii Nunchuck/Classic to GameCube adapter"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES
// ============================================================================
#define REQUIRE_NATIVE_WII_HOST 1
#define WII_MAX_CONTROLLERS 2

#define REQUIRE_NATIVE_GAMECUBE_OUTPUT 1
#define GAMECUBE_OUTPUT_PORTS 1

// CDC config mode when the GameCube isn't present (no 3V3 on the data pin)
#define REQUIRE_USB_DEVICE 1

#define REQUIRE_PLAYER_MANAGEMENT 1
#define REQUIRE_PROFILE_SYSTEM 1
#define REQUIRE_FLASH_SETTINGS 1

// ============================================================================
// PIN CONFIGURATION (Wii side — GC side uses the usb2gc defaults)
// ============================================================================
#define WII_PIN_SDA     12
#define WII_PIN_SCL     13
#define WII_PIN_SDA2    2
#define WII_PIN_SCL2    3
#define WII_I2C_FREQ_HZ 50000

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_SIMPLE
#define MERGE_MODE   MERGE_ALL

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE     PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS     1
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "ada_kb2040"
#define CPU_OVERCLOCK_KHZ 130000   // Required for GameCube joybus timing
#define UART_DEBUG 1

void app_init(void);
void app_task(void);

#endif // APP_WII2GC_H
