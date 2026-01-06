// cdc_protocol.c - Binary framed CDC protocol implementation
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "cdc_protocol.h"
#include "cdc.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// CRC-16-CCITT
// ============================================================================

uint16_t cdc_crc16(const uint8_t* data, size_t len)
{
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i] << 8;
        for (int j = 0; j < 8; j++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc = crc << 1;
            }
        }
    }
    return crc;
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void cdc_protocol_init(cdc_protocol_t* ctx, cdc_packet_handler_t handler)
{
    memset(ctx, 0, sizeof(cdc_protocol_t));
    ctx->handler = handler;
    ctx->rx.state = CDC_RX_SYNC;
}

void cdc_protocol_rx_reset(cdc_protocol_t* ctx)
{
    ctx->rx.state = CDC_RX_SYNC;
    ctx->rx.payload_pos = 0;
}

// ============================================================================
// RECEIVER STATE MACHINE
// ============================================================================

bool cdc_protocol_rx_byte(cdc_protocol_t* ctx, uint8_t byte)
{
    cdc_receiver_t* rx = &ctx->rx;

    switch (rx->state) {
        case CDC_RX_SYNC:
            if (byte == CDC_SYNC_BYTE) {
                rx->state = CDC_RX_LEN_LO;
                rx->payload_pos = 0;
            }
            // Else: keep scanning for sync
            break;

        case CDC_RX_LEN_LO:
            rx->packet.length = byte;
            rx->state = CDC_RX_LEN_HI;
            break;

        case CDC_RX_LEN_HI:
            rx->packet.length |= (uint16_t)byte << 8;
            if (rx->packet.length > CDC_MAX_PAYLOAD) {
                // Invalid length, resync
                rx->state = CDC_RX_SYNC;
            } else {
                rx->state = CDC_RX_TYPE;
            }
            break;

        case CDC_RX_TYPE:
            rx->packet.type = byte;
            rx->state = CDC_RX_SEQ;
            break;

        case CDC_RX_SEQ:
            rx->packet.seq = byte;
            if (rx->packet.length == 0) {
                // No payload, go straight to CRC
                rx->state = CDC_RX_CRC_LO;
            } else {
                rx->state = CDC_RX_PAYLOAD;
            }
            break;

        case CDC_RX_PAYLOAD:
            rx->packet.payload[rx->payload_pos++] = byte;
            if (rx->payload_pos >= rx->packet.length) {
                rx->state = CDC_RX_CRC_LO;
            }
            break;

        case CDC_RX_CRC_LO:
            rx->crc_received = byte;
            rx->state = CDC_RX_CRC_HI;
            break;

        case CDC_RX_CRC_HI:
            rx->crc_received |= (uint16_t)byte << 8;
            rx->state = CDC_RX_SYNC;  // Ready for next packet

            // Calculate CRC over type + seq + payload
            uint8_t crc_buf[2 + CDC_MAX_PAYLOAD];
            crc_buf[0] = rx->packet.type;
            crc_buf[1] = rx->packet.seq;
            memcpy(&crc_buf[2], rx->packet.payload, rx->packet.length);
            uint16_t crc_calc = cdc_crc16(crc_buf, 2 + rx->packet.length);

            if (crc_calc == rx->crc_received) {
                // Valid packet - save seq for response and call handler
                if (rx->packet.type == CDC_MSG_CMD) {
                    ctx->cmd_seq = rx->packet.seq;
                }
                if (ctx->handler) {
                    ctx->handler(&rx->packet);
                }
                return true;
            } else {
                // CRC mismatch - send NAK
                printf("[cdc] CRC error: got 0x%04X, expected 0x%04X\n",
                       rx->crc_received, crc_calc);
                cdc_protocol_send_nak(ctx, rx->packet.seq);
            }
            break;
    }

    return false;
}

// ============================================================================
// TRANSMITTER
// ============================================================================

uint16_t cdc_protocol_send(cdc_protocol_t* ctx, cdc_msg_type_t type,
                           uint8_t seq, const uint8_t* payload, uint16_t len)
{
    if (len > CDC_MAX_PAYLOAD) {
        return 0;
    }

    // Build packet
    uint8_t packet[CDC_MAX_PACKET];
    uint16_t pos = 0;

    // Header
    packet[pos++] = CDC_SYNC_BYTE;
    packet[pos++] = len & 0xFF;
    packet[pos++] = (len >> 8) & 0xFF;
    packet[pos++] = type;
    packet[pos++] = seq;

    // Payload
    if (len > 0 && payload) {
        memcpy(&packet[pos], payload, len);
        pos += len;
    }

    // CRC over type + seq + payload
    uint8_t crc_buf[2 + CDC_MAX_PAYLOAD];
    crc_buf[0] = type;
    crc_buf[1] = seq;
    if (len > 0 && payload) {
        memcpy(&crc_buf[2], payload, len);
    }
    uint16_t crc = cdc_crc16(crc_buf, 2 + len);
    packet[pos++] = crc & 0xFF;
    packet[pos++] = (crc >> 8) & 0xFF;

    // Send via CDC data port
    return cdc_data_write(packet, pos);
}

uint16_t cdc_protocol_send_response(cdc_protocol_t* ctx, const char* json)
{
    return cdc_protocol_send(ctx, CDC_MSG_RSP, ctx->cmd_seq,
                             (const uint8_t*)json, strlen(json));
}

uint16_t cdc_protocol_send_event(cdc_protocol_t* ctx, const char* json)
{
    uint8_t seq = ctx->tx_seq++;
    return cdc_protocol_send(ctx, CDC_MSG_EVT, seq,
                             (const uint8_t*)json, strlen(json));
}

uint16_t cdc_protocol_send_nak(cdc_protocol_t* ctx, uint8_t seq)
{
    return cdc_protocol_send(ctx, CDC_MSG_NAK, seq, NULL, 0);
}
