// uart_host_nrf.c - UART Host Interface for nRF52840 (Zephyr)
//
// Zephyr port of native/host/uart/uart_host.c. Same protocol/state machine
// and public API (uart_host.h); only the UART transport is Zephyr instead
// of pico-SDK. Runs on the console UART (uart0) — logs are lines, MCP
// frames are binary with SYNC+CRC8 framing, exactly like the RP2040 build
// which shares one UART for both over USB CDC.

#include "uart_host.h"
#include "core/uart/uart_protocol.h"
#include "core/router/router.h"
#include "core/input_event.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <string.h>
#include <stdio.h>

// ============================================================================
// INTERNAL STATE
// ============================================================================

static bool initialized = false;
static const struct device *uart_dev;
static uart_host_mode_t host_mode = UART_HOST_MODE_NORMAL;

// Receive state machine (identical framing to the pico-SDK port)
typedef enum {
    RX_STATE_SYNC,
    RX_STATE_LENGTH,
    RX_STATE_TYPE,
    RX_STATE_PAYLOAD,
    RX_STATE_CRC,
} rx_state_t;

static rx_state_t rx_state = RX_STATE_SYNC;
static uint8_t rx_buffer[UART_PROTOCOL_MAX_PAYLOAD + UART_OVERHEAD];
static uint8_t rx_index = 0;
static uint8_t rx_length = 0;
static uint8_t rx_type = 0;

static uint32_t rx_count = 0;
static uint32_t error_count = 0;
static uint32_t crc_errors = 0;
static uint32_t last_rx_time = 0;

static uart_host_profile_callback_t profile_callback = NULL;
static uart_host_mode_callback_t output_mode_callback = NULL;

// ISR-driven RX ring buffer, same rationale as the pico-SDK port: the
// main loop can stall (I2C polling, display refresh) longer than the
// hardware FIFO can absorb at 115200 baud, so drain it in the UART IRQ.
#define RX_RING_SIZE 1024
static volatile uint8_t rx_ring[RX_RING_SIZE];
static volatile uint16_t rx_ring_head = 0;  // written by ISR
static volatile uint16_t rx_ring_tail = 0;  // read by task
static volatile uint32_t rx_ring_overflow = 0;

// ============================================================================
// PACKET PROCESSING
// ============================================================================

static void process_packet(uint8_t type, const uint8_t *payload, uint8_t len)
{
    printf("[uart_host] rx type=0x%02X len=%u\n", type, (unsigned)len);
    switch (type) {
        case UART_PKT_NOP:
            break;

        case UART_PKT_PING:
            // TODO: Send PONG response via uart_device
            break;

        case UART_PKT_INPUT_EVENT: {
            if (len < sizeof(uart_input_event_t)) break;

            const uart_input_event_t *evt = (const uart_input_event_t *)payload;
            if (evt->player_index >= UART_HOST_MAX_PLAYERS) break;

            input_event_t event;
            init_input_event(&event);

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
                router_submit_input(&event);
            }
            break;
        }

        case UART_PKT_INPUT_CONNECT: {
            if (len < sizeof(uart_connect_event_t)) break;
            const uart_connect_event_t *conn = (const uart_connect_event_t *)payload;
            printf("[uart_host] Remote player %d connected (type=%d, VID=%04X, PID=%04X)\n",
                   conn->player_index, conn->device_type, conn->vid, conn->pid);
            break;
        }

        case UART_PKT_INPUT_DISCONNECT: {
            if (len < sizeof(uart_disconnect_event_t)) break;
            const uart_disconnect_event_t *disc = (const uart_disconnect_event_t *)payload;
            printf("[uart_host] Remote player %d disconnected\n", disc->player_index);
            break;
        }

        case UART_PKT_SET_PROFILE:
            if (len >= 1 && profile_callback) {
                profile_callback(payload[0]);
            }
            break;

        case UART_PKT_SET_MODE:
            if (len >= 1 && output_mode_callback) {
                output_mode_callback(payload[0]);
            }
            break;

        case UART_PKT_VERSION:
            if (len >= sizeof(uart_version_t)) {
                const uart_version_t *ver = (const uart_version_t *)payload;
                printf("[uart_host] Remote version: %d.%d.%d (board=%d, features=0x%08lX)\n",
                       ver->major, ver->minor, ver->patch, ver->board_type,
                       (unsigned long)ver->features);
            }
            break;

        default:
            error_count++;
            break;
    }
}

// ============================================================================
// RECEIVE STATE MACHINE
// ============================================================================

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
                uint32_t now = k_uptime_get_32();
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
            rx_state = (rx_length == 0) ? RX_STATE_CRC : RX_STATE_PAYLOAD;
            break;

        case RX_STATE_PAYLOAD:
            rx_buffer[rx_index++] = byte;
            if (rx_index >= UART_HEADER_SIZE + rx_length) {
                rx_state = RX_STATE_CRC;
            }
            break;

        case RX_STATE_CRC: {
            uint8_t received_crc = byte;
            uint8_t calculated_crc = uart_crc8(&rx_buffer[1], rx_length + 2);

            if (received_crc == calculated_crc) {
                rx_count++;
                last_rx_time = k_uptime_get_32();
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
// UART IRQ — drain the hardware FIFO into the software ring buffer
// ============================================================================

static void uart_host_isr(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);
    while (uart_irq_update(dev) && uart_irq_rx_ready(dev)) {
        uint8_t b;
        while (uart_fifo_read(dev, &b, 1) == 1) {
            uint16_t next = (uint16_t)((rx_ring_head + 1) % RX_RING_SIZE);
            if (next == rx_ring_tail) {
                rx_ring_overflow++;
            } else {
                rx_ring[rx_ring_head] = b;
                rx_ring_head = next;
            }
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

// tx_pin/rx_pin are unused on nRF — the port always uses the board's
// devicetree-configured UART_HOST_PERIPHERAL (console uart0 by default,
// pinned via the board overlay), matching how the pico-SDK port's pins
// are themselves just GPIO_FUNC_UART assignments on a fixed peripheral.
void uart_host_init_pins(uint8_t tx_pin, uint8_t rx_pin, uint32_t baud)
{
    ARG_UNUSED(tx_pin);
    ARG_UNUSED(rx_pin);
    ARG_UNUSED(baud);

    printf("[uart_host] Initializing UART host (nRF/Zephyr)\n");

    uart_dev = DEVICE_DT_GET(DT_NODELABEL(UART_HOST_PERIPHERAL));
    if (!device_is_ready(uart_dev)) {
        printf("[uart_host] UART device not ready\n");
        return;
    }

    rx_state = RX_STATE_SYNC;
    rx_index = 0;
    rx_ring_head = rx_ring_tail = 0;
    rx_ring_overflow = 0;

    uart_irq_callback_user_data_set(uart_dev, uart_host_isr, NULL);
    uart_irq_rx_enable(uart_dev);

    initialized = true;
    printf("[uart_host] Initialization complete (IRQ-driven RX, ring=%u)\n",
           (unsigned)RX_RING_SIZE);
}

void uart_host_task(void)
{
    if (!initialized) return;

    static uint32_t last_overflow_log = 0;
    while (rx_ring_tail != rx_ring_head) {
        uint8_t b = rx_ring[rx_ring_tail];
        rx_ring_tail = (uint16_t)((rx_ring_tail + 1) % RX_RING_SIZE);
        process_rx_byte(b);
    }
    if (rx_ring_overflow > 0) {
        uint32_t now = k_uptime_get_32();
        if (now - last_overflow_log > 1000) {
            printf("[uart_host] ring overflow: %lu bytes dropped\n",
                   (unsigned long)rx_ring_overflow);
            rx_ring_overflow = 0;
            last_overflow_log = now;
        }
    }
}

void uart_host_set_mode(uart_host_mode_t mode) { host_mode = mode; }
uart_host_mode_t uart_host_get_mode(void) { return host_mode; }

bool uart_host_is_connected(void)
{
    if (!initialized) return false;
    uint32_t now = k_uptime_get_32();
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
