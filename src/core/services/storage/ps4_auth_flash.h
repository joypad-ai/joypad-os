// ps4_auth_flash.h - Flash storage for PS4 local authentication key material
//
// Stores RSA-2048 key components and Sony device identity data in a dedicated
// 4KB flash sector, separate from the main settings journal.
//
// The data persists across firmware updates and factory resets (different sector).
// Use PS4AUTH.CLEAR CDC command or ps4_auth_flash_erase() to remove.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#ifndef PS4_AUTH_FLASH_H
#define PS4_AUTH_FLASH_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Auth Data Structure
// ============================================================================

// Magic number for PS4 auth flash sector: "PS4A"
#define PS4_AUTH_FLASH_MAGIC  0x50533441

// PS4 auth data stored in flash.
// Padded to 1024 bytes (4 × 256-byte flash pages) for aligned flash writes.
typedef struct __attribute__((packed)) {
    uint32_t magic;         // 0x50533441 ("PS4A") — validates structure presence
    uint32_t crc32;         // CRC-32 of bytes [8 .. sizeof-1] for integrity check
    uint8_t  rsa_n[256];   // RSA-2048 public modulus N
    uint8_t  rsa_e[4];     // RSA public exponent E (big-endian, typically 0x00010001)
    uint8_t  rsa_p[128];   // RSA prime factor P
    uint8_t  rsa_q[128];   // RSA prime factor Q
    uint8_t  serial[16];   // Device serial number (16 bytes from serial.txt)
    uint8_t  sig[256];     // Sony-issued device signature (sig.bin, 256 bytes)
    uint8_t  reserved[228]; // Pad to 1024 bytes total
} ps4_auth_data_t;

_Static_assert(sizeof(ps4_auth_data_t) == 1024, "ps4_auth_data_t must be 1024 bytes");

// ============================================================================
// Flash API
// ============================================================================

// Load auth data from flash into *out.
// Returns true if valid data found (magic and CRC match), false otherwise.
bool ps4_auth_flash_load(ps4_auth_data_t *out);

// Write auth data to the dedicated flash sector.
// Erases the sector first, then programs 4 pages (1024 bytes).
// Blocks for ~5ms total (erase + program). Safe to call from main loop.
void ps4_auth_flash_save(const ps4_auth_data_t *data);

// Erase the PS4 auth flash sector, removing all stored key material.
void ps4_auth_flash_erase(void);

// Validate magic and CRC of an already-loaded auth data structure.
bool ps4_auth_flash_is_valid(const ps4_auth_data_t *data);

#ifdef __cplusplus
}
#endif

#endif // PS4_AUTH_FLASH_H
