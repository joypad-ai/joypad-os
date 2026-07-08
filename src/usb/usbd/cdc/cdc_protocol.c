// cdc_protocol.c - Binary framed CDC protocol implementation
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "cdc_protocol.h"
#include "cdc.h"
#include "platform/platform.h"
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

    // Mid-frame timeout: a frame torn by a killed writer or a USB drop leaves
    // the parser waiting inside a phantom frame — worse, hunting resync can
    // land on the '{' INSIDE a binary frame's JSON payload and drop into text
    // mode, waiting for a newline that framed traffic never contains. That
    // wedged the command channel permanently ("commands ignored until fresh
    // boot"). Frames arrive as contiguous bursts, so any mid-frame gap this
    // long means the frame is dead: resync.
    uint32_t rx_now = platform_time_ms();
    if (rx->state != CDC_RX_SYNC && (uint32_t)(rx_now - rx->last_rx_ms) > 300) {
        rx->state = CDC_RX_SYNC;
        rx->payload_pos = 0;
        ctx->text_mode = false;
    }
    rx->last_rx_ms = rx_now;

    switch (rx->state) {
        case CDC_RX_SYNC:
            if (byte == CDC_SYNC_BYTE) {
                rx->state = CDC_RX_LEN_LO;
                rx->payload_pos = 0;
            } else if (byte == '{') {
                // Text-command mode: a bare newline-delimited JSON object, for
                // humans and tools whose serial send is text rather than our
                // binary framing (e.g. COMrade's send_serial, a plain terminal).
                // Response goes back as text too (see cdc_protocol_send_response).
                ctx->text_mode = true;
                rx->state = CDC_RX_TEXT;
                rx->payload_pos = 0;
                rx->packet.payload[rx->payload_pos++] = byte;  // keep the '{'
            }
            // Else: keep scanning for sync
            break;

        case CDC_RX_TEXT:
            if (byte == '\n' || byte == '\r') {
                // End of line → dispatch the accumulated JSON as a command.
                if (rx->payload_pos > 1) {
                    if (rx->payload_pos < CDC_MAX_PAYLOAD) {
                        rx->packet.payload[rx->payload_pos] = 0;  // null-terminate
                    }
                    rx->packet.type = CDC_MSG_CMD;
                    rx->packet.seq = 0;
                    rx->packet.length = rx->payload_pos;
                    ctx->cmd_seq = 0;
                    if (ctx->handler) {
                        ctx->handler(&rx->packet);
                    }
                }
                rx->state = CDC_RX_SYNC;
                rx->payload_pos = 0;
                return true;
            } else if (rx->payload_pos < CDC_MAX_PAYLOAD) {
                rx->packet.payload[rx->payload_pos++] = byte;
            } else {
                // Line too long — abandon and resync.
                rx->state = CDC_RX_SYNC;
                rx->payload_pos = 0;
                ctx->text_mode = false;
            }
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
                    ctx->text_mode = false;  // binary in → binary response
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

    // Send via transport (custom write function or USB CDC default)
    if (ctx->write) {
        return ctx->write(packet, pos);
    }
    return cdc_data_write(packet, pos);
}

uint16_t cdc_protocol_send_response(cdc_protocol_t* ctx, const char* json)
{
    // Text-mode command → reply as plain JSON + newline so a serial terminal /
    // COMrade shows a readable line instead of a binary frame. Uses the same
    // transport selection as cdc_protocol_send (custom write or USB CDC default).
    if (ctx->text_mode) {
        uint16_t n = (uint16_t)strlen(json);
        if (n > CDC_MAX_PAYLOAD) n = CDC_MAX_PAYLOAD;
        uint8_t line[CDC_MAX_PAYLOAD + 2];
        memcpy(line, json, n);
        line[n++] = '\r';
        line[n++] = '\n';
        if (ctx->write) {
            return ctx->write(line, n);
        }
        return cdc_data_write(line, n);
    }
    return cdc_protocol_send(ctx, CDC_MSG_RSP, ctx->cmd_seq,
                             (const uint8_t*)json, strlen(json));
}

// Cap on how much streaming data can sit in the CDC TX buffer at once.
// tud_cdc_n_write_flush only sends ONE 64-byte EP packet per call and
// each USB bulk transfer takes ~1ms. A 110-byte event needs 2 transfers
// (64 + 46), so even ONE in-flight event leaves residual that the next
// event has to queue behind. Setting the limit to 0 (FIFO fully empty)
// guarantees each streaming event has the whole TX path to itself:
// minimum host-visible latency, no backlog stacking. Drop rate is high
// when the host polls slowly, but web tool only cares about LATEST
// state so a dropped event is no worse than not sampling that frame.
// Command RESPONSES bypass this — they must arrive.
#ifndef CDC_STREAM_QUEUED_LIMIT
#define CDC_STREAM_QUEUED_LIMIT 0
#endif

#if CFG_TUD_CDC
extern uint32_t tud_cdc_n_write_available(uint8_t itf);
#ifndef CFG_TUD_CDC_TX_BUFSIZE
#define CFG_TUD_CDC_TX_BUFSIZE 256
#endif
#endif

static bool stream_tx_backlogged(cdc_protocol_t* ctx)
{
    // Custom transport (e.g. BLE NUS) — let it manage its own flow.
    if (ctx->write) return false;
#if CFG_TUD_CDC
    uint32_t avail = tud_cdc_n_write_available(0);
    uint32_t queued = (CFG_TUD_CDC_TX_BUFSIZE > avail)
                    ? (CFG_TUD_CDC_TX_BUFSIZE - avail) : 0;
    return queued > CDC_STREAM_QUEUED_LIMIT;
#else
    return false;
#endif
}

uint16_t cdc_protocol_send_event(cdc_protocol_t* ctx, const char* json)
{
    if (stream_tx_backlogged(ctx)) return 0;
    uint8_t seq = ctx->tx_seq++;
    return cdc_protocol_send(ctx, CDC_MSG_EVT, seq,
                             (const uint8_t*)json, strlen(json));
}

uint16_t cdc_protocol_send_nak(cdc_protocol_t* ctx, uint8_t seq)
{
    return cdc_protocol_send(ctx, CDC_MSG_NAK, seq, NULL, 0);
}
