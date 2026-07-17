// ps4_local_auth.h - Local RSA-PSS signing for PS4 authentication
//
// When PS4 key material is stored in flash (via PS4AUTH.SET CDC command),
// this module performs the authentication challenge-response locally without
// needing a real DS4 controller on the USB host port.
//
// Authentication response buffer layout (1064 bytes = 19 × 56-byte pages):
//   [  0 : 256]  RSA-PSS-SHA256 signature of the 280-byte nonce
//   [256 : 272]  Device serial number (16 bytes)
//   [272 : 528]  RSA public modulus N (256 bytes)
//   [528 : 784]  RSA public exponent E, zero-padded to 256 bytes
//   [784 :1040]  Sony device signature sig.bin (256 bytes)
//   [1040:1064]  Zeros (24 bytes padding)
//
// Wire format per page (same as DS4 passthrough, 56 payload bytes per page):
//   byte 0: nonce_id (from console's 0xF0 page 0)
//   byte 1: page index (0..18)
//   byte 2: 0x00 padding
//   bytes 3..58: 56 bytes of response data
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#ifndef PS4_LOCAL_AUTH_H
#define PS4_LOCAL_AUTH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Initialization
// ============================================================================

// Load key material from flash and initialize the RSA context.
// Returns true if key material is present and valid; false if not installed.
// Call once at startup (after flash_init()).
bool ps4_local_auth_init(void);

// Reload after key material has been written to flash (via PS4AUTH.SET).
// Equivalent to calling ps4_local_auth_init() again.
bool ps4_local_auth_reload(void);

// Returns true if local auth is available (key material loaded successfully).
bool ps4_local_auth_is_available(void);

// ============================================================================
// Nonce reception (from PS4 console)
// ============================================================================

// Receive one nonce page from the PS4 console (report 0xF0).
// data: 63-byte payload (nonce_id, page, pad, 56 bytes data).
// Accumulates pages 0..4 into the 280-byte nonce buffer.
// On page 4 (last), sets signing_requested flag.
void ps4_local_auth_send_nonce_page(const uint8_t *data, uint16_t len);

// ============================================================================
// Signing task (call from main loop / ps4_mode task)
// ============================================================================

// Performs RSA-PSS signing when a nonce is ready.
// Blocks for ~50ms on RP2040 @ 125 MHz while signing.
// Returns immediately if no signing is needed.
void ps4_local_auth_task(void);

// ============================================================================
// Status and signature retrieval (for PS4 console)
// ============================================================================

// Returns 0x10 while signing, 0x00 when signature is ready.
uint8_t ps4_local_auth_get_status(void);

// Build a status report (report 0xF2) in buffer.
// Format: [nonce_id][status][zeros × 14] = 16 bytes total.
// Returns number of bytes written.
uint16_t ps4_local_auth_get_status_report(uint8_t *buffer, uint16_t maxlen);

// Build the next signature page (report 0xF1) in buffer.
// Auto-increments internal page counter (0..18).
// Format: [nonce_id][page][0][56 bytes data] ... padded to 64 bytes.
// Returns number of bytes written.
uint16_t ps4_local_auth_get_next_page(uint8_t *buffer, uint16_t maxlen);

// Reset auth state (called when console sends report 0xF3).
void ps4_local_auth_reset(void);

// ============================================================================
// Logging control
// ============================================================================

// Enable or disable PS4 auth event logging to flash.
// When disabled (default), no flash writes occur for auth diagnostics.
void ps4_local_auth_set_log_enabled(bool enabled);
bool ps4_local_auth_get_log_enabled(void);

#ifdef __cplusplus
}
#endif

#endif // PS4_LOCAL_AUTH_H
