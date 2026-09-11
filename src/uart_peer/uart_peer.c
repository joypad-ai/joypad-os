// uart_peer.c - UART inter-MCU input sharing implementation
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Robert Dale Smith
//
// Both producer (host MCU) and consumer (device MCU) live in one file; unused
// functions are eliminated by -gc-sections. The producer/consumer roles share
// the same UART init, SLIP+CRC32 framer, and RX dispatch — each side simply
// sends the message type the other consumes.
//
// Threading: the router tap, uart_peer_task, and the get/send helpers all run
// in the main loop on the same core (TinyUSB host/device callbacks are not ISR
// context), so the TX ring and status buffer need no locking.

#include "uart_peer.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// SLIP + CRC32 + TX RING (shared)
// ============================================================================

#define SLIP_END      0xC0
#define SLIP_ESC      0xDB
#define SLIP_ESC_END  0xDC
#define SLIP_ESC_ESC  0xDD

#define UART_PEER_TIMEOUT_MS  500
#define TX_RING_SIZE          512   // power of two
#define RX_FRAME_MAX          128   // > max raw frame (1 + 46 + 4 = 51)

static uart_inst_t* peer_uart = NULL;

static uint8_t  tx_ring[TX_RING_SIZE];
static volatile uint16_t tx_head, tx_tail;   // head=write, tail=read

static uint8_t  rx_frame[RX_FRAME_MAX];
static uint16_t rx_len;
static bool     rx_in_esc;

static uint32_t last_rx_ms;

// Link RX telemetry (diagnostics): raw bytes seen on the wire vs valid frames
// decoded. raw>0 & frames==0 => peer is transmitting a different/garbled
// protocol; raw==0 => nothing arriving (peer silent or wiring/flow issue).
static volatile uint32_t rx_raw_bytes;
static volatile uint32_t rx_valid_frames;

// Consumer-side: latest feedback status from the link (producer reads it)
static uart_peer_status_t latest_status;
static bool latest_status_new;

// Consumer-side: latest diagnostic heartbeat from the producer (B telemetry)
static uart_peer_debug_t latest_debug;
static bool latest_debug_new;

// Consumer-side: latest extended state (touch + motion), buffered per player.
// A MSG_EXT arrives just before its matching EVENT; submit_peer_event() merges
// it into that event and clears the valid flag (so a later touch/motion-free
// event doesn't inherit stale touch). Slots are player_index & (SLOTS-1).
#define UART_PEER_EXT_SLOTS 8
static uart_peer_ext_t latest_ext[UART_PEER_EXT_SLOTS];
static bool            latest_ext_valid[UART_PEER_EXT_SLOTS];

// Device name learned over the link
static char peer_device_name[32] = "Joypad Controller";

// Standard CRC-32 (IEEE 802.3, reflected, init/xorout 0xFFFFFFFF). Both ends
// run this same routine, so it only needs to be self-consistent.
static uint32_t crc32(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
        }
    }
    return ~crc;
}

static inline bool tx_ring_push(uint8_t byte) {
    uint16_t next = (uint16_t)((tx_head + 1) % TX_RING_SIZE);
    if (next == tx_tail) return false;   // full — drop
    tx_ring[tx_head] = byte;
    tx_head = next;
    return true;
}

static inline void slip_push(uint8_t byte) {
    if (byte == SLIP_END) { tx_ring_push(SLIP_ESC); tx_ring_push(SLIP_ESC_END); }
    else if (byte == SLIP_ESC) { tx_ring_push(SLIP_ESC); tx_ring_push(SLIP_ESC_ESC); }
    else tx_ring_push(byte);
}

// Frame = [type][payload][crc32 LE], SLIP-encoded, END-delimited.
static void frame_send(uint8_t type, const void* payload, size_t plen) {
    if (!peer_uart) return;

    uint8_t hdr[1 + 64];
    if (plen > 64) return;
    hdr[0] = type;
    if (plen) memcpy(&hdr[1], payload, plen);
    uint32_t crc = crc32(hdr, 1 + plen);

    for (size_t i = 0; i < 1 + plen; i++) slip_push(hdr[i]);
    slip_push((uint8_t)(crc & 0xFF));
    slip_push((uint8_t)((crc >> 8) & 0xFF));
    slip_push((uint8_t)((crc >> 16) & 0xFF));
    slip_push((uint8_t)((crc >> 24) & 0xFF));
    tx_ring_push(SLIP_END);
}

// ============================================================================
// RX DISPATCH
// ============================================================================

// P5General auth-bridge RX hook. Strong definition lives in p5general_link.c
// (linked alongside via UART_PEER_SOURCES); the weak no-op here keeps uart_peer
// self-contained for any target that doesn't build the bridge.
__attribute__((weak)) void p5general_link_on_frame(uint8_t type, const uint8_t* payload,
                                                   uint16_t plen) {
    (void)type; (void)payload; (void)plen;
}

static void submit_peer_event(const uart_peer_event_t* packed) {
    input_event_t event;
    init_input_event(&event);

    event.dev_addr = UART_PEER_DEV_ADDR_BASE + packed->player_index;
    event.instance = 0;
    event.type = (input_device_type_t)packed->device_type;
    event.transport = INPUT_TRANSPORT_UART;
    event.buttons = packed->buttons;
    event.analog[ANALOG_LX] = packed->analog[0];
    event.analog[ANALOG_LY] = packed->analog[1];
    event.analog[ANALOG_RX] = packed->analog[2];
    event.analog[ANALOG_RY] = packed->analog[3];
    event.analog[ANALOG_L2] = packed->analog[4];
    event.analog[ANALOG_R2] = packed->analog[5];

    // Merge any extended state (touch + motion) that arrived just before this
    // event, then consume it so a later touch/motion-free event stays clean.
    uint8_t pi = packed->player_index & (UART_PEER_EXT_SLOTS - 1);
    if (latest_ext_valid[pi]) {
        const uart_peer_ext_t* x = &latest_ext[pi];
        if (x->flags & UART_PEER_EXT_FLAG_TOUCH) {
            event.has_touch = true;
            for (int f = 0; f < 2; f++) {
                event.touch[f].x = x->touch_x[f];
                event.touch[f].y = x->touch_y[f];
                event.touch[f].active = (x->touch_active >> f) & 1u;
            }
        }
        if (x->flags & UART_PEER_EXT_FLAG_MOTION) {
            event.has_motion = true;
            for (int i = 0; i < 3; i++) {
                event.accel[i] = x->accel[i];
                event.gyro[i]  = x->gyro[i];
            }
            event.gyro_range  = x->gyro_range;
            event.accel_range = x->accel_range;
        }
        latest_ext_valid[pi] = false;
    }

    router_submit_input(&event);
}

static void dispatch_frame(const uint8_t* frame, uint16_t len) {
    if (len < 5) return;                       // type(1) + crc(4) minimum
    uint16_t body = (uint16_t)(len - 4);
    uint32_t want = (uint32_t)frame[len - 4] |
                    ((uint32_t)frame[len - 3] << 8) |
                    ((uint32_t)frame[len - 2] << 16) |
                    ((uint32_t)frame[len - 1] << 24);
    if (crc32(frame, body) != want) return;    // corrupt frame — drop

    uint8_t type = frame[0];
    const uint8_t* payload = &frame[1];
    uint16_t plen = (uint16_t)(body - 1);

    rx_valid_frames++;
    last_rx_ms = to_ms_since_boot(get_absolute_time());

    switch (type) {
        case UART_PEER_MSG_EVENT:
            if (plen == sizeof(uart_peer_event_t)) {
                uart_peer_event_t ev;
                memcpy(&ev, payload, sizeof(ev));
                submit_peer_event(&ev);
            }
            break;
        case UART_PEER_MSG_STATUS:
            if (plen == sizeof(uart_peer_status_t)) {
                memcpy(&latest_status, payload, sizeof(latest_status));
                latest_status_new = true;
                if (latest_status.flags & UART_PEER_STATUS_FLAG_NAME_VALID) {
                    memcpy(peer_device_name, latest_status.name, sizeof(peer_device_name) - 1);
                    peer_device_name[sizeof(peer_device_name) - 1] = '\0';
                }
            }
            break;
        case UART_PEER_MSG_DEBUG:
            // Accept the current 16-byte struct AND the legacy 12-byte one (no
            // bt_status, magic 0xDB) so a device MCU on either firmware version
            // is visible. Zero-fill first so the absent bt_status reads as 0.
            if (plen == sizeof(uart_peer_debug_t) || plen == 12) {
                memset(&latest_debug, 0, sizeof(latest_debug));
                memcpy(&latest_debug, payload,
                       plen < sizeof(latest_debug) ? plen : sizeof(latest_debug));
                latest_debug_new = true;
            }
            break;
        case UART_PEER_MSG_EXT:
            if (plen == sizeof(uart_peer_ext_t)) {
                uart_peer_ext_t x;
                memcpy(&x, payload, sizeof(x));
                uint8_t pi = x.player_index & (UART_PEER_EXT_SLOTS - 1);
                latest_ext[pi] = x;
                latest_ext_valid[pi] = true;
            }
            break;
        default:
            // P5General auth-bridge types (0x04-0x09) and any future ones.
            p5general_link_on_frame(type, payload, plen);
            break;
    }
}

static void rx_byte(uint8_t b) {
    if (b == SLIP_END) {
        if (rx_len > 0) dispatch_frame(rx_frame, rx_len);
        rx_len = 0;
        rx_in_esc = false;
        return;
    }
    if (rx_in_esc) {
        b = (b == SLIP_ESC_END) ? SLIP_END : (b == SLIP_ESC_ESC) ? SLIP_ESC : b;
        rx_in_esc = false;
    } else if (b == SLIP_ESC) {
        rx_in_esc = true;
        return;
    }
    if (rx_len < RX_FRAME_MAX) {
        rx_frame[rx_len++] = b;
    } else {
        rx_len = 0;   // overflow — resync on next END
        rx_in_esc = false;
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================

void uart_peer_init(const uart_peer_config_t* config) {
    peer_uart = (config->uart_inst == 0) ? uart0 : uart1;
    uint32_t baud = config->baud ? config->baud : UART_PEER_DEFAULT_BAUD;

    uart_init(peer_uart, baud);
    gpio_set_function(config->tx_pin, GPIO_FUNC_UART);
    gpio_set_function(config->rx_pin, GPIO_FUNC_UART);
    uart_set_format(peer_uart, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(peer_uart, true);
    if (config->flow_control) {
        gpio_set_function(config->cts_pin, GPIO_FUNC_UART);
        gpio_set_function(config->rts_pin, GPIO_FUNC_UART);
        uart_set_hw_flow(peer_uart, true, true);
    } else {
        uart_set_hw_flow(peer_uart, false, false);
    }

    tx_head = tx_tail = 0;
    rx_len = 0;
    rx_in_esc = false;
    last_rx_ms = 0;
    latest_status_new = false;

    printf("[uart_peer] init UART%d TX=%d RX=%d %s @ %lu baud\n",
           config->uart_inst, config->tx_pin, config->rx_pin,
           config->flow_control ? "RTS/CTS" : "no-flow", (unsigned long)baud);
}

void uart_peer_task(void) {
    if (!peer_uart) return;

    // Drain RX into the deframer.
    while (uart_is_readable(peer_uart)) {
        rx_raw_bytes++;
        rx_byte((uint8_t)uart_getc(peer_uart));
    }

    // Drain TX ring while the UART will accept bytes (non-blocking).
    while (tx_tail != tx_head && uart_is_writable(peer_uart)) {
        uart_putc_raw(peer_uart, tx_ring[tx_tail]);
        tx_tail = (uint16_t)((tx_tail + 1) % TX_RING_SIZE);
    }
}

bool uart_peer_is_connected(void) {
    if (last_rx_ms == 0) return false;
    return (to_ms_since_boot(get_absolute_time()) - last_rx_ms) < UART_PEER_TIMEOUT_MS;
}

uint32_t uart_peer_get_rx_raw_count(void)   { return rx_raw_bytes; }
uint32_t uart_peer_get_rx_frame_count(void) { return rx_valid_frames; }

void uart_peer_send_frame(uint8_t type, const void* payload, uint16_t plen) {
    frame_send(type, payload, plen);
}

// ----- producer -----
void uart_peer_producer_tap(output_target_t output, uint8_t player_index,
                            const input_event_t* event) {
    (void)output;
    // Loop prevention: never re-export events that arrived from a UART peer.
    if (event->dev_addr >= UART_PEER_DEV_ADDR_BASE &&
        event->dev_addr < UART_PEER_DEV_ADDR_BASE + 16) return;

    uart_peer_event_t packed = {
        .player_index = player_index,
        .device_type = (uint8_t)event->type,
        .buttons = event->buttons,
        .analog = {
            event->analog[ANALOG_LX], event->analog[ANALOG_LY],
            event->analog[ANALOG_RX], event->analog[ANALOG_RY],
            event->analog[ANALOG_L2], event->analog[ANALOG_R2],
        },
    };
    // Touch/motion don't fit the 12-byte event — send them first as MSG_EXT so
    // the consumer can merge them into the event it's about to submit. Only when
    // present, so plain controllers add no link traffic.
    if (event->has_touch || event->has_motion) {
        uart_peer_ext_t ext = {0};
        ext.player_index = player_index;
        if (event->has_touch) {
            ext.flags |= UART_PEER_EXT_FLAG_TOUCH;
            for (int f = 0; f < 2; f++) {
                ext.touch_x[f] = event->touch[f].x;
                ext.touch_y[f] = event->touch[f].y;
                if (event->touch[f].active) ext.touch_active |= (uint8_t)(1u << f);
            }
        }
        if (event->has_motion) {
            ext.flags |= UART_PEER_EXT_FLAG_MOTION;
            for (int i = 0; i < 3; i++) {
                ext.accel[i] = event->accel[i];
                ext.gyro[i]  = event->gyro[i];
            }
            ext.gyro_range  = event->gyro_range;
            ext.accel_range = event->accel_range;
        }
        frame_send(UART_PEER_MSG_EXT, &ext, sizeof(ext));
    }

    frame_send(UART_PEER_MSG_EVENT, &packed, sizeof(packed));
}

bool uart_peer_get_status(uart_peer_status_t* out) {
    if (!latest_status_new) return false;
    memcpy(out, &latest_status, sizeof(*out));
    latest_status_new = false;
    return true;
}

void uart_peer_send_debug(const uart_peer_debug_t* dbg) {
    frame_send(UART_PEER_MSG_DEBUG, dbg, sizeof(*dbg));
}

// ----- consumer -----
void uart_peer_send_status(const uart_peer_status_t* status) {
    frame_send(UART_PEER_MSG_STATUS, status, sizeof(*status));
}

bool uart_peer_get_debug(uart_peer_debug_t* out) {
    if (!latest_debug_new) return false;
    memcpy(out, &latest_debug, sizeof(*out));
    latest_debug_new = false;
    return true;
}

const char* uart_peer_get_device_name(void) {
    return peer_device_name;
}

// ============================================================================
// INPUT INTERFACE (consumer-side router integration)
// ============================================================================

static void uart_peer_input_init(void) {
    // UART init handled by the app via uart_peer_init().
}

static void uart_peer_input_task(void) {
    uart_peer_task();
}

static bool uart_peer_input_connected(void) {
    return uart_peer_is_connected();
}

static uint8_t uart_peer_input_count(void) {
    return uart_peer_is_connected() ? 1 : 0;
}

const InputInterface uart_peer_input_interface = {
    .name = "UART Peer",
    .source = INPUT_SOURCE_UART_PEER,
    .init = uart_peer_input_init,
    .task = uart_peer_input_task,
    .is_connected = uart_peer_input_connected,
    .get_device_count = uart_peer_input_count,
};
