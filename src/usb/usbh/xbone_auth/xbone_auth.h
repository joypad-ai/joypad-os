// xbone_auth.h - Xbox One Auth Dongle Host Driver
// SPDX-License-Identifier: MIT
// Based on GP2040-CE implementation (gp2040-ce.info)
//
// This driver detects Xbox One authentication dongles (like Magic-X)
// connected to the USB host port and handles auth passthrough between
// the dongle and an Xbox One console.

#ifndef XBONE_AUTH_H
#define XBONE_AUTH_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// PUBLIC API
// ============================================================================

// Initialize Xbox One auth passthrough
void xbone_auth_init(void);

// Check if an Xbox One auth dongle is available
bool xbone_auth_is_available(void);

// Check if auth is complete
bool xbone_auth_is_complete(void);

// Task to process auth passthrough (call from main loop)
void xbone_auth_task(void);

// Register a dongle on mount
void xbone_auth_register(uint8_t dev_addr, uint8_t instance);

// Unregister dongle on unmount
void xbone_auth_unregister(uint8_t dev_addr);

// Called when report received from dongle
void xbone_auth_report_received(uint8_t dev_addr, uint8_t instance,
                                uint8_t const* report, uint16_t len);

// Called for each input report from an Xbox One source: the first one marks
// the source powered on and ready to carry auth
void xbone_auth_source_input(uint8_t dev_addr, uint8_t instance);

// Called on xinput mount to check for Xbox One dongle
void xbone_auth_xmount(uint8_t dev_addr, uint8_t instance,
                       uint8_t controller_type, uint8_t subtype);

#endif // XBONE_AUTH_H
