// nuon_host.c - Native Nuon Controller Host Driver
//
// Reads native Nuon controllers (Polyface peripherals) via software bit-bang
// clock generation + command sending, and PIO for response capture.
//
// Architecture:
// - Core 1: Continuous clock on GPIO3, sends commands, reads responses via PIO
// - Core 0: Reads shared volatile state, submits to router

#include "nuon_host.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/pio.h"
#include "hardware/gpio.h"
#include "polyface_read.pio.h"
#include <stdio.h>

// ============================================================================
// ARM INTRINSIC HELPERS
// ============================================================================

// Bit-reverse a 32-bit word (equivalent to ARM __rev instruction)
static inline uint32_t bit_reverse_32(uint32_t x) {
    x = ((x & 0x55555555) << 1) | ((x >> 1) & 0x55555555);
    x = ((x & 0x33333333) << 2) | ((x >> 2) & 0x33333333);
    x = ((x & 0x0F0F0F0F) << 4) | ((x >> 4) & 0x0F0F0F0F);
    x = (x << 24) | ((x & 0xFF00) << 8) | ((x >> 8) & 0xFF00) | (x >> 24);
    return x;
}

// ============================================================================
// SHARED STATE (Core 1 -> Core 0)
// ============================================================================

volatile bool nuon_controller_connected = false;
volatile uint16_t nuon_buttons = 0;
volatile uint8_t nuon_analog_x1 = 128;
volatile uint8_t nuon_analog_y1 = 128;
volatile uint8_t nuon_analog_x2 = 128;
volatile uint8_t nuon_analog_y2 = 128;
volatile uint32_t nuon_poll_count = 0;
volatile uint32_t nuon_diag_alive = 0;
volatile uint32_t nuon_diag_fifo = 0;
volatile uint32_t nuon_diag_cmd = 0;  // Last command sent

// ============================================================================
// INTERNAL STATE
// ============================================================================

static bool initialized = false;
static PIO pio_read;
static uint sm_read;
static uint pio_read_offset;  // Store program offset for SM reset
static int crc_lut[256];  // CRC lookup table

// ============================================================================
// CRC FUNCTIONS (from nuon_device.c)
// ============================================================================

static int crc_build_lut(void)
{
    int i, j, k;
    for (i = 0; i < 256; i++) {
        for (j = i << 8, k = 0; k < 8; k++) {
            j = (j & 0x8000) ? (j << 1) ^ PF_CRC16 : (j << 1);
            crc_lut[i] = j;
        }
    }
    return 0;
}

static int __no_inline_not_in_flash_func(crc_calc)(unsigned char data, int crc)
{
    if (crc_lut[1] == 0) crc_build_lut();
    return ((crc_lut[((crc >> 8) ^ data) & 0xff]) ^ (crc << 8)) & 0xffff;
}

// Verify CRC for a 1-byte response: [data][crc_hi][crc_lo][0x00]
static bool __no_inline_not_in_flash_func(verify_crc_1byte)(uint32_t response, uint8_t* data_out)
{
    uint8_t data = (response >> 24) & 0xFF;
    uint8_t crc_hi = (response >> 16) & 0xFF;
    uint8_t crc_lo = (response >> 8) & 0xFF;

    // Calculate expected CRC
    uint16_t calc_crc = crc_calc(data, 0);
    uint16_t recv_crc = (crc_hi << 8) | crc_lo;

    if (calc_crc == recv_crc) {
        *data_out = data;
        return true;
    }
    return false;
}

// Verify CRC for a 2-byte response: [data_hi][data_lo][crc_hi][crc_lo]
static bool __no_inline_not_in_flash_func(verify_crc_2byte)(uint32_t response, uint16_t* data_out)
{
    uint8_t data_hi = (response >> 24) & 0xFF;
    uint8_t data_lo = (response >> 16) & 0xFF;
    uint8_t crc_hi = (response >> 8) & 0xFF;
    uint8_t crc_lo = response & 0xFF;

    // Calculate expected CRC
    uint16_t calc_crc = crc_calc(data_hi, 0);
    calc_crc = crc_calc(data_lo, calc_crc);
    uint16_t recv_crc = (crc_hi << 8) | crc_lo;

    if (calc_crc == recv_crc) {
        *data_out = (data_hi << 8) | data_lo;
        return true;
    }
    return false;
}

// ============================================================================
// EVEN PARITY
// ============================================================================

static uint8_t __no_inline_not_in_flash_func(eparity)(uint32_t data)
{
    uint32_t p;
    p = (data >> 16) ^ data;
    p ^= (p >> 8);
    p ^= (p >> 4);
    p ^= (p >> 2);
    p ^= (p >> 1);
    return (p & 0x1);
}

// ============================================================================
// POLYFACE COMMAND BUILDING
// ============================================================================

// Build a 32-bit Polyface command word
// type0: 1=READ, 0=WRITE
// dataA: address/command (8 bits at 24:17)
// dataS: size field (7 bits at 15:9)
// dataC: control/data field (7 bits at 7:1)
// bit 0: even parity
static uint32_t __no_inline_not_in_flash_func(build_command)(uint8_t type0, uint8_t dataA, uint8_t dataS, uint8_t dataC)
{
    uint32_t word = 0;
    word |= ((type0 & 0x01) << 25);     // bit 25: READ/WRITE
    word |= ((dataA & 0xFF) << 17);     // bits 24:17: address
    word |= ((dataS & 0x7F) << 9);      // bits 15:9: size
    word |= ((dataC & 0x7F) << 1);      // bits 7:1: control
    word |= eparity(word);              // bit 0: even parity
    return word;
}

// ============================================================================
// SOFTWARE BIT-BANG CLOCK + COMMAND SENDING
// ============================================================================

// Generate a single clock cycle (toggle GPIO3)
// Clock period ~2us (500kHz) using busy_wait_us_32(1) per half-cycle
static void __no_inline_not_in_flash_func(clock_cycle)(void)
{
    gpio_put(NUON_PIN_CLK, 1);
    busy_wait_us_32(1);
    gpio_put(NUON_PIN_CLK, 0);
    busy_wait_us_32(1);
}

// Generate N clock cycles (for keeping controller alive)
static void __no_inline_not_in_flash_func(clock_cycles)(int n)
{
    for (int i = 0; i < n; i++) {
        clock_cycle();
    }
}

// Send a 33-bit packet: start bit (1) + 32 data bits
// We drive data on falling edge of clock, controller samples on rising edge
// IMPORTANT: Disable PIO read SM during send to prevent capturing our own bits
static void __no_inline_not_in_flash_func(send_command)(uint32_t word)
{
    // Disable PIO read SM to prevent it from capturing our command
    pio_sm_set_enabled(pio_read, sm_read, false);

    // Switch data pin to SIO function for bit-bang output
    gpio_set_function(NUON_PIN_DATA, GPIO_FUNC_SIO);
    gpio_set_dir(NUON_PIN_DATA, GPIO_OUT);

    // Send start bit = 1
    gpio_put(NUON_PIN_DATA, 1);
    clock_cycle();

    // Send 32 data bits MSB first
    for (int i = 31; i >= 0; i--) {
        gpio_put(NUON_PIN_DATA, (word >> i) & 1);
        clock_cycle();
    }

    // Release data line — switch back to PIO function for reading
    gpio_put(NUON_PIN_DATA, 0);  // Drive low briefly
    gpio_set_dir(NUON_PIN_DATA, GPIO_IN);
    // Restore PIO function so PIO read SM can capture response
    pio_gpio_init(pio_read, NUON_PIN_DATA);

    // Flush any stale PIO FIFO data and re-enable read SM
    // IMPORTANT: jump to program start so SM begins fresh looking for start bit
    pio_sm_clear_fifos(pio_read, sm_read);
    pio_sm_restart(pio_read, sm_read);
    pio_sm_exec(pio_read, sm_read, pio_encode_jmp(pio_read_offset));
    pio_sm_set_enabled(pio_read, sm_read, true);
}

// ============================================================================
// PIO RESPONSE READING
// ============================================================================

// Flush the PIO read FIFO (discard stale data)
static void __no_inline_not_in_flash_func(flush_pio_fifo)(void)
{
    while (!pio_sm_is_rx_fifo_empty(pio_read, sm_read)) {
        (void)pio_sm_get(pio_read, sm_read);
    }
}

// Wait for turnaround gap (controller needs 30+ clock edges before responding)
// and then read response from PIO
// Returns true if response received within timeout
static bool __no_inline_not_in_flash_func(read_response)(uint32_t* word1_out)
{
    // send_command() already re-enabled PIO read SM with clean FIFOs.
    // Now generate turnaround clock cycles (controller prepares response)
    clock_cycles(40);

    // Generate clock for response capture (33 bits = start + 32 data, need ~130 clocks for 2 words)
    for (int i = 0; i < 150; i++) {
        clock_cycle();
    }

    // The polyface_read PIO program outputs TWO 32-bit words per packet
    // (autopush at 32 bits, packet is 1 start + 3*11 = 34 bits captured as 64)
    uint8_t fifo_level = pio_sm_get_rx_fifo_level(pio_read, sm_read);

    if (fifo_level >= 2) {
        uint32_t word0 = pio_sm_get(pio_read, sm_read);
        uint32_t word1 = pio_sm_get(pio_read, sm_read);
        // The PIO captures bits MSB-first into ISR with left shift
        // word0 has the first 32 captured bits, word1 has the next 32
        // The actual response data is in word1 (the 33-bit packet minus the start bit
        // aligns such that data bits end up in word1)
        *word1_out = word1;
        return true;
    }

    if (fifo_level == 1) {
        uint32_t word = pio_sm_get(pio_read, sm_read);
        *word1_out = word;
        return true;
    }

    return false;
}

// ============================================================================
// BUTTON MAPPING: NUON -> JP
// ============================================================================

static uint32_t __no_inline_not_in_flash_func(map_nuon_to_jp)(uint16_t nuon)
{
    uint32_t buttons = 0;

    // Face buttons
    if (nuon & NUON_BTN_A)       buttons |= JP_BUTTON_B1;  // Nuon A -> Cross/A
    if (nuon & NUON_BTN_B)       buttons |= JP_BUTTON_B3;  // Nuon B -> Square/X
    if (nuon & NUON_BTN_C_DOWN)  buttons |= JP_BUTTON_B2;  // Nuon C-Down -> Circle/B
    if (nuon & NUON_BTN_C_LEFT)  buttons |= JP_BUTTON_B4;  // Nuon C-Left -> Triangle/Y
    if (nuon & NUON_BTN_C_UP)    buttons |= JP_BUTTON_L2;  // Nuon C-Up -> L2
    if (nuon & NUON_BTN_C_RIGHT) buttons |= JP_BUTTON_R2;  // Nuon C-Right -> R2

    // Shoulders
    if (nuon & NUON_BTN_L)       buttons |= JP_BUTTON_L1;
    if (nuon & NUON_BTN_R)       buttons |= JP_BUTTON_R1;

    // System buttons
    if (nuon & NUON_BTN_START)   buttons |= JP_BUTTON_S2;  // Start
    if (nuon & NUON_BTN_NUON)    buttons |= JP_BUTTON_S1;  // Nuon/Z -> Select

    // D-pad
    if (nuon & NUON_BTN_UP)      buttons |= JP_BUTTON_DU;
    if (nuon & NUON_BTN_DOWN)    buttons |= JP_BUTTON_DD;
    if (nuon & NUON_BTN_LEFT)    buttons |= JP_BUTTON_DL;
    if (nuon & NUON_BTN_RIGHT)   buttons |= JP_BUTTON_DR;

    return buttons;
}

// ============================================================================
// CORE 1 PROTOCOL HANDLER
// ============================================================================

void __not_in_flash_func(nuon_host_core1_task)(void)
{
    // Give Core 1 high bus priority
    *(volatile uint32_t *)0x40030000 = (1u << 4);

    // Build CRC lookup table
    crc_build_lut();

    // Enumeration state
    bool alive = false;
    bool magic_ok = false;
    bool branded = false;
    bool enabled = false;
    uint8_t device_id = 1;

    // Polling state
    uint8_t current_channel = 0;
    uint32_t loop_count = 0;
    uint32_t success_count = 0;

    // Response storage
    uint32_t response;

    // Diagnostic counters (shared with Core 0 via volatiles)
    volatile uint32_t* diag_alive_raw = &nuon_diag_alive;
    volatile uint32_t* diag_fifo_lvl = &nuon_diag_fifo;

    // Run some initial clock cycles to let controller wake up
    clock_cycles(5000);

    while (1) {
        loop_count++;

        // Keep clock running continuously (controller needs this)
        clock_cycles(200);

        // ================================================================
        // ENUMERATION SEQUENCE
        // ================================================================

        if (!alive) {
            // Send RESET
            send_command(build_command(PF_TYPE_WRITE, PF_CMD_RESET, 0x00, 0x00));
            clock_cycles(100);

            // Send ALIVE and capture raw response for debug
            uint32_t alive_cmd = build_command(PF_TYPE_READ, PF_CMD_ALIVE, 0x04, 0x40);
            nuon_diag_cmd = alive_cmd;
            send_command(alive_cmd);

            // After send_command, PIO read SM is freshly enabled.
            // Generate turnaround clocks
            clock_cycles(40);

            // Now generate clocks for response capture
            for (int i = 0; i < 150; i++) {
                clock_cycle();
            }

            // Try software sampling first to verify controller is responding at ALL
            // Sample data pin on each clock rising edge for 100 cycles
            uint32_t sw_bits = 0;
            uint8_t ones_count = 0;
            for (int i = 0; i < 100; i++) {
                gpio_put(NUON_PIN_CLK, 1);
                busy_wait_us_32(1);
                bool d = gpio_get(NUON_PIN_DATA);
                if (d) ones_count++;
                if (i < 32) sw_bits = (sw_bits << 1) | (d ? 1 : 0);
                gpio_put(NUON_PIN_CLK, 0);
                busy_wait_us_32(1);
            }

            // Also check PIO FIFO
            uint8_t fifo_lvl = pio_sm_get_rx_fifo_level(pio_read, sm_read);
            *diag_fifo_lvl = (fifo_lvl << 8) | ones_count;  // Pack both diagnostics

            if (ones_count > 0) {
                // Controller IS sending data!
                *diag_alive_raw = sw_bits;
                // If PIO also captured, use that
                if (fifo_lvl >= 2) {
                    uint32_t w0 = pio_sm_get(pio_read, sm_read);
                    uint32_t w1 = pio_sm_get(pio_read, sm_read);
                    *diag_alive_raw = w1;
                }
                alive = true;
                nuon_controller_connected = true;
            } else {
                *diag_alive_raw = 0xDEAD0000 | fifo_lvl;
            }
            continue;
        }

        if (!magic_ok) {
            // Send MAGIC request
            send_command(build_command(PF_TYPE_READ, PF_CMD_MAGIC, 0x04, 0x00));
            if (read_response(&response)) {
                // Should return "JUDE" = 0x4A554445
                // Accept any response for now, we'll verify buttons work
                magic_ok = true;
            }
            continue;
        }

        if (!branded) {
            // Send PROBE to get device info
            send_command(build_command(PF_TYPE_READ, PF_CMD_PROBE, 0x04, 0x00));
            if (read_response(&response)) {
                // Response contains device info, we don't need to parse it
            }
            clock_cycles(50);

            // Send BRAND to assign ID
            send_command(build_command(PF_TYPE_WRITE, PF_CMD_BRAND, 0x00, device_id));
            clock_cycles(50);
            branded = true;
            continue;
        }

        if (!enabled) {
            // Send STATE write with ENABLE + ROOT
            send_command(build_command(PF_TYPE_WRITE, PF_CMD_STATE, 0x01, PF_STATE_ENABLE | PF_STATE_ROOT));
            clock_cycles(50);
            enabled = true;
            continue;
        }

        // ================================================================
        // POLLING LOOP
        // ================================================================

        // Select analog channel and read
        switch (current_channel) {
            case 0:
                // Read X1
                send_command(build_command(PF_TYPE_WRITE, PF_CMD_CHANNEL, 0x01, PF_CHANNEL_X1));
                clock_cycles(30);
                send_command(build_command(PF_TYPE_READ, PF_CMD_ANALOG, 0x01, 0x00));
                if (read_response(&response)) {
                    uint8_t data;
                    if (verify_crc_1byte(response, &data)) {
                        nuon_analog_x1 = data;
                    }
                }
                current_channel = 1;
                break;

            case 1:
                // Read Y1
                send_command(build_command(PF_TYPE_WRITE, PF_CMD_CHANNEL, 0x01, PF_CHANNEL_Y1));
                clock_cycles(30);
                send_command(build_command(PF_TYPE_READ, PF_CMD_ANALOG, 0x01, 0x00));
                if (read_response(&response)) {
                    uint8_t data;
                    if (verify_crc_1byte(response, &data)) {
                        nuon_analog_y1 = data;
                    }
                }
                current_channel = 2;
                break;

            case 2:
                // Read X2
                send_command(build_command(PF_TYPE_WRITE, PF_CMD_CHANNEL, 0x01, PF_CHANNEL_X2));
                clock_cycles(30);
                send_command(build_command(PF_TYPE_READ, PF_CMD_ANALOG, 0x01, 0x00));
                if (read_response(&response)) {
                    uint8_t data;
                    if (verify_crc_1byte(response, &data)) {
                        nuon_analog_x2 = data;
                    }
                }
                current_channel = 3;
                break;

            case 3:
                // Read Y2
                send_command(build_command(PF_TYPE_WRITE, PF_CMD_CHANNEL, 0x01, PF_CHANNEL_Y2));
                clock_cycles(30);
                send_command(build_command(PF_TYPE_READ, PF_CMD_ANALOG, 0x01, 0x00));
                if (read_response(&response)) {
                    uint8_t data;
                    if (verify_crc_1byte(response, &data)) {
                        nuon_analog_y2 = data;
                    }
                }
                current_channel = 4;
                break;

            case 4:
                // Read SWITCH (buttons) - 2 byte response
                send_command(build_command(PF_TYPE_READ, PF_CMD_SWITCH, 0x02, 0x00));
                if (read_response(&response)) {
                    uint16_t data;
                    if (verify_crc_2byte(response, &data)) {
                        nuon_buttons = data;
                        success_count++;
                    }
                }
                current_channel = 0;  // Restart cycle
                nuon_poll_count++;
                break;
        }

        // Check for connection loss (no successful reads in a while)
        if (loop_count > 1000 && success_count == 0) {
            // Reset enumeration
            alive = false;
            magic_ok = false;
            branded = false;
            enabled = false;
            nuon_controller_connected = false;
            loop_count = 0;
        }
        if (loop_count > 1000) {
            loop_count = 1;
            success_count = 0;
        }
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================

void nuon_host_init(void)
{
    if (initialized) return;

    printf("[nuon_host] Initializing Nuon host driver\n");
    printf("[nuon_host]   DATA=GPIO%d, CLK=GPIO%d\n", NUON_PIN_DATA, NUON_PIN_CLK);

    // Initialize GPIO pins
    // Data pin: bidirectional, start as input
    // NO pull-down — the controller needs to drive this line HIGH for responses.
    // The bus idles LOW naturally (controller holds it low when connected).
    // A pull-down from our side would fight the controller's output.
    gpio_init(NUON_PIN_DATA);
    gpio_set_dir(NUON_PIN_DATA, GPIO_IN);
    gpio_disable_pulls(NUON_PIN_DATA);
    gpio_set_input_hysteresis_enabled(NUON_PIN_DATA, true);

    // Clock pin: output, we generate the clock
    gpio_init(NUON_PIN_CLK);
    gpio_set_dir(NUON_PIN_CLK, GPIO_OUT);
    gpio_put(NUON_PIN_CLK, 0);  // Start low

    // Initialize PIO for reading responses
    // NOTE: polyface_read_program_init calls pio_gpio_init on BOTH pin (data)
    // and pin+1 (clock), setting them to PIO function. We need to reclaim
    // GPIO3 (clock) for SIO output afterwards, since WE drive the clock.
    // GPIO2 (data) stays PIO-muxed — that's fine because:
    //   - PIO 'in pins' reads from GPIO input register (works regardless of mux)
    //   - For sending, we'll switch GPIO2 to SIO temporarily
    pio_read = pio0;
    pio_read_offset = pio_add_program(pio_read, &polyface_read_program);
    sm_read = pio_claim_unused_sm(pio_read, true);
    polyface_read_program_init(pio_read, sm_read, pio_read_offset, NUON_PIN_DATA);

    // Reclaim clock pin for SIO output (PIO init stole it via pio_gpio_init)
    // Use gpio_init which does a full reset: SIO function + clear output + input direction
    gpio_init(NUON_PIN_CLK);
    gpio_set_dir(NUON_PIN_CLK, GPIO_OUT);
    gpio_put(NUON_PIN_CLK, 0);
    // Verify: toggle clock once to confirm it works
    gpio_put(NUON_PIN_CLK, 1);
    busy_wait_us_32(10);
    gpio_put(NUON_PIN_CLK, 0);
    printf("[nuon_host]   CLK test: GPIO%d toggled, state=%d\n", NUON_PIN_CLK, gpio_get(NUON_PIN_CLK));

    printf("[nuon_host]   PIO read SM%d at offset %d\n", sm_read, pio_read_offset);

    // Build CRC lookup table
    crc_build_lut();

    initialized = true;
    printf("[nuon_host] Initialization complete\n");
}

void nuon_host_task(void)
{
    if (!initialized) return;

    // Debug output periodically
    static uint32_t last_poll = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_poll >= 2000) {
        last_poll = now;
        // Read GPIO states for diagnostic
        bool clk_state = gpio_get(NUON_PIN_CLK);
        bool dat_state = gpio_get(NUON_PIN_DATA);
        printf("[nuon_host] polls=%lu conn=%d alive=0x%08lX fifo=%lu clk=%d dat=%d cmd=0x%08lX\n",
               (unsigned long)nuon_poll_count,
               nuon_controller_connected ? 1 : 0,
               (unsigned long)nuon_diag_alive,
               (unsigned long)nuon_diag_fifo,
               clk_state, dat_state,
               (unsigned long)nuon_diag_cmd);
    }

    // Only submit if connected
    if (!nuon_controller_connected) return;

    // Build input event from shared state
    input_event_t event;
    init_input_event(&event);

    event.dev_addr = 0xC0;  // Use 0xC0 range for Nuon native inputs
    event.instance = 0;
    event.type = INPUT_TYPE_GAMEPAD;
    event.transport = INPUT_TRANSPORT_NATIVE;

    // Map buttons
    event.buttons = map_nuon_to_jp(nuon_buttons);

    // Map analog sticks (Nuon uses 0-255 with 128 center, same as HID)
    event.analog[ANALOG_LX] = nuon_analog_x1;
    event.analog[ANALOG_LY] = nuon_analog_y1;
    event.analog[ANALOG_RX] = nuon_analog_x2;
    event.analog[ANALOG_RY] = nuon_analog_y2;

    // Submit to router
    router_submit_input(&event);
}

bool nuon_host_is_connected(void)
{
    return nuon_controller_connected;
}

uint8_t nuon_host_get_device_count(void)
{
    return nuon_controller_connected ? 1 : 0;
}

// ============================================================================
// INPUT INTERFACE
// ============================================================================

const InputInterface nuon_input_interface = {
    .name = "Nuon",
    .source = INPUT_SOURCE_NATIVE_NUON,
    .init = nuon_host_init,
    .task = nuon_host_task,
    .is_connected = nuon_host_is_connected,
    .get_device_count = nuon_host_get_device_count,
};
