// p5general_link.c - Dual-chip bridge for GP2040 P5General PS5 auth
// SPDX-License-Identifier: MIT
//
// See p5general_link.h. Mirrors the p5general_auth_data_t handoff across the
// uart_peer link so the device half (A) and the dongle-relay half (B) of the
// GP2040 P5General auth can run on separate MCUs. Each side keeps operating on
// its LOCAL p5general_auth_data exactly as in the single-chip case; this module
// only carries the field updates between the two copies, using edge/change
// detection so the pristine mode/host files need no changes.

#include "p5general_link.h"
#include "uart_peer.h"
#include "platform/platform.h"                                 // platform_time_ms
#include "usb/usbh/hid/devices/vendors/sony/p5general_host.h"  // p5general_auth_data + msg IDs
#include <string.h>

#define P5G_BUF 64  // sizeof(hash_*/auth_buffer)

// ============================================================================
// RX — apply an incoming frame into the LOCAL p5general_auth_data.
//   On B this feeds the host relay (p5general_host_task); on A it feeds the
//   device mode (p5general_mode.c). A given side only ever receives the other's
//   message types, so both halves can share one switch.
// ============================================================================
void p5general_link_on_frame(uint8_t type, const uint8_t* payload, uint16_t plen) {
    p5general_auth_data_t* a = &p5general_auth_data;

    switch (type) {
        // ---- received on the HOST side (B): A -> B ----
        case UART_PEER_MSG_P5G_SIGN_REQ:      // A built a report; hand it to the dongle
            if (plen == P5G_BUF) {
                memcpy(a->hash_pending_buffer, payload, P5G_BUF);
                a->hash_pending = true;
            }
            break;
        case UART_PEER_MSG_P5G_F0:            // PS5 F0 challenge; start the auth SM
            if (plen == P5G_BUF) {
                memcpy(a->auth_buffer, payload, P5G_BUF);
                a->passthrough_state = P5G_AUTH_SEND_F0;
            }
            break;
        case UART_PEER_MSG_P5G_POLL_F1:       // PS5 is polling F1/F2; fetch next from dongle
            if (a->passthrough_state == P5G_AUTH_IDLE) {
                a->passthrough_state = P5G_AUTH_RECV_F1;
            }
            break;

        // ---- received on the DEVICE side (A): B -> A ----
        case UART_PEER_MSG_P5G_SIGN_RESP:     // dongle-signed report; queue it for the PS5
            if (plen == P5G_BUF) {
                memcpy(a->hash_finish_buffer, payload, P5G_BUF);
                a->hash_ready = true;
                a->hash_pending = false;      // request satisfied; allow the next one
            }
            break;
        case UART_PEER_MSG_P5G_AUTH_DATA:     // dongle F1/F2 response; serve it on the next GET
            if (plen == P5G_BUF) {
                memcpy(a->auth_buffer, payload, P5G_BUF);
            }
            break;
        case UART_PEER_MSG_P5G_DONGLE:        // dongle presence gates the device's "ready"
            if (plen >= 1) {
                a->dongle_ready = (payload[0] != 0);
            }
            break;

        default:
            break;
    }
}

// ============================================================================
// DEVICE side (A) task — forward local auth writes to B.
//   The device mode writes hash_pending / auth_buffer+SEND_F0 / RECV_F1 into the
//   local struct; we forward each and clear the trigger so B owns the auth SM.
//   Signing is kept to one request in flight (hash_pending stays set until the
//   signed report returns, which also blocks the mode from building a new one).
// ============================================================================
void p5general_link_device_task(void) {
    p5general_auth_data_t* a = &p5general_auth_data;
    static bool sign_in_flight = false;

    // Report to sign: forward once; keep hash_pending set (mode uses it to hold
    // off building the next report) until SIGN_RESP clears it.
    if (a->hash_pending && !sign_in_flight) {
        uart_peer_send_frame(UART_PEER_MSG_P5G_SIGN_REQ, a->hash_pending_buffer, P5G_BUF);
        sign_in_flight = true;
    }
    if (!a->hash_pending) {
        sign_in_flight = false;  // SIGN_RESP arrived (or reset); ready for the next
    }

    // PS5 F0 challenge captured by the mode's SET_REPORT handler.
    if (a->passthrough_state == P5G_AUTH_SEND_F0) {
        uart_peer_send_frame(UART_PEER_MSG_P5G_F0, a->auth_buffer, P5G_BUF);
        a->passthrough_state = P5G_AUTH_IDLE;  // one-shot; B drives the state machine
    }

    // PS5 polling F1/F2: nudge B to fetch the next chunk from the dongle.
    if (a->passthrough_state == P5G_AUTH_RECV_F1) {
        uart_peer_send_frame(UART_PEER_MSG_P5G_POLL_F1, NULL, 0);
        a->passthrough_state = P5G_AUTH_IDLE;
    }
}

// ============================================================================
// HOST side (B) task — forward the dongle relay's results back to A.
//   B's p5general_host_task drives the real dongle transactions on the local
//   struct; we detect the resulting field changes and ship them to A.
// ============================================================================
void p5general_link_host_task(void) {
    p5general_auth_data_t* a = &p5general_auth_data;
    static bool dongle_last = false;
    static p5general_auth_state_t state_last = P5G_AUTH_IDLE;

    // Dongle presence — gates the device's "ready" on A. Send on change AND
    // re-broadcast every ~250ms, so A still learns it if the initial edge was
    // missed (dongle already mounted before the link came up) or after a link
    // reconnect. One byte at 4 Hz is negligible.
    static uint32_t dongle_last_ms = 0;
    uint32_t now = platform_time_ms();
    if (a->dongle_ready != dongle_last || (now - dongle_last_ms) >= 250) {
        dongle_last = a->dongle_ready;
        dongle_last_ms = now;
        uint8_t v = a->dongle_ready ? 1 : 0;
        uart_peer_send_frame(UART_PEER_MSG_P5G_DONGLE, &v, 1);
    }

    // A dongle-signed report landed (report_received set hash_ready) — send it
    // to A and consume the flag (its device mode owned that consume single-chip).
    if (a->hash_ready) {
        uart_peer_send_frame(UART_PEER_MSG_P5G_SIGN_RESP, a->hash_finish_buffer, P5G_BUF);
        a->hash_ready = false;
    }

    // An F1/F2 GET just completed: the relay returns to IDLE from a *_WAIT state
    // with auth_buffer freshly filled. Forward that payload for the PS5's GET.
    if ((state_last == P5G_AUTH_RECV_F1_WAIT || state_last == P5G_AUTH_RECV_F2_WAIT) &&
        a->passthrough_state == P5G_AUTH_IDLE) {
        uart_peer_send_frame(UART_PEER_MSG_P5G_AUTH_DATA, a->auth_buffer, P5G_BUF);
    }
    state_last = a->passthrough_state;
}
