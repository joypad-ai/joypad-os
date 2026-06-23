// app.h - PCE2USB App Manifest
// PCEngine / TurboGrafx-16 controller to USB HID gamepad adapter
//
// This app reads native PCEngine controllers (2-button standard pad or
// 6-button Avenue Pad 6) and outputs a USB HID gamepad.

#ifndef APP_PCE2USB_H
#define APP_PCE2USB_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "PCE2USB"
#define APP_DESCRIPTION "PCEngine controller to USB HID gamepad adapter"
#define APP_AUTHOR "Robert Dale Smith"

// ============================================================================
// CORE DEPENDENCIES
// ============================================================================

// Output drivers
#define REQUIRE_USB_DEVICE 1
#define USB_OUTPUT_PORTS 1              // Single USB gamepad output

// Services
#define REQUIRE_PLAYER_MANAGEMENT 1

// ============================================================================
// PIN CONFIGURATION
// ============================================================================
// PCEngine controller pins (directly from controller port).
// SEL and CLR are MCU outputs; D0..D3 are MCU inputs and must be four
// consecutive GPIOs starting at PCE_PIN_D0.
#define PCE_PIN_SEL 5    // SEL - output to controller
#define PCE_PIN_CLR 6    // CLR - output to controller
#define PCE_PIN_D0  8    // D0..D3 - inputs from controller (8,9,10,11)

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_SIMPLE   // Simple 1:1 (PCE → USB)
#define MERGE_MODE MERGE_ALL

// ============================================================================
// PLAYER MANAGEMENT
// ============================================================================
#define PLAYER_SLOT_MODE PLAYER_SLOT_FIXED  // Fixed slots (no shifting)
#define MAX_PLAYER_SLOTS 1                   // Single player
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

#endif // APP_PCE2USB_H
