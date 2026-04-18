// app.h - WII2USB App Manifest
// Wii Nunchuck / Classic Controller to USB HID gamepad adapter.
//
// Cut a Wii extension cable in half and wire it directly to the Pico:
//   Red    -> 3V3
//   Black  -> GND
//   White  -> SCL pin below
//   Green  -> SDA pin below
// External 1.8 kΩ–4.7 kΩ pull-ups to 3.3 V are required on SDA and SCL.
// Do NOT power from VBUS/5V — Wii extensions are 3.3 V parts only.

#ifndef APP_WII2USB_H
#define APP_WII2USB_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "WII2USB"
#define APP_VERSION "0.1.0"
#define APP_DESCRIPTION "Wii Nunchuck/Classic controller to USB HID adapter"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES
// ============================================================================
#define REQUIRE_NATIVE_WII_HOST 1
#define WII_MAX_CONTROLLERS 2

#define REQUIRE_USB_DEVICE 1
#define USB_OUTPUT_PORTS 1

#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// PIN CONFIGURATION
// ============================================================================
// KB2040 defaults: GP12/GP13 are the STEMMA QT / QWIIC connector pair,
// so a Wii-extension-to-QWIIC adapter plugs in solderless. Since the
// transport is HW I2C, any GPIO pair works — override as needed.
#define WII_PIN_SDA     12
#define WII_PIN_SCL     13
// Second I2C bus for dual nunchuck mode. GP2/GP3 use I2C1.
// Wire a second extension cable to these pins (with pull-ups).
// When two nunchucks are detected: left = stick+C/Z, right = stick+B3/B4.
#define WII_PIN_SDA2    2
#define WII_PIN_SCL2    3
// I2C clock. Nunchuck spec is 400 kHz; clone controllers and Qwiic-adapter
// chains often misbehave above ~100 kHz, so default to 50 kHz for max
// robustness. The gp2040-ce team ran 400 kHz successfully; WiiChuck uses
// 10 kHz on ESP32. 50 kHz is a conservative middle ground.
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
#define CPU_OVERCLOCK_KHZ 0
#define UART_DEBUG 1

// ============================================================================
// APP INTERFACE
// ============================================================================
void app_init(void);

#endif // APP_WII2USB_H
