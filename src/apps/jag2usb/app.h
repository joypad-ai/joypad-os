// app.h - JAG2USB App Manifest
// Atari Jaguar controller to USB HID gamepad adapter
//
// This app reads a native Jaguar controller (standard 3-button pad or Pro
// Controller) and outputs a USB HID gamepad. The 12-key keypad is emitted
// as HID keyboard numpad keys (SInput mode's composite keyboard interface).
// Hold Pause+Option 2s to toggle Pro Controller mode (kp 7/8/9/4/6 become
// X/Y/Z/L/R gamepad buttons).

#ifndef APP_JAG2USB_H
#define APP_JAG2USB_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "JAG2USB"
#define APP_DESCRIPTION "Atari Jaguar controller to USB HID gamepad adapter"
#define APP_AUTHOR "Robert Dale Smith"

// ============================================================================
// CORE DEPENDENCIES
// ============================================================================

// Output drivers
#define REQUIRE_USB_DEVICE 1
#define USB_OUTPUT_PORTS 1

// Services
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// PIN CONFIGURATION
// ============================================================================
// Jaguar controller DE-15 pins. J0..J3 are MCU outputs (active-low column
// selects); B0, B1, J8..J11 are MCU inputs with pull-ups. Feed the pad's 5V
// pin (DE-15 pin 7) from 3.3V — the pad is a passive matrix, no shifters.
// Matches the RetroFrog usb2jag wiring except J11: the KB2040 doesn't break
// out GP11, so J11 lives on GP26 (A0; also present on Pico/Pico W).
#define JAG_PIN_J0   2    // DE-15 pin 4
#define JAG_PIN_J1   3    // DE-15 pin 3
#define JAG_PIN_J2   4    // DE-15 pin 2
#define JAG_PIN_J3   5    // DE-15 pin 1
#define JAG_PIN_B0   6    // DE-15 pin 6
#define JAG_PIN_B1   7    // DE-15 pin 10
#define JAG_PIN_J8   8    // DE-15 pin 14
#define JAG_PIN_J9   9    // DE-15 pin 13
#define JAG_PIN_J10  10   // DE-15 pin 12
#define JAG_PIN_J11  26   // DE-15 pin 11 (A0 — GP11 not exposed on KB2040)

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_MERGE
#define MERGE_MODE MERGE_BLEND

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_FIXED
#define MAX_PLAYER_SLOTS 1                  // single pad (Team Tap is future work)
#define AUTO_ASSIGN_ON_PRESS 1

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "ada_kb2040"                  // KB2040 default
#define CPU_OVERCLOCK_KHZ 0                 // No overclock needed
#define UART_DEBUG 1

// ============================================================================
// APP INTERFACE
// ============================================================================
void app_init(void);
void app_task(void);

#endif // APP_JAG2USB_H
