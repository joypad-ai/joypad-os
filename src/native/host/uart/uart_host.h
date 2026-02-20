// uart_host.h - UART Host Interface
//
// Receives controller inputs from a remote device (ESP32, another RP2040, etc.)
// over UART and submits them to the router. This makes the RP2040 the "host"
// of inputs arriving via UART.
//
// Use cases:
//   - ESP32-S3 AI platform sending AI-controlled inputs
//   - Another Joypad board sharing its USB controller inputs
//   - Any external MCU sending controller data
//
// The UART host and device (uart_device) can share the same UART peripheral
// for bidirectional communication - inputs come in, feedback goes out.

#ifndef UART_HOST_H
#define UART_HOST_H

#include <stdint.h>
#include <stdbool.h>
#include "core/uart/uart_protocol.h"
#include "core/input_event.h"

// ============================================================================
// CONFIGURATION
// ============================================================================

// Default UART pins (Qwiic cable: SDA→TX, SCL→RX)
#ifndef UART_HOST_TX_PIN
#define UART_HOST_TX_PIN        4       // TX (to remote RX)
#endif

#ifndef UART_HOST_RX_PIN
#define UART_HOST_RX_PIN        5       // RX (from remote TX)
#endif

#ifndef UART_HOST_PERIPHERAL
#define UART_HOST_PERIPHERAL    uart1   // UART peripheral
#endif

// Maximum players that can be received from UART
#define UART_HOST_MAX_PLAYERS   8

// ============================================================================
// UART HOST MODES
// ============================================================================

typedef enum {
    UART_HOST_MODE_OFF = 0,         // UART host disabled
    UART_HOST_MODE_NORMAL,          // Submit UART inputs to router (like USB/native)
    UART_HOST_MODE_AI_BLEND,        // Blend UART inputs with existing player inputs
} uart_host_mode_t;

// AI blend modes are defined in uart_protocol.h (uart_blend_mode_t)

// ============================================================================
// PUBLIC API
// ============================================================================

// Initialize UART host with default pins
void uart_host_init(void);

// Initialize UART host with custom pins
void uart_host_init_pins(uint8_t tx_pin, uint8_t rx_pin, uint32_t baud);

// UART host task - call from main loop
// Receives and processes incoming packets, submits inputs to router
void uart_host_task(void);

// Set operating mode
void uart_host_set_mode(uart_host_mode_t mode);
uart_host_mode_t uart_host_get_mode(void);

// Get AI injection for a player (when in AI_BLEND mode)
// Called by router to blend AI inputs with player inputs
// Returns true if AI has input to inject for this player
bool uart_host_get_injection(uint8_t player_index, input_event_t* out);

// Get AI blend mode for a player
uart_blend_mode_t uart_host_get_blend_mode(uint8_t player_index);

// Check if remote device is connected (received valid packet recently)
bool uart_host_is_connected(void);

// Get statistics
uint32_t uart_host_get_rx_count(void);
uint32_t uart_host_get_error_count(void);
uint32_t uart_host_get_crc_errors(void);

// ============================================================================
// CALLBACKS
// ============================================================================

// Callback when remote requests profile change
typedef void (*uart_host_profile_callback_t)(uint8_t profile_index);
void uart_host_set_profile_callback(uart_host_profile_callback_t callback);

// Callback when remote requests output mode change
typedef void (*uart_host_mode_callback_t)(uint8_t mode);
void uart_host_set_output_mode_callback(uart_host_mode_callback_t callback);

#endif // UART_HOST_H
