// uart_device.h - UART Device Interface
//
// Sends controller outputs to a remote device (ESP32, another RP2040, etc.)
// over UART. This makes the RP2040 the "device" that provides controller data.
//
// Use cases:
//   - Sharing USB controller inputs with ESP32-S3 for AI processing
//   - Sharing controller inputs with another Joypad board
//   - Providing controller state to any external MCU
//
// The UART device and host (uart_host) can share the same UART peripheral
// for bidirectional communication - outputs go out, feedback comes in.

#ifndef UART_DEVICE_H
#define UART_DEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "core/uart/uart_protocol.h"
#include "core/input_event.h"

// ============================================================================
// CONFIGURATION
// ============================================================================

// Default UART pins (Qwiic cable: SDA→TX, SCL→RX)
// Should match uart_host pins for bidirectional on same bus
#ifndef UART_DEVICE_TX_PIN
#define UART_DEVICE_TX_PIN        4       // TX (to remote RX)
#endif

#ifndef UART_DEVICE_RX_PIN
#define UART_DEVICE_RX_PIN        5       // RX (from remote TX)
#endif

#ifndef UART_DEVICE_PERIPHERAL
#define UART_DEVICE_PERIPHERAL    uart1   // UART peripheral
#endif

// ============================================================================
// UART DEVICE MODES
// ============================================================================

typedef enum {
    UART_DEVICE_MODE_OFF = 0,       // UART device disabled
    UART_DEVICE_MODE_STREAM,        // Stream all input events continuously
    UART_DEVICE_MODE_ON_CHANGE,     // Only send on state change
    UART_DEVICE_MODE_ON_REQUEST,    // Only send when remote requests
} uart_device_mode_t;

// ============================================================================
// PUBLIC API
// ============================================================================

// Initialize UART device with default pins
void uart_device_init(void);

// Initialize UART device with custom pins
void uart_device_init_pins(uint8_t tx_pin, uint8_t rx_pin, uint32_t baud);

// UART device task - call from main loop
// Processes pending requests and sends queued events
void uart_device_task(void);

// Set operating mode
void uart_device_set_mode(uart_device_mode_t mode);
uart_device_mode_t uart_device_get_mode(void);

// ============================================================================
// OUTPUT INTERFACE (router tap)
// ============================================================================

// Queue an input event for transmission
// Called by router tap when input events occur
void uart_device_queue_input(const input_event_t* event, uint8_t player_index);

// Send player connect notification
void uart_device_send_connect(uint8_t player_index, uint8_t device_type,
                               uint16_t vid, uint16_t pid);

// Send player disconnect notification
void uart_device_send_disconnect(uint8_t player_index);

// ============================================================================
// FEEDBACK HANDLERS
// ============================================================================

// Callback when remote sends rumble command
typedef void (*uart_device_rumble_callback_t)(uint8_t player_index,
                                               uint8_t left_motor,
                                               uint8_t right_motor,
                                               uint16_t duration_ms);
void uart_device_set_rumble_callback(uart_device_rumble_callback_t callback);

// Callback when remote sends LED command
typedef void (*uart_device_led_callback_t)(uint8_t player_index,
                                            uint8_t pattern,
                                            uint8_t r, uint8_t g, uint8_t b);
void uart_device_set_led_callback(uart_device_led_callback_t callback);

// ============================================================================
// STATUS AND DIAGNOSTICS
// ============================================================================

// Check if remote device is connected (received valid packet recently)
bool uart_device_is_connected(void);

// Get statistics
uint32_t uart_device_get_tx_count(void);
uint32_t uart_device_get_rx_count(void);
uint32_t uart_device_get_error_count(void);
uint32_t uart_device_get_queue_drops(void);

// ============================================================================
// PACKET SENDING (for advanced use)
// ============================================================================

// Send a raw packet (type + payload)
void uart_device_send_packet(uint8_t type, const void* payload, uint8_t len);

// Send status response
void uart_device_send_status(void);

// Send version info
void uart_device_send_version(void);

#endif // UART_DEVICE_H
