// xgip_protocol.h - Xbox Game Input Protocol handler
// SPDX-License-Identifier: MIT
// Based on GP2040-CE implementation (gp2040-ce.info)
//
// XGIP (Xbox Game Input Protocol) is used for Xbox One controller communication.
// This implementation handles packet parsing, generation, and chunked transfers.

#ifndef XGIP_PROTOCOL_H
#define XGIP_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "../descriptors/xbone_descriptors.h"

// Maximum data buffer size for auth packets
#define XGIP_MAX_DATA_SIZE 1024

// XGIP Protocol state
typedef struct {
    gip_header_t header;

    // Chunk tracking
    uint16_t total_chunk_length;    // Total size of chunked data
    uint16_t actual_data_received;  // How much actual data received
    uint16_t total_chunk_received;  // Chunk counter (with 0x80 encoding)
    uint16_t total_chunk_sent;      // How much we've sent
    uint16_t total_data_sent;       // Actual data bytes sent
    uint16_t num_chunks_sent;       // Number of chunk packets sent
    bool chunk_ended;               // End of chunk reached

    // Output packet buffer
    uint8_t packet[64];
    uint8_t packet_length;

    // Data buffer (for incoming or outgoing data)
    uint8_t* data;
    uint16_t data_length;

    bool is_valid;                  // Is this a valid packet?
} xgip_t;

// Initialize XGIP protocol state
void xgip_init(xgip_t* xgip);

// Reset XGIP state for new packet
void xgip_reset(xgip_t* xgip);

// Parse incoming packet
// Returns true if packet is complete (non-chunked or end of chunk)
bool xgip_parse(xgip_t* xgip, const uint8_t* buffer, uint16_t len);

// Check if parsed packet is valid
bool xgip_validate(xgip_t* xgip);

// Check if we're at end of chunked data
bool xgip_end_of_chunk(xgip_t* xgip);

// Check if last parsed packet requires ACK
bool xgip_ack_required(xgip_t* xgip);

// Set attributes for outgoing packet
void xgip_set_attributes(xgip_t* xgip, uint8_t cmd, uint8_t seq,
                         uint8_t internal, uint8_t is_chunked, uint8_t needs_ack);

// Set data for outgoing packet
// Returns false if data is too large
bool xgip_set_data(xgip_t* xgip, const uint8_t* data, uint16_t len);

// Generate next output packet (handles chunking automatically)
// Returns pointer to packet buffer
uint8_t* xgip_generate_packet(xgip_t* xgip);

// Generate ACK packet for last received packet
// Returns pointer to packet buffer
uint8_t* xgip_generate_ack(xgip_t* xgip);

// Get command from parsed packet
uint8_t xgip_get_command(xgip_t* xgip);

// Get sequence number from parsed packet
uint8_t xgip_get_sequence(xgip_t* xgip);

// Is packet chunked?
bool xgip_is_chunked(xgip_t* xgip);

// Get packet length of last generated output
uint8_t xgip_get_packet_length(xgip_t* xgip);

// Did last generated packet require ACK?
bool xgip_get_packet_ack(xgip_t* xgip);

// Get data from parsed packet
uint8_t* xgip_get_data(xgip_t* xgip);

// Get data length from parsed packet
uint16_t xgip_get_data_length(xgip_t* xgip);

// Increment sequence number
void xgip_increment_sequence(xgip_t* xgip);

#endif // XGIP_PROTOCOL_H
