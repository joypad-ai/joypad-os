// cdc_protocol.h - Binary framed CDC protocol
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Provides reliable bidirectional communication over CDC with:
// - Packet framing (sync byte, length, CRC)
// - Request/response correlation (sequence numbers)
// - Async events (input, connect/disconnect)
// - Flow control (ACK/NAK)

#ifndef CDC_PROTOCOL_H
#define CDC_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// CONSTANTS
// ============================================================================

#define CDC_SYNC_BYTE       0xAA
#define CDC_MAX_PAYLOAD     512     // Max payload size (JSON commands)
#define CDC_HEADER_SIZE     5       // sync(1) + len(2) + type(1) + seq(1)
#define CDC_CRC_SIZE        2
#define CDC_MAX_PACKET      (CDC_HEADER_SIZE + CDC_MAX_PAYLOAD + CDC_CRC_SIZE)

// ============================================================================
// MESSAGE TYPES
// ============================================================================

typedef enum {
    CDC_MSG_CMD = 0x01,     // Command from host
    CDC_MSG_RSP = 0x02,     // Response to command (SEQ matches)
    CDC_MSG_EVT = 0x03,     // Async event from device
    CDC_MSG_ACK = 0x04,     // Acknowledgment
    CDC_MSG_NAK = 0x05,     // Negative ack (resend/error)
    CDC_MSG_DAT = 0x10,     // Data stream chunk
} cdc_msg_type_t;

// ============================================================================
// PACKET STRUCTURE
// ============================================================================

// Wire format:
// [SYNC:1][LENGTH:2][TYPE:1][SEQ:1][PAYLOAD:LENGTH][CRC:2]
//
// LENGTH is little-endian, payload size only (not including header/crc)
// CRC is CRC-16-CCITT over TYPE+SEQ+PAYLOAD

typedef struct {
    uint8_t type;               // cdc_msg_type_t
    uint8_t seq;                // Sequence number
    uint16_t length;            // Payload length
    uint8_t payload[CDC_MAX_PAYLOAD];
} cdc_packet_t;

// ============================================================================
// RECEIVER STATE MACHINE
// ============================================================================

typedef enum {
    CDC_RX_SYNC,        // Waiting for sync byte
    CDC_RX_LEN_LO,      // Waiting for length low byte
    CDC_RX_LEN_HI,      // Waiting for length high byte
    CDC_RX_TYPE,        // Waiting for type byte
    CDC_RX_SEQ,         // Waiting for sequence byte
    CDC_RX_PAYLOAD,     // Receiving payload
    CDC_RX_CRC_LO,      // Waiting for CRC low byte
    CDC_RX_CRC_HI,      // Waiting for CRC high byte
} cdc_rx_state_t;

typedef struct {
    cdc_rx_state_t state;
    cdc_packet_t packet;
    uint16_t payload_pos;
    uint16_t crc_received;
} cdc_receiver_t;

// ============================================================================
// PROTOCOL CONTEXT
// ============================================================================

// Callback for received packets
typedef void (*cdc_packet_handler_t)(const cdc_packet_t* packet);

typedef struct {
    cdc_receiver_t rx;
    uint8_t tx_seq;             // Next TX sequence number (for EVT)
    uint8_t cmd_seq;            // Last received CMD sequence (for RSP)
    cdc_packet_handler_t handler;
    bool input_streaming;       // Input event streaming enabled
} cdc_protocol_t;

// ============================================================================
// API
// ============================================================================

// Initialize protocol handler
void cdc_protocol_init(cdc_protocol_t* ctx, cdc_packet_handler_t handler);

// Process incoming byte (call for each byte received)
// Returns true if a complete valid packet was received
bool cdc_protocol_rx_byte(cdc_protocol_t* ctx, uint8_t byte);

// Reset receiver state (on timeout or error)
void cdc_protocol_rx_reset(cdc_protocol_t* ctx);

// Build and send a packet
// Returns number of bytes written, or 0 on error
uint16_t cdc_protocol_send(cdc_protocol_t* ctx, cdc_msg_type_t type,
                           uint8_t seq, const uint8_t* payload, uint16_t len);

// Convenience: send response to last command
uint16_t cdc_protocol_send_response(cdc_protocol_t* ctx,
                                    const char* json);

// Convenience: send async event
uint16_t cdc_protocol_send_event(cdc_protocol_t* ctx,
                                 const char* json);

// Convenience: send NAK
uint16_t cdc_protocol_send_nak(cdc_protocol_t* ctx, uint8_t seq);

// ============================================================================
// CRC
// ============================================================================

// CRC-16-CCITT (poly 0x1021, init 0xFFFF)
uint16_t cdc_crc16(const uint8_t* data, size_t len);

#ifdef __cplusplus
}
#endif

#endif // CDC_PROTOCOL_H
