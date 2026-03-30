// ps4_auth_flash.c - Flash storage for PS4 local authentication key material
//
// Flash layout (from end of flash):
//   [... firmware ...]
//   [PS4 auth sector — 4KB]   <-- this module
//   [Settings Sector B — 4KB]
//   [Settings Sector A — 4KB]
//   [BTstack — 8KB]
//   [end of flash]
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "ps4_auth_flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// FLASH OFFSET
// Must stay in sync with flash.c layout. The PS4 auth sector is placed
// immediately before Settings Sector B (one extra sector from the base).
// ============================================================================

#define BTSTACK_FLASH_SIZE  (FLASH_SECTOR_SIZE * 2)   // 8KB for BTstack

#if PICO_RP2350 && PICO_RP2350_A2_SUPPORTED
// RP2350 layout: [... | PS4 auth | Sector B | Sector A | BTstack (2) | reserved (1)]
#define _SECTOR_A_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE - BTSTACK_FLASH_SIZE - FLASH_SECTOR_SIZE)
#else
// RP2040 layout: [... | PS4 auth | Sector B | Sector A | BTstack (2)]
#define _SECTOR_A_OFFSET (PICO_FLASH_SIZE_BYTES - BTSTACK_FLASH_SIZE - FLASH_SECTOR_SIZE)
#endif

#define _SECTOR_B_OFFSET  (_SECTOR_A_OFFSET - FLASH_SECTOR_SIZE)
#define FLASH_PS4_AUTH_OFFSET (_SECTOR_B_OFFSET - FLASH_SECTOR_SIZE)

// ============================================================================
// CRC-32
// Standard polynomial (0xEDB88320 reflected)
// ============================================================================

static uint32_t crc32_compute(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ 0xEDB88320;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc ^ 0xFFFFFFFF;
}

// CRC covers everything after the header (magic + crc32 fields)
#define CRC_OFFSET   (sizeof(uint32_t) * 2)      // skip magic + crc32
#define CRC_LENGTH   (sizeof(ps4_auth_data_t) - CRC_OFFSET)

// ============================================================================
// FLASH WORKERS (must not be in flash — called during flash operations)
// ============================================================================

typedef struct {
    uint32_t offset;
    size_t   length;
} flash_erase_params_t;

typedef struct {
    uint32_t       offset;
    const uint8_t *data;
    size_t         length;
} flash_program_params_t;

static void __no_inline_not_in_flash_func(do_flash_erase)(void *param)
{
    flash_erase_params_t *p = (flash_erase_params_t *)param;
    flash_range_erase(p->offset, p->length);
}

static void __no_inline_not_in_flash_func(do_flash_program)(void *param)
{
    flash_program_params_t *p = (flash_program_params_t *)param;
    flash_range_program(p->offset, p->data, p->length);
}

// ============================================================================
// PUBLIC API
// ============================================================================

bool ps4_auth_flash_is_valid(const ps4_auth_data_t *data)
{
    if (!data) return false;
    if (data->magic != PS4_AUTH_FLASH_MAGIC) return false;

    uint32_t expected = crc32_compute(
        (const uint8_t *)data + CRC_OFFSET,
        CRC_LENGTH
    );
    return data->crc32 == expected;
}

bool ps4_auth_flash_load(ps4_auth_data_t *out)
{
    if (!out) return false;

    const ps4_auth_data_t *flash_ptr =
        (const ps4_auth_data_t *)(XIP_BASE + FLASH_PS4_AUTH_OFFSET);

    // Quick magic check before copying
    if (flash_ptr->magic != PS4_AUTH_FLASH_MAGIC) {
        printf("[ps4_auth] No auth data in flash (magic mismatch)\n");
        return false;
    }

    memcpy(out, flash_ptr, sizeof(ps4_auth_data_t));

    if (!ps4_auth_flash_is_valid(out)) {
        printf("[ps4_auth] Auth data CRC mismatch — data corrupt or incomplete\n");
        memset(out, 0, sizeof(ps4_auth_data_t));
        return false;
    }

    printf("[ps4_auth] Auth data loaded from flash (CRC OK)\n");
    return true;
}

void ps4_auth_flash_save(const ps4_auth_data_t *data)
{
    // Build the write buffer (static so it survives flash ops)
    static ps4_auth_data_t write_buf;
    memcpy(&write_buf, data, sizeof(ps4_auth_data_t));
    write_buf.magic = PS4_AUTH_FLASH_MAGIC;
    write_buf.crc32 = crc32_compute(
        (const uint8_t *)&write_buf + CRC_OFFSET,
        CRC_LENGTH
    );

    printf("[ps4_auth] Saving auth data to flash at offset 0x%lX...\n",
           (unsigned long)FLASH_PS4_AUTH_OFFSET);

    // Erase sector
    flash_erase_params_t ep = {
        .offset = FLASH_PS4_AUTH_OFFSET,
        .length = FLASH_SECTOR_SIZE
    };
    flash_safe_execute(do_flash_erase, &ep, UINT32_MAX);

    // Program 1024 bytes (4 × 256-byte pages)
    flash_program_params_t pp = {
        .offset = FLASH_PS4_AUTH_OFFSET,
        .data   = (const uint8_t *)&write_buf,
        .length = sizeof(ps4_auth_data_t)  // 1024 bytes, 4 pages
    };
    flash_safe_execute(do_flash_program, &pp, UINT32_MAX);

    printf("[ps4_auth] Auth data saved successfully\n");
}

void ps4_auth_flash_erase(void)
{
    printf("[ps4_auth] Erasing auth data from flash...\n");
    flash_erase_params_t ep = {
        .offset = FLASH_PS4_AUTH_OFFSET,
        .length = FLASH_SECTOR_SIZE
    };
    flash_safe_execute(do_flash_erase, &ep, UINT32_MAX);
    printf("[ps4_auth] Auth data erased\n");
}
