// uart_host.c - UART Host Implementation
//
// Receives controller inputs from a remote device over UART and submits
// them to the router. Supports both normal mode (inputs go to router)
// and AI blend mode (inputs can blend with existing player inputs).

#include "uart_host.h"
#include "core/uart/uart_protocol.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// INTERNAL STATE
// ============================================================================

static bool initialized = false;
static uart_inst_t* uart_port = UART_HOST_PERIPHERAL;
static uart_host_mode_t host_mode = UART_HOST_MODE_NORMAL;

// Receive state machine
typedef enum {
    RX_STATE_SYNC,              // Waiting for sync byte
    RX_STATE_LENGTH,            // Reading length byte
    RX_STATE_TYPE,              // Reading packet type
    RX_STATE_PAYLOAD,           // Reading payload
    RX_STATE_CRC,               // Reading CRC
} rx_state_t;

static rx_state_t rx_state = RX_STATE_SYNC;
static uint8_t rx_buffer[UART_PROTOCOL_MAX_PAYLOAD + UART_OVERHEAD];
static uint8_t rx_index = 0;
static uint8_t rx_length = 0;
static uint8_t rx_type = 0;

// AI injection state per player
typedef struct {
    uart_blend_mode_t blend_mode;
    input_event_t injection;
    uint8_t duration_frames;
    bool active;
} ai_injection_t;

static ai_injection_t ai_injections[UART_HOST_MAX_PLAYERS];

// Statistics
static uint32_t rx_count = 0;
static uint32_t error_count = 0;
static uint32_t crc_errors = 0;
static uint32_t last_rx_time = 0;

// Callbacks
static uart_host_profile_callback_t profile_callback = NULL;
static uart_host_mode_callback_t output_mode_callback = NULL;

// ============================================================================
// PACKET PROCESSING
// ============================================================================

// Process a complete received packet
static void process_packet(uint8_t type, const uint8_t* payload, uint8_t len)
{
    switch (type) {
        case UART_PKT_NOP:
            // Keepalive, nothing to do
            break;

        case UART_PKT_PING:
            // TODO: Send PONG response via uart_device
            break;

        case UART_PKT_INPUT_EVENT: {
            if (len < sizeof(uart_input_event_t)) break;

            const uart_input_event_t* evt = (const uart_input_event_t*)payload;

            if (evt->player_index >= UART_HOST_MAX_PLAYERS) break;

            // Build input event
            input_event_t event;
            init_input_event(&event);

            // Use 0xD0+ range for UART inputs (0xD0-0xD7)
            event.dev_addr = 0xD0 + evt->player_index;
            event.instance = 0;
            event.type = evt->device_type;
            event.buttons = evt->buttons;
            event.analog[ANALOG_LX] = evt->analog[0];
            event.analog[ANALOG_LY] = evt->analog[1];
            event.analog[ANALOG_RX] = evt->analog[2];
            event.analog[ANALOG_RY] = evt->analog[3];
            event.analog[ANALOG_L2] = evt->analog[4];
            event.analog[ANALOG_R2] = evt->analog[5];
            event.delta_x = evt->delta_x;
            event.delta_y = evt->delta_y;

            if (host_mode == UART_HOST_MODE_NORMAL) {
                // Submit directly to router like USB/native inputs
                router_submit_input(&event);
            }
            // In AI_BLEND mode, inputs are stored and retrieved via uart_host_get_injection()
            break;
        }

        case UART_PKT_INPUT_CONNECT: {
            if (len < sizeof(uart_connect_event_t)) break;

            const uart_connect_event_t* conn = (const uart_connect_event_t*)payload;
            printf("[uart_host] Remote player %d connected (type=%d, VID=%04X, PID=%04X)\n",
                   conn->player_index, conn->device_type, conn->vid, conn->pid);
            break;
        }

        case UART_PKT_INPUT_DISCONNECT: {
            if (len < sizeof(uart_disconnect_event_t)) break;

            const uart_disconnect_event_t* disc = (const uart_disconnect_event_t*)payload;
            printf("[uart_host] Remote player %d disconnected\n", disc->player_index);

            // Clear any AI injection for this player
            if (disc->player_index < UART_HOST_MAX_PLAYERS) {
                ai_injections[disc->player_index].active = false;
                ai_injections[disc->player_index].blend_mode = UART_BLEND_OFF;
            }
            break;
        }

        case UART_PKT_AI_INJECT: {
            if (len < sizeof(uart_ai_inject_t)) break;

            const uart_ai_inject_t* inject = (const uart_ai_inject_t*)payload;

            if (inject->player_index >= UART_HOST_MAX_PLAYERS) break;

            ai_injection_t* ai = &ai_injections[inject->player_index];

            ai->blend_mode = inject->blend_mode;
            ai->active = (inject->blend_mode != UART_BLEND_OFF &&
                         inject->blend_mode != UART_BLEND_OBSERVE);
            ai->duration_frames = inject->duration_frames;

            // Convert to input_event_t
            init_input_event(&ai->injection);
            ai->injection.buttons = inject->buttons;
            ai->injection.analog[ANALOG_LX] = inject->analog[0];
            ai->injection.analog[ANALOG_LY] = inject->analog[1];
            ai->injection.analog[ANALOG_RX] = inject->analog[2];
            ai->injection.analog[ANALOG_RY] = inject->analog[3];
            ai->injection.analog[ANALOG_L2] = inject->analog[4];
            ai->injection.analog[ANALOG_R2] = inject->analog[5];
            break;
        }

        case UART_PKT_AI_BLEND_MODE: {
            if (len < sizeof(uart_blend_mode_cmd_t)) break;

            const uart_blend_mode_cmd_t* cmd = (const uart_blend_mode_cmd_t*)payload;

            if (cmd->player_index >= UART_HOST_MAX_PLAYERS) break;

            ai_injections[cmd->player_index].blend_mode = cmd->blend_mode;
            ai_injections[cmd->player_index].active =
                (cmd->blend_mode != UART_BLEND_OFF && cmd->blend_mode != UART_BLEND_OBSERVE);
            break;
        }

        case UART_PKT_SET_PROFILE: {
            if (len >= 1 && profile_callback) {
                profile_callback(payload[0]);
            }
            break;
        }

        case UART_PKT_SET_MODE: {
            if (len >= 1 && output_mode_callback) {
                output_mode_callback(payload[0]);
            }
            break;
        }

        case UART_PKT_VERSION: {
            if (len >= sizeof(uart_version_t)) {
                const uart_version_t* ver = (const uart_version_t*)payload;
                printf("[uart_host] Remote version: %d.%d.%d (board=%d, features=0x%08lX)\n",
                       ver->major, ver->minor, ver->patch, ver->board_type, ver->features);
            }
            break;
        }

        default:
            // Unknown packet type
            error_count++;
            break;
    }
}

// ============================================================================
// RECEIVE STATE MACHINE
// ============================================================================

// Process a single received byte through the state machine
static void process_rx_byte(uint8_t byte)
{
    switch (rx_state) {
        case RX_STATE_SYNC:
            if (byte == UART_PROTOCOL_SYNC_BYTE) {
                rx_buffer[0] = byte;
                rx_index = 1;
                rx_state = RX_STATE_LENGTH;
            }
            break;

        case RX_STATE_LENGTH:
            rx_length = byte;
            rx_buffer[rx_index++] = byte;
            if (rx_length > UART_PROTOCOL_MAX_PAYLOAD) {
                // Invalid length, reset
                error_count++;
                rx_state = RX_STATE_SYNC;
            } else {
                rx_state = RX_STATE_TYPE;
            }
            break;

        case RX_STATE_TYPE:
            rx_type = byte;
            rx_buffer[rx_index++] = byte;
            if (rx_length == 0) {
                // No payload, go straight to CRC
                rx_state = RX_STATE_CRC;
            } else {
                rx_state = RX_STATE_PAYLOAD;
            }
            break;

        case RX_STATE_PAYLOAD:
            rx_buffer[rx_index++] = byte;
            if (rx_index >= UART_HEADER_SIZE + rx_length) {
                rx_state = RX_STATE_CRC;
            }
            break;

        case RX_STATE_CRC: {
            uint8_t received_crc = byte;

            // Calculate CRC over length + type + payload
            uint8_t calculated_crc = uart_crc8(&rx_buffer[1], rx_length + 2);

            if (received_crc == calculated_crc) {
                // Valid packet
                rx_count++;
                last_rx_time = to_ms_since_boot(get_absolute_time());
                process_packet(rx_type, &rx_buffer[UART_HEADER_SIZE], rx_length);
            } else {
                // CRC mismatch
                crc_errors++;
                error_count++;
            }

            rx_state = RX_STATE_SYNC;
            break;
        }
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================

void uart_host_init(void)
{
    uart_host_init_pins(UART_HOST_TX_PIN, UART_HOST_RX_PIN, UART_PROTOCOL_BAUD_DEFAULT);
}

void uart_host_init_pins(uint8_t tx_pin, uint8_t rx_pin, uint32_t baud)
{
    printf("[uart_host] Initializing UART host\n");
    printf("[uart_host]   TX=%d, RX=%d, BAUD=%lu\n", tx_pin, rx_pin, baud);

    // Initialize UART
    uart_init(uart_port, baud);

    // Set GPIO functions
    gpio_set_function(tx_pin, GPIO_FUNC_UART);
    gpio_set_function(rx_pin, GPIO_FUNC_UART);

    // Configure UART format: 8N1
    uart_set_format(uart_port, 8, 1, UART_PARITY_NONE);

    // Enable FIFO
    uart_set_fifo_enabled(uart_port, true);

    // Initialize state
    memset(ai_injections, 0, sizeof(ai_injections));
    rx_state = RX_STATE_SYNC;
    rx_index = 0;

    initialized = true;
    printf("[uart_host] Initialization complete\n");
}

void uart_host_task(void)
{
    if (!initialized) return;

    // Process all available bytes from UART
    while (uart_is_readable(uart_port)) {
        uint8_t byte = uart_getc(uart_port);
        process_rx_byte(byte);
    }

    // Decrement injection duration counters
    for (int i = 0; i < UART_HOST_MAX_PLAYERS; i++) {
        if (ai_injections[i].active && ai_injections[i].duration_frames > 0) {
            ai_injections[i].duration_frames--;
            if (ai_injections[i].duration_frames == 0) {
                ai_injections[i].active = false;
            }
        }
    }
}

void uart_host_set_mode(uart_host_mode_t mode)
{
    host_mode = mode;
}

uart_host_mode_t uart_host_get_mode(void)
{
    return host_mode;
}

bool uart_host_get_injection(uint8_t player_index, input_event_t* out)
{
    if (!initialized) return false;
    if (player_index >= UART_HOST_MAX_PLAYERS) return false;

    ai_injection_t* ai = &ai_injections[player_index];

    if (!ai->active) return false;
    if (ai->blend_mode == UART_BLEND_OFF) return false;
    if (ai->blend_mode == UART_BLEND_OBSERVE) return false;

    *out = ai->injection;
    return true;
}

uart_blend_mode_t uart_host_get_blend_mode(uint8_t player_index)
{
    if (!initialized) return UART_BLEND_OFF;
    if (player_index >= UART_HOST_MAX_PLAYERS) return UART_BLEND_OFF;

    return ai_injections[player_index].blend_mode;
}

bool uart_host_is_connected(void)
{
    if (!initialized) return false;

    // Consider connected if we received valid data in last 5 seconds
    uint32_t now = to_ms_since_boot(get_absolute_time());
    return (now - last_rx_time) < 5000;
}

uint32_t uart_host_get_rx_count(void) { return rx_count; }
uint32_t uart_host_get_error_count(void) { return error_count; }
uint32_t uart_host_get_crc_errors(void) { return crc_errors; }

void uart_host_set_profile_callback(uart_host_profile_callback_t callback)
{
    profile_callback = callback;
}

void uart_host_set_output_mode_callback(uart_host_mode_callback_t callback)
{
    output_mode_callback = callback;
}

