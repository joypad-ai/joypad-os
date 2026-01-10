// dreamcast_device.c - Dreamcast Maple Bus output interface
// Emulates a Dreamcast controller using PIO for precise timing
//
// Reference: MaplePad by Charlie Cole / mackieks
// https://github.com/mackieks/MaplePad

#include "dreamcast_device.h"
#include "maple.pio.h"
#include "core/output_interface.h"
#include "core/router/router.h"
#include "core/services/players/manager.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/multicore.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// PIO AND DMA CONFIGURATION
// ============================================================================

static PIO maple_tx_pio = pio0;
static PIO maple_rx_pio = pio1;
static uint maple_tx_sm = 0;
static uint maple_rx_sm[3] = {0, 1, 2};

static int tx_dma_channel = -1;

// ============================================================================
// CONTROLLER STATE
// ============================================================================

// Current controller state per port (what we send to Dreamcast)
static dc_controller_state_t dc_state[MAX_PLAYERS];

// Rumble state from Dreamcast (for feedback to USB controllers)
static uint8_t dc_rumble[MAX_PLAYERS];

// RX state machine lookup table for decoding transitions
// Maps 8-bit transition pattern to 4 decoded bits
static uint8_t rx_lookup[256];

// ============================================================================
// PACKET BUFFERS
// ============================================================================

#define MAPLE_RX_BUFFER_SIZE 256
#define MAPLE_TX_BUFFER_SIZE 256

static uint8_t rx_buffer[MAPLE_RX_BUFFER_SIZE];
static uint32_t tx_buffer[MAPLE_TX_BUFFER_SIZE / 4];

// ============================================================================
// DEVICE INFO PACKET (sent in response to DEVICE_INFO command)
// ============================================================================

// Standard Dreamcast controller device info
static const uint8_t controller_device_info[] = {
    // Function type: FT0 (controller) - big endian
    0x00, 0x00, 0x00, 0x01,
    // Function data (3 words, big endian)
    0x00, 0x0f, 0x06, 0xfe,  // Buttons, triggers, analog
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
    // Area code (0xFF = all regions)
    0xff,
    // Connector direction
    0x00,
    // Product name (30 bytes, space padded)
    'D', 'r', 'e', 'a', 'm', 'c', 'a', 's', 't', ' ',
    'C', 'o', 'n', 't', 'r', 'o', 'l', 'l', 'e', 'r',
    ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ',
    // License (60 bytes, space padded)
    'P', 'r', 'o', 'd', 'u', 'c', 'e', 'd', ' ', 'B',
    'y', ' ', 'o', 'r', ' ', 'U', 'n', 'd', 'e', 'r',
    ' ', 'L', 'i', 'c', 'e', 'n', 's', 'e', ' ', 'F',
    'r', 'o', 'm', ' ', 'S', 'E', 'G', 'A', ' ', 'E',
    'N', 'T', 'E', 'R', 'P', 'R', 'I', 'S', 'E', 'S',
    ',', 'L', 'T', 'D', '.', ' ', ' ', ' ', ' ', ' ',
    // Standby power (mA * 10), big endian
    0x01, 0xf4,  // 500 = 50mA
    // Max power (mA * 10), big endian
    0x01, 0xf4,  // 500 = 50mA
};

// ============================================================================
// CRC CALCULATION
// ============================================================================

static uint8_t maple_calc_crc(const uint8_t *data, uint16_t length)
{
    uint8_t crc = 0;
    for (uint16_t i = 0; i < length; i++) {
        crc ^= data[i];
    }
    return crc;
}

// ============================================================================
// RX LOOKUP TABLE INITIALIZATION
// ============================================================================

// Build lookup table for decoding Maple Bus transitions
// The RX state machines capture 2 bits per transition (pin states)
// We need to decode 4 transitions (8 bits) into 4 data bits
static void build_rx_lookup_table(void)
{
    for (int i = 0; i < 256; i++) {
        uint8_t decoded = 0;
        for (int bit = 0; bit < 4; bit++) {
            // Each pair of bits represents pin states after a transition
            // 01 = 0, 10 = 1 (differential encoding)
            uint8_t pair = (i >> (bit * 2)) & 0x03;
            if (pair == 0x02) {  // Pin1=1, Pin5=0 -> data 1
                decoded |= (1 << bit);
            }
            // pair == 0x01 means Pin1=0, Pin5=1 -> data 0
            // Other values indicate errors or idle
        }
        rx_lookup[i] = decoded;
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void dreamcast_init(void)
{
    printf("[DC] Initializing Dreamcast Maple Bus output...\n");

    // Initialize controller states to neutral
    for (int i = 0; i < MAX_PLAYERS; i++) {
        dc_state[i].buttons = 0xFFFF;  // All buttons released (active-low)
        dc_state[i].rt = 0;
        dc_state[i].lt = 0;
        dc_state[i].joy_x = 128;
        dc_state[i].joy_y = 128;
        dc_state[i].joy2_x = 128;
        dc_state[i].joy2_y = 128;
        dc_rumble[i] = 0;
    }

    // Build RX lookup table
    build_rx_lookup_table();

    // Initialize GPIO pins with pull-ups (open-drain bus)
    gpio_init(MAPLE_PIN1);
    gpio_init(MAPLE_PIN5);
    gpio_set_dir(MAPLE_PIN1, GPIO_IN);
    gpio_set_dir(MAPLE_PIN5, GPIO_IN);
    gpio_pull_up(MAPLE_PIN1);
    gpio_pull_up(MAPLE_PIN5);

    // Initialize TX PIO (PIO0)
    uint tx_offset = pio_add_program(maple_tx_pio, &maple_tx_program);
    maple_tx_sm = pio_claim_unused_sm(maple_tx_pio, true);

    // Clock divider: 125MHz / 62.5 = 2MHz Maple Bus clock
    float clock_div = (float)clock_get_hz(clk_sys) / 2000000.0f / 2.0f;

    maple_tx_program_init(maple_tx_pio, maple_tx_sm, tx_offset,
                          MAPLE_PIN1, MAPLE_PIN5, clock_div);

    // Initialize RX PIO (PIO1) - uses 3 state machines
    uint rx_offsets[3];
    rx_offsets[0] = pio_add_program(maple_rx_pio, &maple_rx_triple1_program);
    rx_offsets[1] = pio_add_program(maple_rx_pio, &maple_rx_triple2_program);
    rx_offsets[2] = pio_add_program(maple_rx_pio, &maple_rx_triple3_program);

    maple_rx_triple_program_init(maple_rx_pio, rx_offsets,
                                 MAPLE_PIN1, MAPLE_PIN5, clock_div);

    // Start RX state machines
    for (int i = 0; i < 3; i++) {
        pio_sm_set_enabled(maple_rx_pio, maple_rx_sm[i], true);
    }

    // Setup DMA for TX
    tx_dma_channel = dma_claim_unused_channel(true);
    dma_channel_config tx_config = dma_channel_get_default_config(tx_dma_channel);
    channel_config_set_dreq(&tx_config, pio_get_dreq(maple_tx_pio, maple_tx_sm, true));
    channel_config_set_transfer_data_size(&tx_config, DMA_SIZE_32);
    channel_config_set_read_increment(&tx_config, true);
    channel_config_set_write_increment(&tx_config, false);
    dma_channel_configure(
        tx_dma_channel,
        &tx_config,
        &maple_tx_pio->txf[maple_tx_sm],
        NULL,
        0,
        false
    );

    printf("[DC] Maple Bus initialized\n");
    printf("[DC]   TX: PIO0 SM%d, RX: PIO1 SM0-2\n", maple_tx_sm);
    printf("[DC]   Pins: %d/%d, Clock div: %.2f\n", MAPLE_PIN1, MAPLE_PIN5, clock_div);
}

// ============================================================================
// PACKET TRANSMISSION
// ============================================================================

static void maple_send_packet(const uint8_t *data, uint16_t length)
{
    // Maple packets are sent as 32-bit words
    // First word sent to PIO is bit count - 1

    uint16_t bit_pairs = length * 4;  // 4 bit pairs per byte
    uint16_t word_count = (length + 3) / 4;

    // Build TX buffer
    tx_buffer[0] = bit_pairs - 1;  // Size in bit pairs minus 1
    memcpy(&tx_buffer[1], data, length);

    // Pad to word boundary if needed
    if (length % 4 != 0) {
        uint8_t *pad = (uint8_t *)&tx_buffer[1] + length;
        for (int i = length % 4; i < 4; i++) {
            *pad++ = 0;
        }
    }

    // Send via DMA
    dma_channel_set_read_addr(tx_dma_channel, tx_buffer, false);
    dma_channel_set_trans_count(tx_dma_channel, word_count + 1, true);
    dma_channel_wait_for_finish_blocking(tx_dma_channel);

    // Small delay for bus to settle
    busy_wait_us(5);
}

// ============================================================================
// PACKET RECEPTION
// ============================================================================

// Decode received transitions into packet data
// Returns packet length, or 0 if no valid packet
static uint16_t maple_receive_packet(uint8_t *buffer, uint16_t max_len)
{
    // Check if RX FIFO has data
    if (pio_sm_is_rx_fifo_empty(maple_rx_pio, maple_rx_sm[0])) {
        return 0;
    }

    uint16_t byte_count = 0;
    uint8_t current_byte = 0;
    uint8_t bit_count = 0;
    bool in_packet = false;
    uint8_t sync_count = 0;

    // Set timeout
    uint32_t timeout = time_us_32() + 2000;  // 2ms timeout

    while (time_us_32() < timeout) {
        if (pio_sm_is_rx_fifo_empty(maple_rx_pio, maple_rx_sm[0])) {
            if (in_packet && byte_count > 0) {
                // End of packet detected (no more transitions)
                break;
            }
            continue;
        }

        // Read 8 bits of transition data (4 transitions)
        uint8_t transitions = pio_sm_get(maple_rx_pio, maple_rx_sm[0]) >> 24;

        if (!in_packet) {
            // Look for sync pattern (alternating 01/10 pairs)
            if (transitions == 0x55 || transitions == 0xAA) {
                sync_count++;
                if (sync_count >= 2) {
                    in_packet = true;
                    sync_count = 0;
                }
            } else {
                sync_count = 0;
            }
            continue;
        }

        // Decode transitions to data bits using lookup table
        uint8_t decoded = rx_lookup[transitions];

        // Accumulate bits into bytes
        for (int i = 0; i < 4 && byte_count < max_len; i++) {
            current_byte = (current_byte >> 1) | ((decoded & (1 << i)) ? 0x80 : 0);
            bit_count++;

            if (bit_count == 8) {
                buffer[byte_count++] = current_byte;
                current_byte = 0;
                bit_count = 0;
            }
        }
    }

    // Clear any remaining data in FIFO
    while (!pio_sm_is_rx_fifo_empty(maple_rx_pio, maple_rx_sm[0])) {
        pio_sm_get(maple_rx_pio, maple_rx_sm[0]);
    }

    return byte_count;
}

// ============================================================================
// BUTTON MAPPING
// ============================================================================

static uint16_t map_buttons_to_dc(uint32_t jp_buttons)
{
    uint16_t dc_buttons = 0;

    // Map JP_BUTTON_* to DC buttons
    if (jp_buttons & JP_BUTTON_B1) dc_buttons |= DC_MAP_B1;
    if (jp_buttons & JP_BUTTON_B2) dc_buttons |= DC_MAP_B2;
    if (jp_buttons & JP_BUTTON_B3) dc_buttons |= DC_MAP_B3;
    if (jp_buttons & JP_BUTTON_B4) dc_buttons |= DC_MAP_B4;
    if (jp_buttons & JP_BUTTON_L1) dc_buttons |= DC_MAP_L1;
    if (jp_buttons & JP_BUTTON_R1) dc_buttons |= DC_MAP_R1;
    if (jp_buttons & JP_BUTTON_S2) dc_buttons |= DC_MAP_S2;
    if (jp_buttons & JP_BUTTON_DU) dc_buttons |= DC_MAP_DU;
    if (jp_buttons & JP_BUTTON_DD) dc_buttons |= DC_MAP_DD;
    if (jp_buttons & JP_BUTTON_DL) dc_buttons |= DC_MAP_DL;
    if (jp_buttons & JP_BUTTON_DR) dc_buttons |= DC_MAP_DR;
    if (jp_buttons & JP_BUTTON_A1) dc_buttons |= DC_MAP_A1;

    // Dreamcast uses active-low buttons (0 = pressed)
    return ~dc_buttons;
}

// ============================================================================
// OUTPUT UPDATE (Called from router)
// ============================================================================

void __not_in_flash_func(dreamcast_update_output)(void)
{
    // Get output state from router for each port
    for (int port = 0; port < MAX_PLAYERS; port++) {
        const input_event_t *event = router_get_output(OUTPUT_TARGET_DREAMCAST, port);
        if (!event || event->type == INPUT_TYPE_NONE) {
            // No input for this port - set neutral state
            dc_state[port].buttons = 0xFFFF;
            dc_state[port].rt = 0;
            dc_state[port].lt = 0;
            dc_state[port].joy_x = 128;
            dc_state[port].joy_y = 128;
            continue;
        }

        // Map buttons
        dc_state[port].buttons = map_buttons_to_dc(event->buttons);

        // Analog sticks (already 0-255 with 128 center)
        dc_state[port].joy_x = event->analog[ANALOG_LX];
        dc_state[port].joy_y = event->analog[ANALOG_LY];
        dc_state[port].joy2_x = event->analog[ANALOG_RX];
        dc_state[port].joy2_y = event->analog[ANALOG_RY];

        // Triggers (0-255)
        dc_state[port].lt = event->analog[ANALOG_L2];
        dc_state[port].rt = event->analog[ANALOG_R2];
    }
}

// ============================================================================
// PACKET HANDLERS
// ============================================================================

static void send_device_info(uint8_t port, uint8_t dest_addr)
{
    uint8_t packet[128];
    uint16_t len = 0;

    // Header
    packet[len++] = MAPLE_RESP_DEVICE_INFO;  // Command
    packet[len++] = dest_addr;                // Destination (console)
    packet[len++] = (port << 6) | MAPLE_ADDR_MAIN;  // Source (us)
    packet[len++] = sizeof(controller_device_info) / 4;  // Length in words

    // Device info payload
    memcpy(&packet[len], controller_device_info, sizeof(controller_device_info));
    len += sizeof(controller_device_info);

    // CRC
    packet[len] = maple_calc_crc(packet, len);
    len++;

    maple_send_packet(packet, len);
}

static void send_controller_condition(uint8_t port, uint8_t dest_addr)
{
    uint8_t packet[32];
    uint16_t len = 0;

    // Header
    packet[len++] = MAPLE_RESP_DATA_TRANSFER;  // Command
    packet[len++] = dest_addr;                  // Destination (console)
    packet[len++] = (port << 6) | MAPLE_ADDR_MAIN;  // Source (us)
    packet[len++] = 3;  // Length: 3 words (12 bytes of data)

    // Function type (FT0 = controller)
    packet[len++] = 0x00;
    packet[len++] = 0x00;
    packet[len++] = 0x00;
    packet[len++] = 0x01;

    // Controller state
    packet[len++] = (dc_state[port].buttons >> 8) & 0xFF;
    packet[len++] = dc_state[port].buttons & 0xFF;
    packet[len++] = dc_state[port].rt;
    packet[len++] = dc_state[port].lt;
    packet[len++] = dc_state[port].joy_x;
    packet[len++] = dc_state[port].joy_y;
    packet[len++] = dc_state[port].joy2_x;
    packet[len++] = dc_state[port].joy2_y;

    // CRC
    packet[len] = maple_calc_crc(packet, len);
    len++;

    maple_send_packet(packet, len);
}

// ============================================================================
// CORE 1 TASK (Real-time Maple Bus handling)
// ============================================================================

void __not_in_flash_func(dreamcast_core1_task)(void)
{
    printf("[DC] Core1: Maple Bus handler started\n");

    while (1) {
        // Update output state from router
        dreamcast_update_output();

        // Check for incoming packet from Dreamcast
        uint16_t rx_len = maple_receive_packet(rx_buffer, MAPLE_RX_BUFFER_SIZE);

        if (rx_len < 4) {
            // No valid packet or too short
            tight_loop_contents();
            continue;
        }

        // Parse header
        uint8_t command = rx_buffer[0];
        uint8_t dest_addr = rx_buffer[1];
        uint8_t src_addr = rx_buffer[2];
        uint8_t data_len = rx_buffer[3];

        // Extract port from destination address
        uint8_t port = (dest_addr >> 6) & 0x03;
        uint8_t peripheral = dest_addr & MAPLE_PERIPHERAL_MASK;

        // Only respond to requests for main controller (not VMU/rumble subperipherals yet)
        if (peripheral != MAPLE_ADDR_MAIN && peripheral != 0x20) {
            continue;
        }

        // Validate port
        if (port >= MAX_PLAYERS) {
            continue;
        }

        // Response delay (Maple Bus spec requires minimum delay)
        busy_wait_us(MAPLE_RESPONSE_DELAY_US);

        // Handle command
        switch (command) {
            case MAPLE_CMD_DEVICE_INFO:
                send_device_info(port, src_addr);
                break;

            case MAPLE_CMD_GET_CONDITION:
                // Check function type in payload (should be FT0 = controller)
                if (rx_len >= 8) {
                    uint32_t func = (rx_buffer[4] << 24) | (rx_buffer[5] << 16) |
                                    (rx_buffer[6] << 8) | rx_buffer[7];
                    if (func == MAPLE_FT_CONTROLLER) {
                        send_controller_condition(port, src_addr);
                    }
                }
                break;

            case MAPLE_CMD_SET_CONDITION:
                // Rumble/vibration control
                if (rx_len >= 12) {
                    uint32_t func = (rx_buffer[4] << 24) | (rx_buffer[5] << 16) |
                                    (rx_buffer[6] << 8) | rx_buffer[7];
                    if (func == MAPLE_FT_VIBRATION) {
                        // Extract rumble intensity
                        dc_rumble[port] = rx_buffer[9];  // Power byte
                    }
                }
                break;

            case MAPLE_CMD_RESET:
            case MAPLE_CMD_KILL:
                // No response needed
                break;

            default:
                // Unknown command - no response
                break;
        }
    }
}

// ============================================================================
// CORE 0 TASK (Periodic maintenance)
// ============================================================================

void dreamcast_task(void)
{
    // Periodic maintenance if needed
    // For now, nothing to do here
}

// ============================================================================
// FEEDBACK ACCESSORS
// ============================================================================

static uint8_t dc_get_rumble(void)
{
    // Return max rumble across all ports
    uint8_t max_rumble = 0;
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (dc_rumble[i] > max_rumble) {
            max_rumble = dc_rumble[i];
        }
    }
    return max_rumble;
}

// ============================================================================
// OUTPUT INTERFACE
// ============================================================================

const OutputInterface dreamcast_output_interface = {
    .name = "Dreamcast",
    .target = OUTPUT_TARGET_DREAMCAST,
    .init = dreamcast_init,
    .task = dreamcast_task,
    .core1_task = dreamcast_core1_task,
    .get_feedback = NULL,
    .get_rumble = dc_get_rumble,
    .get_player_led = NULL,
    .get_profile_count = NULL,
    .get_active_profile = NULL,
    .set_active_profile = NULL,
    .get_profile_name = NULL,
    .get_trigger_threshold = NULL,
};
