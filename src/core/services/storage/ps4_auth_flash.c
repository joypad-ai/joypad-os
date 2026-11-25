// ps4_auth_flash.c - Flash storage for PS4 local authentication key material
//
// Flash layout (from end of flash):
//   see flash_layout.h: the PS4 auth sector is its own sector, below the
//   Remote Play and pad config sectors. It used to share the pad config
//   sector, so saving a pad config erased the keys and the reverse.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "ps4_auth_flash.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "core/services/storage/flash_layout.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// FLASH OFFSET (flash_layout.h owns every sector)
// ============================================================================

#define FLASH_PS4_AUTH_OFFSET     FLASH_LAYOUT_PS4_AUTH_OFFSET
#define FLASH_PS4_AUTH_OLD_OFFSET FLASH_LAYOUT_PS4_AUTH_OLD_OFFSET

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

    // One-time carry-forward: keys saved by older firmware live in the sector
    // now owned by pad config. If the new sector is empty and a complete,
    // CRC-valid record is still at the old offset, copy it across once. The
    // old sector is left alone (it belongs to pad config now).
    if (flash_ptr->magic != PS4_AUTH_FLASH_MAGIC) {
        const ps4_auth_data_t *old_ptr =
            (const ps4_auth_data_t *)(XIP_BASE + FLASH_PS4_AUTH_OLD_OFFSET);
        if (ps4_auth_flash_is_valid(old_ptr)) {
            static ps4_auth_data_t carry;
            memcpy(&carry, old_ptr, sizeof(carry));
            printf("[ps4_auth] Moving auth data from the old shared sector\n");
            if (!ps4_auth_flash_save(&carry)) {
                // Could not write the new sector: still use the keys this boot.
                memcpy(out, &carry, sizeof(ps4_auth_data_t));
                return true;
            }
        }
    }

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

// Run a flash worker and report whether it was actually carried out.
//
// Deliberately NOT falling back to a direct erase/program with interrupts
// disabled the way flash.c does for the adjacent settings sectors
// (flash.c:302, :326). Disabling interrupts on Core 0 does not park Core 1, so
// if flash_safe_execute() failed because the multicore lockout is unavailable,
// a direct erase can fault Core 1 while it executes from XIP. flash.c accepts
// that trade for settings; taking it here would be a behaviour change that
// cannot be validated without hardware, inside a fix whose whole point is to
// stop reporting unverified success. Surface the failure instead.
static bool run_flash_op(void (*worker)(void *), void *param,
                         const char *what)
{
    int result = flash_safe_execute(worker, param, UINT32_MAX);
    if (result == PICO_OK) return true;

    printf("[ps4_auth] flash_safe_execute failed for %s (%d)\n", what, result);
    return false;
}

bool ps4_auth_flash_save(const ps4_auth_data_t *data)
{
    if (!data) return false;

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
    if (!run_flash_op(do_flash_erase, &ep, "erase")) {
        printf("[ps4_auth] Auth data NOT saved — erase did not run\n");
        return false;
    }

    // Program 1024 bytes (4 × 256-byte pages)
    flash_program_params_t pp = {
        .offset = FLASH_PS4_AUTH_OFFSET,
        .data   = (const uint8_t *)&write_buf,
        .length = sizeof(ps4_auth_data_t)  // 1024 bytes, 4 pages
    };
    if (!run_flash_op(do_flash_program, &pp, "program")) {
        printf("[ps4_auth] Auth data NOT saved — program did not run\n");
        return false;
    }

    // Read back from flash and validate. A PICO_OK from flash_safe_execute()
    // only says the worker ran, not that a usable record landed, so the final
    // verdict is taken from the sector itself.
    const ps4_auth_data_t *flash_ptr =
        (const ps4_auth_data_t *)(XIP_BASE + FLASH_PS4_AUTH_OFFSET);
    if (!ps4_auth_flash_is_valid(flash_ptr)) {
        printf("[ps4_auth] Auth data NOT saved — read-back failed validation\n");
        return false;
    }

    printf("[ps4_auth] Auth data saved successfully (read-back verified)\n");
    return true;
}

bool ps4_auth_flash_erase(void)
{
    printf("[ps4_auth] Erasing auth data from flash...\n");
    flash_erase_params_t ep = {
        .offset = FLASH_PS4_AUTH_OFFSET,
        .length = FLASH_SECTOR_SIZE
    };
    if (!run_flash_op(do_flash_erase, &ep, "erase")) {
        printf("[ps4_auth] Auth data NOT erased — erase did not run\n");
        return false;
    }

    const ps4_auth_data_t *flash_ptr =
        (const ps4_auth_data_t *)(XIP_BASE + FLASH_PS4_AUTH_OFFSET);
    if (flash_ptr->magic == PS4_AUTH_FLASH_MAGIC) {
        printf("[ps4_auth] Auth data NOT erased — magic still present\n");
        return false;
    }

    printf("[ps4_auth] Auth data erased\n");
    return true;
}
