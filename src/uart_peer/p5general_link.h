// p5general_link.h - Dual-chip bridge for GP2040 P5General PS5 auth
// SPDX-License-Identifier: MIT
//
// GP2040's P5General auth is single-chip: the device mode (p5general_mode.c,
// presents to the PS5) and the host relay (p5general_host.c, drives the auth
// dongle) share one p5general_auth_data_t in RAM. On the dual-RP2040 remapper
// those halves live on different MCUs, so this module mirrors the relevant
// fields of that struct across the uart_peer link:
//
//   DEVICE side (A, has p5general_mode.c):  call p5general_link_device_task()
//     - forwards A's report-to-sign / F0 challenge / F1 poll to B
//     - applies B's signed report / F1-F2 auth data / dongle-ready into A's copy
//   HOST side (B, has p5general_host.c):     call p5general_link_host_task()
//     - applies A's report-to-sign / F0 / F1 poll into B's copy (B's relay runs
//       the actual dongle transactions on it)
//     - forwards B's signed report / F1-F2 auth data / dongle-ready to A
//
// Incoming frames are applied by p5general_link_on_frame(), called from the
// uart_peer RX dispatch. Both tasks are no-ops until the peer sends P5General
// traffic, so linking this on a single-chip build (shared RAM) is inert.

#ifndef P5GENERAL_LINK_H
#define P5GENERAL_LINK_H

#include <stdint.h>

// Device side (A): forward local auth writes to B, one signing request in flight.
void p5general_link_device_task(void);

// Host side (B): forward the dongle's results back to A as they land.
void p5general_link_host_task(void);

// RX apply — strong override of the weak hook in uart_peer.c. Called from the
// link's frame dispatcher for the P5General message types.
void p5general_link_on_frame(uint8_t type, const uint8_t* payload, uint16_t plen);

#endif // P5GENERAL_LINK_H
