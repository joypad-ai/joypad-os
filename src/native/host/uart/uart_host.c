// uart_host.c - UART Host Implementation
//
// Receives controller inputs from a remote device over UART and submits
// them to the router. The remote treats this UART link as a synthetic
// controller bus (joypad-mcp, sibling boards, etc.).

#include "uart_host.h"
#include "core/uart/uart_protocol.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
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

// Statistics
static uint32_t rx_count = 0;
static uint32_t error_count = 0;
static uint32_t crc_errors = 0;
static uint32_t last_rx_time = 0;

// Callbacks
static uart_host_profile_callback_t profile_callback = NULL;
static uart_host_mode_callback_t output_mode_callback = NULL;

// IRQ-driven RX ring buffer. Without this, blocking calls elsewhere in
// the main loop (e.g. JoyWing's seesaw I2C polling, ~6 ops × ~ms each)
// can outrun the 32-byte UART RX FIFO at 115200 baud (~3ms to fill) and
// drop incoming MCP packets. With it, an IRQ drains the FIFO into a
// software buffer that survives any amount of main-loop blocking.
#define RX_RING_SIZE 1024
static volatile uint8_t rx_ring[RX_RING_SIZE];
static volatile uint16_t rx_ring_head = 0;  // written by ISR
static volatile uint16_t rx_ring_tail = 0;  // read by task
static volatile uint32_t rx_ring_overflow = 0;

// ============================================================================
// PACKET PROCESSING
// ============================================================================

// Process a complete received packet
static void process_packet(uint8_t type, const uint8_t* payload, uint8_t len)
{
    printf("[uart_host] rx type=0x%02X len=%u\n", type, (unsigned)len);
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
            } else {
                static uint32_t junk_count = 0;
                static uint32_t last_junk_log = 0;
                junk_count++;
                uint32_t now = to_ms_since_boot(get_absolute_time());
                if (now - last_junk_log > 1000) {
                    printf("[uart_host] junk: %lu non-sync bytes (last=0x%02X)\n",
                           (unsigned long)junk_count, (unsigned)byte);
                    last_junk_log = now;
                }
            }
            break;

        case RX_STATE_LENGTH:
            rx_length = byte;
            rx_buffer[rx_index++] = byte;
            if (rx_length > UART_PROTOCOL_MAX_PAYLOAD) {
                printf("[uart_host] bad length %u (max %u)\n",
                       (unsigned)rx_length, (unsigned)UART_PROTOCOL_MAX_PAYLOAD);
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
                printf("[uart_host] CRC fail type=0x%02X len=%u got=0x%02X want=0x%02X\n",
                       (unsigned)rx_type, (unsigned)rx_length,
                       (unsigned)received_crc, (unsigned)calculated_crc);
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

// IRQ handler — drain the hardware FIFO into the software ring buffer.
// Runs even when the main loop is blocked in I2C, so MCP packets don't
// get lost during JoyWing seesaw polling. Kept tiny — no printf, no
// state-machine work, just byte copy + head advance.
static void __not_in_flash_func(uart_host_rx_isr)(void)
{
    while (uart_is_readable(uart_port)) {
        uint8_t b = (uint8_t)uart_get_hw(uart_port)->dr;
        uint16_t next = (uint16_t)((rx_ring_head + 1) % RX_RING_SIZE);
        if (next == rx_ring_tail) {
            rx_ring_overflow++;        // ring full — byte dropped
        } else {
            rx_ring[rx_ring_head] = b;
            rx_ring_head = next;
        }
    }
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

    // Wire up IRQ-driven RX. UART0_IRQ vs UART1_IRQ depending on instance.
    int irq = (uart_port == uart0) ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(irq, uart_host_rx_isr);
    irq_set_enabled(irq, true);
    // Enable RX + RX-timeout IRQs (fires on FIFO threshold OR idle line)
    uart_set_irqs_enabled(uart_port, true, false);

    // Initialize state
    rx_state = RX_STATE_SYNC;
    rx_index = 0;
    rx_ring_head = rx_ring_tail = 0;
    rx_ring_overflow = 0;

    initialized = true;
    printf("[uart_host] Initialization complete (IRQ-driven RX, ring=%u)\n",
           (unsigned)RX_RING_SIZE);
}

void uart_host_task(void)
{
    if (!initialized) return;

    // Drain the software ring buffer (filled by uart_host_rx_isr).
    // Process each byte through the protocol state machine. The buffer
    // survives main-loop stalls (e.g. seesaw I2C ops) up to RX_RING_SIZE
    // bytes — beyond that, ISR increments rx_ring_overflow.
    static uint32_t last_overflow_log = 0;
    while (rx_ring_tail != rx_ring_head) {
        uint8_t b = rx_ring[rx_ring_tail];
        rx_ring_tail = (uint16_t)((rx_ring_tail + 1) % RX_RING_SIZE);
        process_rx_byte(b);
    }
    // Periodic overflow notice (1Hz max) — should be 0 in normal operation
    if (rx_ring_overflow > 0) {
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_overflow_log > 1000) {
            printf("[uart_host] ring overflow: %lu bytes dropped\n",
                   (unsigned long)rx_ring_overflow);
            rx_ring_overflow = 0;
            last_overflow_log = now;
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

