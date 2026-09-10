// uart_peer.h - UART inter-MCU input sharing (dual-RP2040 boards)
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Robert Dale Smith
//
// Point-to-point UART analog of i2c_peer, for dual-RP2040 boards where one MCU
// is the USB host (reads controllers) and the other is the USB device (output),
// connected by a crossed UART link (HID-Remapper lineage: uart1, 4 Mbps, RTS/CTS).
//
// Producer side (USB host MCU): installs a router tap, serializes each
//   input_event_t to a 12-byte wire event and pushes it as a framed EVENT.
// Consumer side (USB device MCU): receives EVENT frames, submits them to its
//   router under INPUT_SOURCE_UART_PEER, and pushes feedback (rumble/LED) back
//   as STATUS frames.
//
// Framing: SLIP (END=0xC0, ESC=0xDB) over [type:1][payload][crc32:4 LE].
// Both sides run the same uart_peer_task(); each just sends the other's type.

#ifndef UART_PEER_H
#define UART_PEER_H

#include <stdint.h>
#include <stdbool.h>
#include "core/input_event.h"
#include "core/input_interface.h"
#include "core/router/router.h"

// Defaults match the HID-Remapper dual board wiring (uart1, crossed A<->B).
#define UART_PEER_DEFAULT_BAUD   4000000
#define UART_PEER_TX_PIN         20
#define UART_PEER_RX_PIN         21
#define UART_PEER_CTS_PIN        26
#define UART_PEER_RTS_PIN        27

// Device address range for UART-peer devices — distinct from USB/BT, native
// (0xD0+) and I2C peer (0xE0+). Used for loop-prevention + router slotting.
#define UART_PEER_DEV_ADDR_BASE  0xC0

// Frame message types (first byte of each framed packet)
#define UART_PEER_MSG_EVENT      0x01   // producer -> consumer: input event
#define UART_PEER_MSG_STATUS     0x02   // consumer -> producer: feedback/status
#define UART_PEER_MSG_DEBUG      0x03   // producer -> consumer: liveness/host telemetry

// P5General PS5-auth bridge (dual-chip). Carries the p5general_auth_data handoff
// across the link so GP2040 P5General auth — normally single-chip shared RAM —
// works split across the DEVICE side (A, presents to the PS5) and the HOST side
// (B, owns the auth dongle). A distinct type per operation keeps every payload
// <= 64 bytes (the frame_send cap), avoiding a subtype byte on the 64B buffers.
// Handled by the weak p5general_link_on_frame() hook (src/uart_peer/p5general_link.c).
#define UART_PEER_MSG_P5G_SIGN_REQ   0x04   // A->B: 64B report to sign
#define UART_PEER_MSG_P5G_SIGN_RESP  0x05   // B->A: 64B signed report
#define UART_PEER_MSG_P5G_F0         0x06   // A->B: 64B auth_buffer (PS5 F0 challenge)
#define UART_PEER_MSG_P5G_POLL_F1    0x07   // A->B: 0B, PS5 is polling F1/F2
#define UART_PEER_MSG_P5G_AUTH_DATA  0x08   // B->A: 64B auth_buffer (dongle F1/F2 response)
#define UART_PEER_MSG_P5G_DONGLE     0x09   // B->A: 1B dongle_ready

// Extended input state (touch + motion) — producer -> consumer. The 12-byte
// EVENT only carries buttons + 6 analog axes; a DualSense/DS4/SC2 input also has
// touchpad + gyro/accel that output modes (e.g. P5General DualSense) can emit.
// The producer sends this frame immediately BEFORE the matching EVENT whenever
// the source event has touch or motion; the consumer buffers it per-player and
// merges it into that next EVENT before submitting to the router. Optional: an
// input with neither touch nor motion sends no EXT (event stays touch/motion-free).
#define UART_PEER_MSG_EXT            0x0A   // producer -> consumer: touch + motion

// Diagnostic heartbeat (producer/host MCU -> consumer): proves B is alive, its
// loop is advancing (uptime_ms), and how many USB host devices it has mounted.
typedef struct __attribute__((packed)) {
    uint8_t  magic;            // 0xDB sentinel
    uint8_t  dev_count;        // mounted USB host devices
    uint16_t last_vid;         // VID of most-recently-mounted device
    uint16_t last_pid;         // PID of most-recently-mounted device
    uint32_t uptime_ms;        // B uptime (changing value => B loop running)
} uart_peer_debug_t;

// 12-byte wire event — byte-identical to i2c_peer_event_t so the two peer
// transports share a format.
typedef struct __attribute__((packed)) {
    uint8_t  player_index;      // Player slot (0-7)
    uint8_t  device_type;       // INPUT_TYPE_* enum
    uint32_t buttons;           // Button state
    uint8_t  analog[6];         // [0]=LX [1]=LY [2]=RX [3]=RY [4]=L2 [5]=R2
} uart_peer_event_t;

_Static_assert(sizeof(uart_peer_event_t) == 12, "uart_peer_event_t must be 12 bytes");

// Extended input state (touch + motion) for MSG_EXT. Kept separate from the
// 12-byte event so uart_peer_event_t stays byte-identical to i2c_peer_event_t.
// Touch coords are the device-agnostic normalized 0..65535 (input_event_t.touch);
// accel/gyro are raw int16 as the source driver produced them.
typedef struct __attribute__((packed)) {
    uint8_t  player_index;      // player/slot this extends (matches the EVENT)
    uint8_t  flags;             // bit0 = has_touch, bit1 = has_motion
    uint8_t  touch_active;      // bit0 = touch[0] active, bit1 = touch[1] active
    uint16_t touch_x[2];        // normalized 0..65535
    uint16_t touch_y[2];        // normalized 0..65535
    int16_t  accel[3];          // raw accelerometer X,Y,Z
    int16_t  gyro[3];           // raw gyroscope X,Y,Z
    uint16_t gyro_range;        // dps full-scale (for output-side scaling)
    uint16_t accel_range;       // milli-g full-scale
} uart_peer_ext_t;

#define UART_PEER_EXT_FLAG_TOUCH   (1 << 0)
#define UART_PEER_EXT_FLAG_MOTION  (1 << 1)

_Static_assert(sizeof(uart_peer_ext_t) == 27, "uart_peer_ext_t must be 27 bytes");

// Feedback status (consumer -> producer) — same shape as i2c_peer_status_t.
typedef struct __attribute__((packed)) {
    uint8_t  flags;             // bit0=connected, bit1=name_valid
    uint8_t  transport;         // input_transport_t
    uint8_t  device_type;       // input_device_type_t
    uint8_t  player_number;     // 1-based (0=unassigned)
    uint8_t  usb_mode;          // usb_output_mode_t
    uint8_t  mode_color[3];     // RGB for current USB mode
    uint8_t  rumble_left;       // Heavy motor 0-255
    uint8_t  rumble_right;      // Light motor 0-255
    uint8_t  led_player;        // LED player pattern
    uint8_t  led_color[3];      // LED RGB from host feedback
    char     name[32];          // Device name, null-terminated
} uart_peer_status_t;

#define UART_PEER_STATUS_FLAG_CONNECTED  (1 << 0)
#define UART_PEER_STATUS_FLAG_NAME_VALID (1 << 1)

typedef struct {
    uint8_t  uart_inst;         // UART instance (0 or 1)
    uint8_t  tx_pin, rx_pin;    // TX/RX GPIO
    uint8_t  cts_pin, rts_pin;  // CTS/RTS GPIO (used if flow_control)
    uint32_t baud;              // 0 = UART_PEER_DEFAULT_BAUD
    bool     flow_control;      // enable RTS/CTS hardware flow control
} uart_peer_config_t;

// ----- shared (both sides) -----
// Initialize the UART link.
void uart_peer_init(const uart_peer_config_t* config);
// Pump RX (decode + dispatch frames) and drain the TX ring. Call every loop.
void uart_peer_task(void);
// True if a valid frame was received within the link timeout.
bool uart_peer_is_connected(void);
// Link RX diagnostics: total raw bytes seen on the wire / valid frames decoded.
uint32_t uart_peer_get_rx_raw_count(void);
uint32_t uart_peer_get_rx_frame_count(void);
// Send a raw framed message of the given type. plen must be <= 64. Used by the
// P5General auth bridge to carry the p5general_auth_data handoff across the link.
void uart_peer_send_frame(uint8_t type, const void* payload, uint16_t plen);

// ----- producer side (USB host MCU) -----
// Router tap: serialize input_event_t -> EVENT frame. Install via router_set_tap().
void uart_peer_producer_tap(output_target_t output, uint8_t player_index,
                            const input_event_t* event);
// Latest feedback from the consumer; returns true and copies if new since last call.
bool uart_peer_get_status(uart_peer_status_t* out);
// Send a diagnostic heartbeat to the consumer (producer side).
void uart_peer_send_debug(const uart_peer_debug_t* dbg);

// ----- consumer side (USB device MCU) -----
// Send feedback (rumble/LED/player) to the producer.
void uart_peer_send_status(const uart_peer_status_t* status);
// Latest diagnostic heartbeat from the producer; true + copies if new since last call.
bool uart_peer_get_debug(uart_peer_debug_t* out);
// Device name learned from the producer (for router get_device_name()).
const char* uart_peer_get_device_name(void);
// Router input interface (source = INPUT_SOURCE_UART_PEER).
extern const InputInterface uart_peer_input_interface;

#endif // UART_PEER_H
