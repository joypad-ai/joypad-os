// ble_nus.c - BLE Nordic UART Service (NUS) bridge for CDC protocol
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Bridges the CDC binary protocol over BLE NUS (Nordic UART Service),
// enabling wireless configuration using the same command set as USB CDC.
// RX: NUS write characteristic → cdc_protocol_rx_byte() → command dispatch
// TX: cdc_protocol_send() → nordic_spp_service_server_send() (chunked by MTU)

#include "ble_nus.h"
#include "usb/usbd/cdc/cdc_protocol.h"
#include "usb/usbd/cdc/cdc_commands.h"

#include "btstack_defines.h"
#include "btstack_event.h"
#include "ble/att_server.h"
#include "ble/gatt-service/nordic_spp_service_server.h"
#include "gap.h"

#include <stdio.h>
#include <string.h>

// ============================================================================
// STATE
// ============================================================================

static cdc_protocol_t nus_protocol_ctx;
static hci_con_handle_t nus_con_handle = HCI_CON_HANDLE_INVALID;
static bool nus_connected = false;

// TX queue: ring of packets, each sent in MTU-sized chunks
#define NUS_TX_BUF_SIZE CDC_MAX_PACKET
#define NUS_TX_QUEUE_SIZE 4  // Number of queued packets

typedef struct {
    uint8_t data[NUS_TX_BUF_SIZE];
    uint16_t len;
} nus_tx_entry_t;

static nus_tx_entry_t nus_tx_queue[NUS_TX_QUEUE_SIZE];
static uint8_t nus_tx_head = 0;      // Next write slot
static uint8_t nus_tx_tail = 0;      // Currently sending slot
static uint16_t nus_tx_offset = 0;   // Bytes sent in current entry
static bool nus_tx_active = false;   // Currently chunking a packet
static btstack_context_callback_registration_t nus_send_request;

// ============================================================================
// TX CHUNKING
// ============================================================================

static uint16_t nus_get_max_chunk(void)
{
    uint16_t mtu = att_server_get_mtu(nus_con_handle);
    if (mtu < 3) return 20;  // fallback
    return mtu - 3;  // ATT notification overhead
}

static void nus_send_next_chunk(void *context)
{
    (void)context;

    if (nus_tx_tail == nus_tx_head) {
        // Queue empty
        nus_tx_active = false;
        nus_tx_offset = 0;
        return;
    }

    nus_tx_entry_t *entry = &nus_tx_queue[nus_tx_tail];

    if (nus_tx_offset >= entry->len) {
        // Current entry fully sent — advance to next
        nus_tx_tail = (nus_tx_tail + 1) % NUS_TX_QUEUE_SIZE;
        nus_tx_offset = 0;
        if (nus_tx_tail == nus_tx_head) {
            // Queue now empty
            nus_tx_active = false;
            return;
        }
        entry = &nus_tx_queue[nus_tx_tail];
    }

    uint16_t max_chunk = nus_get_max_chunk();
    uint16_t remaining = entry->len - nus_tx_offset;
    uint16_t chunk_len = remaining < max_chunk ? remaining : max_chunk;

    int result = nordic_spp_service_server_send(
        nus_con_handle, &entry->data[nus_tx_offset], chunk_len);

    if (result == 0) {
        nus_tx_offset += chunk_len;
        // If more data in this entry or more entries in queue, keep going
        if (nus_tx_offset >= entry->len) {
            nus_tx_tail = (nus_tx_tail + 1) % NUS_TX_QUEUE_SIZE;
            nus_tx_offset = 0;
            if (nus_tx_tail == nus_tx_head) {
                nus_tx_active = false;
                return;
            }
        }
        nordic_spp_service_server_request_can_send_now(
            &nus_send_request, nus_con_handle);
    } else {
        // Busy — request CAN_SEND_NOW to retry
        nordic_spp_service_server_request_can_send_now(
            &nus_send_request, nus_con_handle);
    }
}

// ============================================================================
// NUS TRANSPORT WRITE
// ============================================================================

static uint32_t nus_write(const uint8_t* data, uint16_t len)
{
    if (!nus_connected || nus_con_handle == HCI_CON_HANDLE_INVALID) {
        return 0;
    }

    if (len > NUS_TX_BUF_SIZE) {
        printf("[ble_nus] TX too large (%d > %d), dropping\n", len, NUS_TX_BUF_SIZE);
        return 0;
    }

    // Check if queue is full
    uint8_t next_head = (nus_tx_head + 1) % NUS_TX_QUEUE_SIZE;
    if (next_head == nus_tx_tail) {
        // Queue full — drop oldest entry (stream event) to make room
        nus_tx_tail = (nus_tx_tail + 1) % NUS_TX_QUEUE_SIZE;
        nus_tx_offset = 0;
    }

    // Enqueue the packet
    memcpy(nus_tx_queue[nus_tx_head].data, data, len);
    nus_tx_queue[nus_tx_head].len = len;
    nus_tx_head = next_head;

    // If not already sending, kick off transmission
    if (!nus_tx_active) {
        nus_tx_active = true;
        nus_send_next_chunk(NULL);
    }

    return len;
}

// ============================================================================
// NUS PACKET HANDLER
// ============================================================================

static void nus_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    (void)channel;

    if (packet_type == RFCOMM_DATA_PACKET) {
        // RX data from BLE central — feed into protocol parser
        // Swap active protocol so command responses route back via NUS
        cdc_commands_set_active_protocol(&nus_protocol_ctx);
        for (uint16_t i = 0; i < size; i++) {
            cdc_protocol_rx_byte(&nus_protocol_ctx, packet[i]);
        }
        cdc_commands_set_active_protocol(NULL);  // Restore USB CDC
        return;
    }

    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
        case HCI_EVENT_GATTSERVICE_META:
            switch (hci_event_gattservice_meta_get_subevent_code(packet)) {
                case GATTSERVICE_SUBEVENT_SPP_SERVICE_CONNECTED:
                    nus_con_handle = gattservice_subevent_spp_service_connected_get_con_handle(packet);
                    nus_connected = true;
                    printf("[ble_nus] NUS connected (handle=0x%04x, MTU=%d)\n",
                           nus_con_handle, att_server_get_mtu(nus_con_handle));
                    // Request faster connection interval for streaming (7.5-15ms)
                    gap_request_connection_parameter_update(nus_con_handle, 6, 12, 0, 200);
                    break;

                case GATTSERVICE_SUBEVENT_SPP_SERVICE_DISCONNECTED:
                    printf("[ble_nus] NUS disconnected\n");
                    nus_con_handle = HCI_CON_HANDLE_INVALID;
                    nus_connected = false;
                    nus_tx_active = false;
                    nus_tx_head = 0;
                    nus_tx_tail = 0;
                    nus_tx_offset = 0;
                    cdc_protocol_rx_reset(&nus_protocol_ctx);
                    break;
            }
            break;

        default:
            break;
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================

void ble_nus_init(void)
{
    printf("[ble_nus] Initializing NUS service\n");

    // Initialize protocol context with NUS transport
    cdc_protocol_init(&nus_protocol_ctx, cdc_commands_process);
    nus_protocol_ctx.write = nus_write;
    nus_protocol_ctx.ble_transport = true;

    // Set up context callback for chunked TX flow control
    nus_send_request.callback = &nus_send_next_chunk;

    // Initialize Nordic SPP (NUS) service
    nordic_spp_service_server_init(&nus_packet_handler);

    printf("[ble_nus] NUS ready\n");
}

void ble_nus_task(void)
{
    // Currently no periodic work needed — all NUS processing is event-driven
}
