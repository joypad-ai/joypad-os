// nuon_serial_device.c - Nuon Polyface Serial Adapter Device
//
// Emulates a Polyface TYPE=0x04 (SERIAL) peripheral.
// Handles the standard Polyface enumeration protocol (RESET, ALIVE, MAGIC,
// PROBE, BRAND, STATE) and serial register commands (0x40-0x46).
//
// SDATA writes from the Nuon go into tx_fifo → USB CDC → PC terminal.
// USB CDC input from PC goes into rx_fifo → Nuon reads from SDATA (0x43).

#include "nuon_serial_device.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// PIO STATE — both programs on PIO0 (matching nuon_device.c)
// ============================================================================

static PIO pio;
static uint sm_read, sm_send;

// ============================================================================
// CRC
// ============================================================================

static int crc_lut[256];

static int crc_build_lut(void)
{
    for (int i = 0; i < 256; i++) {
        int j = i << 8;
        for (int k = 0; k < 8; k++) {
            j = (j & 0x8000) ? (j << 1) ^ NUONSER_CRC16 : (j << 1);
        }
        crc_lut[i] = j;
    }
    return 0;
}

static int __no_inline_not_in_flash_func(crc_calc)(unsigned char data, int crc)
{
    if (crc_lut[1] == 0) crc_build_lut();
    return ((crc_lut[((crc >> 8) ^ data) & 0xff]) ^ (crc << 8)) & 0xffff;
}

static uint32_t __no_inline_not_in_flash_func(crc_data_packet)(int32_t value, int8_t size)
{
    uint32_t packet = 0;
    uint16_t crc = 0;
    for (int i = 0; i < size; i++) {
        uint8_t byte_val = (value >> ((size - i - 1) * 8)) & 0xff;
        crc = crc_calc(byte_val, crc) & 0xffff;
        packet |= (byte_val << ((3 - i) * 8));
    }
    packet |= (crc << ((2 - size) * 8));
    return packet;
}

static uint8_t __no_inline_not_in_flash_func(eparity)(uint32_t data)
{
    uint32_t p = (data >> 16) ^ data;
    p ^= (p >> 8);
    p ^= (p >> 4);
    p ^= (p >> 2);
    p ^= (p >> 1);
    return p & 1;
}

// ============================================================================
// POLYFACE RESPOND — turnaround delay then send (matching nuon_device.c)
// ============================================================================

static void __no_inline_not_in_flash_func(polyface_respond)(uint32_t word1, uint32_t word0)
{
    (void)word0;

    // Wait 30 clock edges via SIO gpio_get (zero APB contention).
    for (int d = 0; d < 30; d++) {
        while (!gpio_get(CLKIN_PIN)) tight_loop_contents();
        while (gpio_get(CLKIN_PIN)) tight_loop_contents();
    }

    // Push data to PIO send SM
    pio_sm_put_blocking(pio, sm_send, word1);
}

// ============================================================================
// RING BUFFERS (lock-free single-producer single-consumer)
// ============================================================================

typedef struct {
    volatile uint16_t head;
    volatile uint16_t tail;
    uint16_t size;
    uint8_t *buf;
} ringbuf_t;

static uint8_t tx_buf[NUONSER_TX_FIFO_SIZE];
static uint8_t rx_buf[NUONSER_RX_FIFO_SIZE];

static ringbuf_t tx_fifo = { .head = 0, .tail = 0, .size = NUONSER_TX_FIFO_SIZE, .buf = tx_buf };
static ringbuf_t rx_fifo = { .head = 0, .tail = 0, .size = NUONSER_RX_FIFO_SIZE, .buf = rx_buf };

static inline uint16_t ringbuf_count(const ringbuf_t *rb) {
    int16_t diff = (int16_t)rb->head - (int16_t)rb->tail;
    if (diff < 0) diff += rb->size;
    return (uint16_t)diff;
}

static inline uint16_t ringbuf_free(const ringbuf_t *rb) {
    return rb->size - 1 - ringbuf_count(rb);
}

static inline bool ringbuf_push(ringbuf_t *rb, uint8_t byte) {
    uint16_t next = (rb->head + 1) % rb->size;
    if (next == rb->tail) return false;
    rb->buf[rb->head] = byte;
    rb->head = next;
    return true;
}

static inline int ringbuf_pop(ringbuf_t *rb) {
    if (rb->head == rb->tail) return -1;
    uint8_t byte = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) % rb->size;
    return byte;
}

// ============================================================================
// PUBLIC FIFO API (called from Core 0 / app_task)
// ============================================================================

int nuonser_tx_read(void) { return ringbuf_pop(&tx_fifo); }
uint16_t nuonser_tx_available(void) { return ringbuf_count(&tx_fifo); }
bool nuonser_rx_write(uint8_t byte) { return ringbuf_push(&rx_fifo, byte); }
uint16_t nuonser_rx_available(void) { return ringbuf_count(&rx_fifo); }

// ============================================================================
// SERIAL REGISTER STATE
// ============================================================================

static volatile uint8_t ser_flags1 = 0;
static volatile uint8_t ser_sstatus_err = 0;
static volatile uint8_t ser_rstatus_err = 0;

// Debug: log Polyface commands to tx_fifo (appears on CDC)
static void __no_inline_not_in_flash_func(debug_log_packet)(uint8_t type0, uint8_t dataA,
                                                              uint8_t dataS, uint8_t dataC)
{
    static const char hex[] = "0123456789ABCDEF";
    ringbuf_push(&tx_fifo, type0 ? 'R' : 'W');
    ringbuf_push(&tx_fifo, ':');
    ringbuf_push(&tx_fifo, hex[(dataA >> 4) & 0xF]);
    ringbuf_push(&tx_fifo, hex[dataA & 0xF]);
    ringbuf_push(&tx_fifo, ':');
    ringbuf_push(&tx_fifo, hex[(dataS >> 4) & 0xF]);
    ringbuf_push(&tx_fifo, hex[dataS & 0xF]);
    ringbuf_push(&tx_fifo, ':');
    ringbuf_push(&tx_fifo, hex[(dataC >> 4) & 0xF]);
    ringbuf_push(&tx_fifo, hex[dataC & 0xF]);
    ringbuf_push(&tx_fifo, '\n');
}

// ============================================================================
// INIT
// ============================================================================

void nuonser_init(void)
{
    printf("[nuonser] Initializing Nuon serial adapter\n");

    pio = pio0;

    // Both polyface programs on PIO0 (matching nuon_device.c)
    uint offset_read = pio_add_program(pio, &polyface_read_program);
    sm_read = pio_claim_unused_sm(pio, true);
    polyface_read_program_init(pio, sm_read, offset_read, DATAIO_PIN);

    uint offset_send = pio_add_program(pio, &polyface_send_program);
    sm_send = pio_claim_unused_sm(pio, true);
    polyface_send_program_init(pio, sm_send, offset_send, DATAIO_PIN);

    // Claim remaining PIO0 SMs so nothing else uses them
    for (int i = 0; i < 4; i++) {
        if (!pio_sm_is_claimed(pio, i)) pio_sm_claim(pio, i);
    }

    printf("[nuonser] PIO0: read=SM%d send=SM%d (data=GPIO%d, clk=GPIO%d)\n",
           sm_read, sm_send, DATAIO_PIN, CLKIN_PIN);
    printf("[nuonser] PROBE: TYPE=%d (SERIAL), VERSION=%d, MFG=%d\n",
           NUONSER_TYPE, NUONSER_VERSION, NUONSER_MFG);
}

// ============================================================================
// CORE 1 TASK — Polyface command handler (matching nuon_device.c patterns)
// ============================================================================

void __not_in_flash_func(nuonser_core1_task)(void)
{
    // Give Core 1 high bus priority (same as nuon_device.c)
    *(volatile uint32_t *)0x40030000 = (1u << 4);

    uint8_t id = 0;
    bool alive = false;
    bool tagged = false;
    bool branded = false;
    bool configured = false;  // Set after BIOS reads CONFIG
    uint16_t state = 0;
    uint8_t channel = 0;

    // MODE_CONFIG analog value: mode = ((val - 12) * 11) / 255
    // For mode=7 (CONFIG): need ((val-12)*11)/255 >= 7
    // val=175: ((175-12)*11)/255 = 1793/255 = 7 ✓
    #define ATOD_MODE_CONFIG_VALUE 0xAF
    // Modem/serial uses CONFIG_PARALLEL_MEMORY (0x20) per Polyface V11 spec.
    // The UART48 sits on the parallel memory bus. BIOS maps /dev/tty0 to this.
    #define CONFIG_PARALLEL_MEMORY 0x20

    // ATOD latch — real Polyface latches value on CHANNEL write, not on ANALOG read
    uint8_t atod_latched = 0x80;
    bool atod_ready = false;

    // Emulated UART48 (ST16C650A) register state
    uint32_t mem_addr = 0;       // Current memory address (set by A0/A1/A2 writes)
    uint8_t uart_ier = 0;        // Interrupt Enable Register
    uint8_t uart_lcr = 0;        // Line Control Register
    uint8_t uart_mcr = 0;        // Modem Control Register
    uint8_t uart_spr = 0;        // Scratchpad Register

    while (1) {
        // Non-blocking PIO read (matching nuon_device.c)
        uint64_t packet = 0;
        for (int i = 0; i < 2; i++) {
            while (pio_sm_is_rx_fifo_empty(pio, sm_read)) {
                tight_loop_contents();
            }
            uint32_t rxdata = pio_sm_get(pio, sm_read);
            packet = (packet << 32) | (rxdata & 0xFFFFFFFF);
        }

        uint8_t dataA = (packet >> 17) & 0xFF;
        uint8_t dataS = (packet >> 9) & 0x7F;
        uint8_t dataC = (packet >> 1) & 0x7F;
        uint8_t type0 = (packet >> 25) & 0x01;
        uint32_t word0 = 1;
        uint32_t word1 = 0;

        // Log only NON-POLLING commands to CDC
        // Skip the normal polling cycle (84,88,27,35,34,30,31,80,B2,B0,90,94,99,A0,00)
        // so the tx_fifo stays empty for actual test data
        if (dataA != 0x84 && dataA != 0x88 && dataA != 0x27 &&
            dataA != 0x35 && dataA != 0x34 && dataA != 0x30 &&
            dataA != 0x31 && dataA != 0x80 && dataA != 0xB2 &&
            dataA != 0xB0 && dataA != 0x90 && dataA != 0x94 &&
            dataA != 0x99 && dataA != 0xA0 && dataA != 0x00 &&
            dataA != 0xB1 && dataA != 0xB4 && dataA != 0x25 &&
            dataA != 0x20 && dataA != 0x24 && dataA != 0x32) {
            debug_log_packet(type0, dataA, dataS, dataC);
        }

        // =================================================================
        // CONFIGURATION COMMANDS
        // =================================================================

        if (dataA == 0xB1 && dataS == 0x00 && dataC == 0x00) {
            // RESET
            id = 0;
            alive = false;
            tagged = false;
            branded = false;
            state = 0;
            ser_flags1 = 0;
            ser_sstatus_err = 0;
            ser_rstatus_err = 0;
        }
        else if (dataA == 0x80) {
            // ALIVE — test: just respond with bit 0 set (unbranded device present)
            word1 = __rev(0b01);
            if (alive) word1 = __rev(((id & 0x7F) << 1));
            else alive = true;
            polyface_respond(word1, word0);
        }
        else if (dataA == 0x88 && dataS == 0x04 && dataC == 0x40) {
            // ERROR
            word1 = 0;
            polyface_respond(word1, word0);
        }
        else if (dataA == 0x90) {
            // MAGIC — respond whether branded or not (BIOS re-checks after BRAND)
            word1 = __rev(NUONSER_MAGIC);
            polyface_respond(word1, word0);
        }
        else if (dataA == 0x94) {
            // PROBE — TYPE=3 (ORBIT), device function determined by MODE/CONFIG after BRAND
            word1 = ((NUONSER_DEFCFG  & 1) << 31) |
                    ((NUONSER_VERSION & 0x7F) << 24) |
                    ((NUONSER_TYPE   & 0xFF) << 16) |
                    ((NUONSER_MFG    & 0xFF) << 8) |
                    (((tagged  ? 1 : 0) & 1) << 7) |
                    (((branded ? 1 : 0) & 1) << 6) |
                    ((id & 0x1F) << 1);
            word1 = __rev(word1 | eparity(word1));
            polyface_respond(word1, word0);
        }
        else if (dataA == 0x84 && dataS == 0x04 && dataC == 0x40) {
            // REQUEST (B) — always assert our bit when branded
            word1 = 0;
            if (branded && id > 0) {
                word1 = __rev((uint32_t)(1 << id));
            }
            polyface_respond(word1, word0);
        }
        else if (dataA == 0x99 && dataS == 0x01) {
            // STATE — act as a read/write register (BIOS stores SELECT/ENABLE/ROOT bits)
            if (type0 == PACKET_TYPE_READ) {
                word1 = __rev(crc_data_packet(state & 0xFF, 1));
                polyface_respond(word1, word0);
            } else {
                state = dataC & 0x7F;
            }
        }
        else if (dataA == 0xB4 && dataS == 0x00) {
            // BRAND
            id = dataC;
            branded = true;
        }

        // =================================================================
        // MODE/CONFIG REGISTERS (BIOS reads these after BRAND)
        // Polyface device type is determined by MODE (ATOD) + CONFIG register
        // =================================================================

        else if (dataA == 0x34 && dataS == 0x01) {
            // CHANNEL (JS_CHANNEL) — store channel and latch ATOD if MODE
            channel = dataC;
            if (channel == 0x01) {
                // ATOD_CHANNEL_MODE: latch the mode value now
                // Real Polyface hardware latches on channel write, not on read
                atod_latched = ATOD_MODE_CONFIG_VALUE;
                atod_ready = true;
            }
        }
        else if (dataA == 0x27 && dataS == 0x01 && dataC == 0x00) {
            // ADD_REQUEST — report device status bits
            // BIOS serial driver checks these to know when to read/write SDATA
            uint8_t req = 0x04;  // ADD_REQUEST_ATOD (always ready)
            // Signal serial recv data available when rx_fifo has data from PC
            if (ringbuf_count(&rx_fifo) >= 4)
                req |= (1 << 4);  // ADD_REQUEST_SER_RECV_FOUR
            if (ringbuf_count(&rx_fifo) > 0)
                req |= (1 << 5);  // ADD_REQUEST_SER_RECV_ANY
            // Signal serial send space available when tx_fifo has room
            if (ringbuf_free(&tx_fifo) >= 4)
                req |= (1 << 6);  // ADD_REQUEST_SER_SEND_FOUR
            if (ringbuf_free(&tx_fifo) > 0)
                req |= (1 << 7);  // ADD_REQUEST_SER_SEND_ANY
            word1 = __rev(crc_data_packet(req, 1));
            polyface_respond(word1, word0);
        }
        else if (dataA == 0x35 && dataS == 0x01 && dataC == 0x00) {
            // ANALOG (JS_ATOD) — always return MODE_CONFIG value
            // Real Polyface latches ATOD on CHANNEL write. The BIOS reads
            // ANALOG after resetting CHANNEL to 0, expecting the latched value.
            // Simplest: always return MODE_CONFIG for all ANALOG reads.
            word1 = __rev(crc_data_packet(ATOD_MODE_CONFIG_VALUE, 1));
            polyface_respond(word1, word0);
        }
        else if (dataA == 0x25 && dataS == 0x01 && dataC == 0x00) {
            // ADD_CONFIG — return CONFIG_PARALLEL_MEMORY (0x20)
            // This is what real Polyface modem/serial hardware uses
            word1 = __rev(crc_data_packet(CONFIG_PARALLEL_MEMORY, 1));
            polyface_respond(word1, word0);
        }
        else if (dataA == 0x31 && dataS == 0x01 && dataC == 0x00) {
            // SW1 (SWITCH[16:9]) — BIOS polls this for ALL device types
            word1 = __rev(crc_data_packet(0x00, 1));
            polyface_respond(word1, word0);
        }
        else if (dataA == 0x30 && dataS == 0x02 && dataC == 0x00) {
            // SW0 (SWITCH[8:1]) — buttons, return idle
            word1 = __rev(crc_data_packet(0x0080, 2));
            polyface_respond(word1, word0);
        }
        else if (dataA == 0x32 && dataS == 0x02 && dataC == 0x00) {
            // QUADX — return zero
            word1 = __rev(crc_data_packet(0, 1));
            polyface_respond(word1, word0);
        }
        // =================================================================
        // ADDRESS REGISTERS (0x20-0x28) — Polyface parallel memory bus
        // The BIOS uses these to address UART48 registers on the HS Modem
        // =================================================================

        else if (dataA == 0x20 && type0 == PACKET_TYPE_WRITE) {
            // ADD_A0 — low byte of address (UART register select)
            mem_addr = (mem_addr & 0xFFFF80) | (dataC & 0x7F);
        }
        else if (dataA == 0x21 && type0 == PACKET_TYPE_WRITE) {
            // ADD_A1 — mid byte
            mem_addr = (mem_addr & 0xFF007F) | ((dataC & 0x7F) << 7);
        }
        else if (dataA == 0x22 && type0 == PACKET_TYPE_WRITE) {
            // ADD_A2 — high byte
            mem_addr = (mem_addr & 0x003FFF) | ((dataC & 0x7F) << 14);
        }
        else if (dataA == 0x23 && type0 == PACKET_TYPE_WRITE) {
            // ADD_STROBE — accept
        }
        else if (dataA == 0x24 && type0 == PACKET_TYPE_WRITE) {
            // ADD_PINOUT — accept
        }

        // =================================================================
        // MEMORY DATA (0x10-0x11) — Emulates ST16C650A UART registers
        // The BIOS /dev/tty0 driver reads/writes UART via this interface:
        //   1. Write UART register address to ADD_A0 (0x20)
        //   2. Read/write DATA (0x10) to access that UART register
        //
        // ST16C650A registers (3-bit address, selected by A0-A2):
        //   0x00 RBR/THR  — Receive/Transmit data (our FIFO bridge)
        //   0x01 IER      — Interrupt Enable
        //   0x02 IIR/FCR  — Interrupt ID / FIFO Control
        //   0x03 LCR      — Line Control
        //   0x04 MCR      — Modem Control
        //   0x05 LSR      — Line Status
        //   0x06 MSR      — Modem Status
        //   0x07 SPR      — Scratchpad
        // =================================================================

        else if (dataA == 0x10 || dataA == 0x11) {
            uint8_t uart_reg = mem_addr & 0x07;
            bool auto_inc = (dataA == 0x11);
            // Log memory access to CDC for debugging
            debug_log_packet(type0, dataA, uart_reg, dataC);

            if (type0 == PACKET_TYPE_WRITE) {
                // Write to UART register
                switch (uart_reg) {
                case 0x00:  // THR — Transmit data → tx_fifo
                    ringbuf_push(&tx_fifo, dataC & 0x7F);
                    break;
                case 0x01:  // IER — accept
                    uart_ier = dataC & 0x7F;
                    break;
                case 0x02:  // FCR — accept (FIFO control)
                    break;
                case 0x03:  // LCR — accept
                    uart_lcr = dataC & 0x7F;
                    break;
                case 0x04:  // MCR — accept
                    uart_mcr = dataC & 0x7F;
                    break;
                default:
                    break;
                }
                if (auto_inc) mem_addr++;
            } else {
                // Read from UART register
                uint8_t val = 0;
                switch (uart_reg) {
                case 0x00:  // RBR — Receive data ← rx_fifo
                    {
                        int byte = ringbuf_pop(&rx_fifo);
                        val = (byte >= 0) ? (uint8_t)byte : 0;
                    }
                    break;
                case 0x01:  // IER
                    val = uart_ier;
                    break;
                case 0x02:  // IIR — no interrupts pending
                    val = 0x01;  // Bit 0 = no interrupt pending
                    break;
                case 0x03:  // LCR
                    val = uart_lcr;
                    break;
                case 0x04:  // MCR
                    val = uart_mcr;
                    break;
                case 0x05:  // LSR — Line Status
                    val = 0x60;  // Bit 5 = THR empty, Bit 6 = TX empty
                    if (ringbuf_count(&rx_fifo) > 0)
                        val |= 0x01;  // Bit 0 = Data Ready
                    break;
                case 0x06:  // MSR — Modem Status
                    val = 0x30;  // CTS + DSR asserted
                    break;
                case 0x07:  // SPR — Scratchpad
                    val = uart_spr;
                    break;
                }
                word1 = __rev(crc_data_packet(val, 1));
                polyface_respond(word1, word0);
                if (auto_inc) mem_addr++;
            }
        }

        // =================================================================
        // POLYFACE SERIAL REGISTERS (0x40-0x46) — kept for compatibility
        // =================================================================

        else if (dataA == 0x40 && type0 == PACKET_TYPE_WRITE) {
            // BAUD — accepted
        }
        else if (dataA == 0x41 && type0 == PACKET_TYPE_WRITE) {
            // FLAGS0 — accepted
        }
        else if (dataA == 0x42 && type0 == PACKET_TYPE_WRITE) {
            // FLAGS1 — store RUN bit
            ser_flags1 = dataC & 0x7F;
        }
        else if (dataA == 0x43) {
            if (type0 == PACKET_TYPE_WRITE) {
                ringbuf_push(&tx_fifo, dataC & 0x7F);
            } else {
                int byte = ringbuf_pop(&rx_fifo);
                word1 = __rev(crc_data_packet(byte >= 0 ? byte : 0, 1));
                polyface_respond(word1, word0);
            }
        }
        else if (dataA == 0x44 && type0 == PACKET_TYPE_READ) {
            uint16_t free = ringbuf_free(&tx_fifo);
            if (free > 31) free = 31;
            word1 = __rev(crc_data_packet((uint8_t)free | ser_sstatus_err, 1));
            polyface_respond(word1, word0);
        }
        else if (dataA == 0x45 && type0 == PACKET_TYPE_READ) {
            uint16_t avail = ringbuf_count(&rx_fifo);
            if (avail > 31) avail = 31;
            word1 = __rev(crc_data_packet((uint8_t)avail | ser_rstatus_err, 1));
            polyface_respond(word1, word0);
        }
        else if (dataA == 0x46 && type0 == PACKET_TYPE_WRITE) {
            ser_sstatus_err = 0;
            ser_rstatus_err = 0;
        }

        // Unhandled commands silently ignored
    }
}

// ============================================================================
// OUTPUT INTERFACE
// ============================================================================

const OutputInterface nuon_serial_output_interface = {
    .name = "Nuon Serial",
    .target = OUTPUT_TARGET_NUON,
    .init = nuonser_init,
    .core1_task = nuonser_core1_task,
    .task = NULL,
    .get_rumble = NULL,
    .get_player_led = NULL,
    .get_profile_count = NULL,
    .get_active_profile = NULL,
    .set_active_profile = NULL,
    .get_profile_name = NULL,
    .get_trigger_threshold = NULL,
};
