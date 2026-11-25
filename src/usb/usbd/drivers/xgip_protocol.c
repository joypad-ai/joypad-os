// xgip_protocol.c - Xbox Game Input Protocol handler
// SPDX-License-Identifier: MIT
// Based on GP2040-CE implementation (gp2040-ce.info)

#include "xgip_protocol.h"
#include <string.h>
#include <stdlib.h>

void xgip_init(xgip_t* xgip)
{
    memset(xgip, 0, sizeof(xgip_t));
    xgip->data = NULL;
}

void xgip_reset(xgip_t* xgip)
{
    memset(&xgip->header, 0, sizeof(gip_header_t));
    xgip->total_chunk_length = 0;
    xgip->actual_data_received = 0;
    xgip->total_chunk_received = 0;
    xgip->total_chunk_sent = 0;
    xgip->total_data_sent = 0;
    xgip->num_chunks_sent = 0;
    xgip->chunk_ended = false;
    xgip->is_valid = false;

    if (xgip->data != NULL) {
        free(xgip->data);
        xgip->data = NULL;
    }
    xgip->data_length = 0;

    memset(xgip->packet, 0, sizeof(xgip->packet));
    xgip->packet_length = 0;
}

bool xgip_parse(xgip_t* xgip, const uint8_t* buffer, uint16_t len)
{
    // Need at least header
    if (len < 4) {
        xgip_reset(xgip);
        xgip->is_valid = false;
        return false;
    }

    xgip->packet_length = len;
    gip_header_t* new_header = (gip_header_t*)buffer;

    // Handle ACK response
    if (new_header->command == GIP_ACK_RESPONSE) {
        if (len != 13 || new_header->internal != 0x01 || new_header->length != 0x09) {
            xgip_reset(xgip);
            xgip->is_valid = false;
            return false;
        }
        memcpy(&xgip->header, buffer, sizeof(gip_header_t));
        xgip->is_valid = true;
        return true;
    }

    // Handle chunked data
    if (new_header->chunked) {
        memcpy(&xgip->header, buffer, sizeof(gip_header_t));

        if (xgip->header.length == 0) {
            // End of chunk marker
            if (len < 6) {
                xgip->is_valid = false;
                return false;
            }
            uint16_t end_chunk_size = (buffer[4] | (buffer[5] << 8));
            if (xgip->total_chunk_length != end_chunk_size) {
                xgip->is_valid = false;
                return false;
            }
            xgip->chunk_ended = true;
            xgip->is_valid = true;
            return true;
        }

        // Every chunk carries a 2-byte length/offset after the header, then data.
        // The header's length byte comes from the device: never read past
        // what was actually received.
        uint16_t copy_len = xgip->header.length;
        if (copy_len > GIP_MAX_CHUNK_SIZE) {
            copy_len ^= 0x80;  // Remove encoding
        }
        if (len < 6 || copy_len > len - 6) {
            xgip_reset(xgip);
            xgip->is_valid = false;
            return false;
        }

        if (xgip->header.chunk_start) {
            // Start of new chunk
            xgip_reset(xgip);
            memcpy(&xgip->header, buffer, sizeof(gip_header_t));

            // Calculate total chunk length
            if (xgip->header.length > GIP_MAX_CHUNK_SIZE && buffer[4] == 0x00) {
                // Single-byte mode
                xgip->total_chunk_length = buffer[5];
            } else {
                xgip->total_chunk_length = (buffer[4] | (buffer[5] << 8));
            }

            // Calculate actual data length
            xgip->data_length = xgip->total_chunk_length;
            if (xgip->total_chunk_length > 0x100) {
                xgip->data_length = xgip->data_length - 0x100;
                xgip->data_length = xgip->data_length - ((xgip->data_length / 0x100) * 0x80);
            }

            // Allocate data buffer
            if (xgip->data != NULL) {
                free(xgip->data);
            }
            xgip->data = (uint8_t*)malloc(xgip->data_length);
            if (xgip->data == NULL) {
                xgip->is_valid = false;
                return false;
            }

            xgip->actual_data_received = 0;
            xgip->total_chunk_received = xgip->header.length;
        } else {
            xgip->total_chunk_received += xgip->header.length;
        }

        // Copy data (copy_len checked against len above)
        if (xgip->data && (xgip->actual_data_received + copy_len) <= xgip->data_length) {
            memcpy(&xgip->data[xgip->actual_data_received], &buffer[6], copy_len);
            xgip->actual_data_received += copy_len;
        }
        xgip->num_chunks_sent++;
        xgip->is_valid = true;

        // Some controllers (e.g. MS Xbox Wireless 0x045E:0x0B12) don't send an
        // explicit length=0 end-of-chunk marker — they rely on actual_data_received
        // reaching the declared data_length. Mark complete in that case too.
        if (xgip->data_length > 0 && xgip->actual_data_received >= xgip->data_length) {
            xgip->chunk_ended = true;
        }

    } else {
        // Non-chunked data
        xgip_reset(xgip);
        memcpy(&xgip->header, buffer, sizeof(gip_header_t));

        // The header's length byte comes from the device: never read past
        // what was actually received.
        if (xgip->header.length > len - 4) {
            xgip_reset(xgip);
            xgip->is_valid = false;
            return false;
        }

        if (xgip->header.length > 0) {
            xgip->data = (uint8_t*)malloc(xgip->header.length);
            if (xgip->data != NULL) {
                memcpy(xgip->data, &buffer[4], xgip->header.length);
            }
        }
        xgip->actual_data_received = xgip->header.length;
        xgip->data_length = xgip->actual_data_received;
        xgip->is_valid = true;
    }

    return false;
}

bool xgip_validate(xgip_t* xgip)
{
    return xgip->is_valid;
}

bool xgip_end_of_chunk(xgip_t* xgip)
{
    return xgip->chunk_ended;
}

bool xgip_ack_required(xgip_t* xgip)
{
    return xgip->header.needs_ack;
}

void xgip_set_attributes(xgip_t* xgip, uint8_t cmd, uint8_t seq,
                         uint8_t internal, uint8_t is_chunked, uint8_t needs_ack)
{
    xgip->header.command = cmd;
    xgip->header.sequence = seq;
    xgip->header.internal = internal;
    xgip->header.chunked = is_chunked;
    xgip->header.needs_ack = needs_ack;
}

bool xgip_set_data(xgip_t* xgip, const uint8_t* data, uint16_t len)
{
    if (len > XGIP_MAX_DATA_SIZE) {
        return false;
    }

    if (xgip->data != NULL) {
        free(xgip->data);
    }

    xgip->data = (uint8_t*)malloc(len);
    if (xgip->data == NULL) {
        return false;
    }

    memcpy(xgip->data, data, len);
    xgip->data_length = len;
    return true;
}

uint8_t* xgip_generate_packet(xgip_t* xgip)
{
    if (!xgip->header.chunked) {
        // Simple non-chunked packet
        xgip->header.length = (uint8_t)xgip->data_length;
        memcpy(xgip->packet, &xgip->header, sizeof(gip_header_t));
        if (xgip->data && xgip->data_length > 0) {
            memcpy(&xgip->packet[4], xgip->data, xgip->data_length);
        }
        xgip->packet_length = sizeof(gip_header_t) + xgip->data_length;
    } else {
        // Chunked packet handling
        if (xgip->num_chunks_sent > 0 && xgip->total_data_sent == xgip->data_length) {
            // Final chunk (end marker)
            xgip->header.needs_ack = 0;
            xgip->header.length = 0;
            memcpy(xgip->packet, &xgip->header, sizeof(gip_header_t));
            xgip->packet[4] = xgip->total_chunk_length & 0xFF;
            xgip->packet[5] = (xgip->total_chunk_length >> 8) & 0xFF;
            xgip->packet_length = sizeof(gip_header_t) + 2;
            xgip->chunk_ended = true;
        } else {
            if (xgip->num_chunks_sent == 0) {
                if (xgip->data_length < GIP_MAX_CHUNK_SIZE) {
                    // Small chunk - treat as non-chunked but still needs ACK
                    xgip->total_chunk_length = xgip->data_length;
                    xgip->header.chunk_start = 0;
                    xgip->header.chunked = 0;
                } else {
                    xgip->header.chunk_start = 1;

                    // Calculate chunk length with 0x80 boundary encoding
                    uint16_t remaining = xgip->data_length;
                    uint16_t chunk_total = 0;

                    while (remaining > 0) {
                        uint16_t chunk_size;
                        if (remaining < GIP_MAX_CHUNK_SIZE) {
                            chunk_size = remaining;
                        } else {
                            chunk_size = GIP_MAX_CHUNK_SIZE;
                        }

                        // Handle 0x100 boundary
                        if ((chunk_total + chunk_size > 0x80) && (chunk_total + chunk_size < 0x100)) {
                            chunk_total = chunk_total + chunk_size + 0x100;
                        } else if ((chunk_total / 0x100) != ((chunk_total + chunk_size) / 0x100)) {
                            chunk_total = chunk_total + (chunk_size | 0x80);
                        } else {
                            chunk_total = chunk_total + chunk_size;
                        }

                        remaining -= chunk_size;
                    }
                    xgip->total_chunk_length = chunk_total;
                }
            } else {
                xgip->header.chunk_start = 0;
            }

            // Set ACK flag on 1st and every 5th chunk
            if (xgip->num_chunks_sent == 0 || (xgip->num_chunks_sent + 1) % 5 == 0) {
                xgip->header.needs_ack = 1;
            } else {
                xgip->header.needs_ack = 0;
            }

            // Determine data to send in this chunk
            uint16_t data_to_send = GIP_MAX_CHUNK_SIZE;
            if ((xgip->data_length - xgip->total_data_sent) < data_to_send) {
                data_to_send = xgip->data_length - xgip->total_data_sent;
                xgip->header.needs_ack = 1;
            }

            // Set length with 0x80 encoding if needed
            if (xgip->num_chunks_sent > 0 && xgip->total_chunk_sent < 0x100) {
                xgip->header.length = data_to_send | 0x80;
            } else if (xgip->num_chunks_sent == 0 && xgip->data_length > GIP_MAX_CHUNK_SIZE && xgip->data_length < 0x80) {
                xgip->header.length = data_to_send | 0x80;
            } else {
                xgip->header.length = data_to_send;
            }

            // Build packet
            memcpy(xgip->packet, &xgip->header, sizeof(gip_header_t));
            if (xgip->data) {
                memcpy(&xgip->packet[6], &xgip->data[xgip->total_data_sent], data_to_send);
            }
            xgip->packet_length = sizeof(gip_header_t) + 2 + data_to_send;

            // Set chunk offset value
            uint16_t chunk_value;
            if (xgip->num_chunks_sent == 0) {
                chunk_value = xgip->total_chunk_length;
            } else {
                chunk_value = xgip->total_chunk_sent;
            }

            if (chunk_value < 0x100) {
                xgip->packet[4] = 0x00;
                xgip->packet[5] = (uint8_t)chunk_value;
            } else {
                xgip->packet[4] = chunk_value & 0xFF;
                xgip->packet[5] = (chunk_value >> 8) & 0xFF;
            }

            // Update chunk tracking with 0x80 boundary encoding
            if (xgip->total_chunk_sent < 0x100 && (xgip->total_chunk_sent + data_to_send) > 0x80) {
                xgip->total_chunk_sent = xgip->total_chunk_sent + data_to_send + 0x100;
            } else if (((xgip->total_chunk_sent + data_to_send) / 0x100) > (xgip->total_chunk_sent / 0x100)) {
                xgip->total_chunk_sent = xgip->total_chunk_sent + (data_to_send | 0x80);
            } else {
                xgip->total_chunk_sent = xgip->total_chunk_sent + data_to_send;
            }

            xgip->total_data_sent += data_to_send;
            xgip->num_chunks_sent++;
        }
    }

    return xgip->packet;
}

uint8_t* xgip_generate_ack(xgip_t* xgip)
{
    xgip->packet[0] = 0x01;  // GIP_ACK_RESPONSE
    xgip->packet[1] = 0x20;
    xgip->packet[2] = xgip->header.sequence;
    xgip->packet[3] = 0x09;  // Length
    xgip->packet[4] = 0x00;
    xgip->packet[5] = xgip->header.command;
    xgip->packet[6] = 0x20;

    // Bytes received
    xgip->packet[7] = xgip->actual_data_received & 0xFF;
    xgip->packet[8] = (xgip->actual_data_received >> 8) & 0xFF;
    xgip->packet[9] = 0x00;
    xgip->packet[10] = 0x00;

    // Bytes remaining (for chunked)
    if (xgip->header.chunked) {
        uint16_t remaining = xgip->data_length - xgip->actual_data_received;
        xgip->packet[11] = remaining & 0xFF;
        xgip->packet[12] = (remaining >> 8) & 0xFF;
    } else {
        xgip->packet[11] = 0;
        xgip->packet[12] = 0;
    }

    xgip->packet_length = 13;
    return xgip->packet;
}

uint8_t xgip_get_command(xgip_t* xgip)
{
    return xgip->header.command;
}

uint8_t xgip_get_sequence(xgip_t* xgip)
{
    return xgip->header.sequence;
}

bool xgip_is_chunked(xgip_t* xgip)
{
    return xgip->header.chunked;
}

uint8_t xgip_get_packet_length(xgip_t* xgip)
{
    return xgip->packet_length;
}

bool xgip_get_packet_ack(xgip_t* xgip)
{
    return xgip->header.needs_ack;
}

uint8_t* xgip_get_data(xgip_t* xgip)
{
    return xgip->data;
}

uint16_t xgip_get_data_length(xgip_t* xgip)
{
    return xgip->data_length;
}

void xgip_increment_sequence(xgip_t* xgip)
{
    xgip->header.sequence++;
    if (xgip->header.sequence == 0) {
        xgip->header.sequence = 1;
    }
}
