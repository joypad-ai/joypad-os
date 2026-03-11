// app.h - Nuon Serial Adapter App Manifest
// Polyface serial device for Nuon homebrew debug logging
//
// Plugs into Nuon controller port, presents as TYPE=0x04 (SERIAL).
// Nuon homebrew writes debug output to SDATA register, RP2040 relays
// it over USB CDC to a PC terminal. Bidirectional.

#ifndef APP_NUONSERIAL_H
#define APP_NUONSERIAL_H

// ============================================================================
// APP METADATA
// ============================================================================
#define APP_NAME "nuonserial"
#define APP_VERSION "0.1.0"
#define APP_DESCRIPTION "Nuon serial adapter for homebrew debug"
#define APP_AUTHOR "RobertDaleSmith"

// ============================================================================
// CORE DEPENDENCIES
// ============================================================================

// No USB host input — this is a serial bridge, not a controller adapter
#define REQUIRE_USB_HOST 0

// Output: Nuon Polyface serial device on controller port
#define REQUIRE_NATIVE_NUON_OUTPUT 1

// USB device output for CDC serial to PC
#define REQUIRE_USB_DEVICE 1

// Services
#define REQUIRE_FLASH_SETTINGS 1

// ============================================================================
// ROUTING CONFIGURATION
// ============================================================================
#define ROUTING_MODE ROUTING_MODE_SIMPLE

// ============================================================================
// HARDWARE CONFIGURATION
// ============================================================================
#define BOARD "rpi_pico"
#define UART_DEBUG 1

// ============================================================================
// APP INTERFACE
// ============================================================================
void app_init(void);
void app_task(void);

#endif // APP_NUONSERIAL_H
