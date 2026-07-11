// ps4_event_log.c - Flash-persistent event log for PS4 auth debugging
//
// Flash layout within the PS4 auth sector (4 KB):
//   [0    .. 1023] ps4_auth_data_t  — RSA key + serial + sig (written by ps4_auth_flash.c)
//   [1024 .. 4095] event log        — 48 × 64-byte records (this module)
//
// Record format (64 bytes):
//   [0]     valid   0xA5 = valid, 0xFF = empty
//   [1]     seq     monotonic counter (wraps 255 → 0)
//   [2]     len     message length in bytes (1–55)
//   [3]     rsvd    unused (written as 0xFF)
//   [4–7]   ts_ms   platform_time_ms() at write time (little-endian uint32)
//   [8–63]  text    null-terminated message (up to 55 chars + null)
//
// Flash programming granularity is 256 bytes. Four 64-byte records share
// one flash page. Writing a record reads the page from XIP, fills one slot,
// and programs the page back — safe because we only flip 1→0 bits in the
// previously-erased slot (0xFF → record data).
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "ps4_event_log.h"
#include "ps4_auth_flash.h"
#include "platform/platform.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// FLASH OFFSETS  (mirrors ps4_auth_flash.c)
// ============================================================================

#define BTSTACK_FLASH_SIZE  (FLASH_SECTOR_SIZE * 2)

#if PICO_RP2350 && PICO_RP2350_A2_SUPPORTED
#define _SECTOR_A_OFFSET \
    (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE - BTSTACK_FLASH_SIZE - FLASH_SECTOR_SIZE)
#else
#define _SECTOR_A_OFFSET \
    (PICO_FLASH_SIZE_BYTES - BTSTACK_FLASH_SIZE - FLASH_SECTOR_SIZE)
#endif
#define _SECTOR_B_OFFSET     (_SECTOR_A_OFFSET - FLASH_SECTOR_SIZE)
#define FLASH_PS4_AUTH_OFFSET (_SECTOR_B_OFFSET - FLASH_SECTOR_SIZE)

// Event log starts immediately after the 1024-byte auth struct
#define LOG_FLASH_BASE   (FLASH_PS4_AUTH_OFFSET + 1024u)

// ============================================================================
// LAYOUT CONSTANTS
// ============================================================================

#define LOG_RECORD_SIZE       64u    // bytes per record
#define LOG_VALID_MARKER      0xA5u  // byte 0 of a written record
#define LOG_TEXT_OFFSET       8u     // text starts at this byte within record
#define LOG_PAGE_SIZE         256u   // RP2040 flash programming granularity
#define LOG_RECORDS_PER_PAGE  4u     // LOG_PAGE_SIZE / LOG_RECORD_SIZE
#define LOG_NUM_PAGES         12u    // 3072 / 256
#define LOG_MAX_RECORDS       48u    // LOG_NUM_PAGES * LOG_RECORDS_PER_PAGE

// ============================================================================
// STATE
// ============================================================================

static uint8_t s_next_slot = 0;   // next slot index to write (0–47)
static uint8_t s_seq       = 0;   // monotonic sequence number

// ============================================================================
// FLASH HELPERS  (__no_inline_not_in_flash_func so they run from SRAM)
// ============================================================================

typedef struct { uint32_t offset; size_t length; }                   erase_p_t;
typedef struct { uint32_t offset; const uint8_t *data; size_t len; } prog_p_t;

static void __no_inline_not_in_flash_func(do_erase)(void *p)
{
    erase_p_t *ep = (erase_p_t *)p;
    flash_range_erase(ep->offset, ep->length);
}

static void __no_inline_not_in_flash_func(do_program)(void *p)
{
    prog_p_t *pp = (prog_p_t *)p;
    flash_range_program(pp->offset, pp->data, pp->len);
}

static void safe_erase(uint32_t offset, size_t length)
{
    erase_p_t ep = { offset, length };
    // No fallback: if flash_safe_execute fails (Core 1 not initialized as lockout
    // victim), skip the operation rather than erasing without pausing Core 1.
    flash_safe_execute(do_erase, &ep, UINT32_MAX);
}

static void safe_program(uint32_t offset, const uint8_t *data, size_t len)
{
    prog_p_t pp = { offset, data, len };
    // No fallback: if flash_safe_execute fails (Core 1 not initialized as lockout
    // victim), skip the operation rather than programming without pausing Core 1.
    flash_safe_execute(do_program, &pp, UINT32_MAX);
}

// ============================================================================
// XIP READ HELPERS
// ============================================================================

static inline const uint8_t *record_xip(uint8_t slot)
{
    return (const uint8_t *)(XIP_BASE + LOG_FLASH_BASE + (uint32_t)slot * LOG_RECORD_SIZE);
}

// ============================================================================
// PUBLIC API
// ============================================================================

void ps4_event_log_init(void)
{
    s_next_slot = LOG_MAX_RECORDS;  // default: assume full
    s_seq       = 0;

    for (uint8_t i = 0; i < LOG_MAX_RECORDS; i++) {
        const uint8_t *r = record_xip(i);
        if (r[0] == 0xFF) {
            // Found first empty slot
            s_next_slot = i;
            if (i > 0) {
                // Resume sequence from last written record
                s_seq = record_xip(i - 1)[1] + 1u;
            }
            return;
        }
    }
    // All 48 slots are occupied — s_next_slot stays at LOG_MAX_RECORDS
    // Track highest seq from last record for continuity if we ever clear
    s_seq = record_xip(LOG_MAX_RECORDS - 1)[1] + 1u;
}

void ps4_event_log_write(const char *msg)
{
    if (!msg || s_next_slot >= LOG_MAX_RECORDS) return;

    uint8_t slot         = s_next_slot;
    uint32_t page_idx    = slot / LOG_RECORDS_PER_PAGE;
    uint32_t slot_in_pg  = slot % LOG_RECORDS_PER_PAGE;
    uint32_t page_off    = LOG_FLASH_BASE + page_idx * LOG_PAGE_SIZE;

    // Read the current 256-byte page from XIP into SRAM
    static uint8_t page_buf[LOG_PAGE_SIZE];
    memcpy(page_buf, (const uint8_t *)(XIP_BASE + page_off), LOG_PAGE_SIZE);

    // Fill the 64-byte slot within the page
    uint8_t *rec = page_buf + slot_in_pg * LOG_RECORD_SIZE;

    size_t msg_len = strlen(msg);
    if (msg_len >= PS4_EVENT_LOG_TEXT_SIZE) msg_len = PS4_EVENT_LOG_TEXT_SIZE - 1u;

    uint32_t ts = platform_time_ms();

    rec[0] = LOG_VALID_MARKER;
    rec[1] = s_seq++;
    rec[2] = (uint8_t)msg_len;
    rec[3] = 0xFF;  // reserved, keep erased value
    rec[4] = (uint8_t)( ts        & 0xFF);
    rec[5] = (uint8_t)((ts >>  8) & 0xFF);
    rec[6] = (uint8_t)((ts >> 16) & 0xFF);
    rec[7] = (uint8_t)((ts >> 24) & 0xFF);
    memcpy(rec + LOG_TEXT_OFFSET, msg, msg_len);
    rec[LOG_TEXT_OFFSET + msg_len] = 0x00;  // null terminator

    safe_program(page_off, page_buf, LOG_PAGE_SIZE);
    s_next_slot++;
}

int ps4_event_log_dump(char *out, size_t maxlen)
{
    if (!out || maxlen < 2) return 0;

    int written = 0;
    for (uint8_t i = 0; i < LOG_MAX_RECORDS; i++) {
        const uint8_t *r = record_xip(i);
        if (r[0] != LOG_VALID_MARKER) continue;

        uint8_t len = r[2];
        if (len == 0 || len >= PS4_EVENT_LOG_TEXT_SIZE) continue;

        uint32_t ts = (uint32_t)r[4]
                    | ((uint32_t)r[5] <<  8)
                    | ((uint32_t)r[6] << 16)
                    | ((uint32_t)r[7] << 24);

        const char *text = (const char *)(r + LOG_TEXT_OFFSET);
        int n = snprintf(out + written, maxlen - (size_t)written,
                         "[%lums] %.*s\n",
                         (unsigned long)ts, (int)len, text);
        if (n <= 0 || (size_t)(written + n) >= maxlen) break;
        written += n;
    }

    out[written] = '\0';
    return written;
}

void ps4_event_log_clear(void)
{
    // Back up auth data before erasing the sector.
    // Use ps4_auth_flash_save() to restore it — it uses a static internal buffer
    // (safe for flash ops) and handles erase+program atomically with full CRC.
    // The sector erase inside ps4_auth_flash_save() also clears the log area.
    ps4_auth_data_t auth_backup;
    bool auth_valid = ps4_auth_flash_load(&auth_backup);

    if (auth_valid) {
        // Restore auth — ps4_auth_flash_save erases the sector then reprograms
        // only the first 1024 bytes, leaving the log area (1024-4095) as 0xFF.
        ps4_auth_flash_save(&auth_backup);
    } else {
        // No auth to preserve — just erase the sector to clear the log.
        safe_erase(FLASH_PS4_AUTH_OFFSET, FLASH_SECTOR_SIZE);
    }

    // Zero out sensitive key material from stack copy
    memset(&auth_backup, 0, sizeof(auth_backup));

    s_next_slot = 0;
    s_seq       = 0;
}

uint8_t ps4_event_log_count(void)
{
    return (s_next_slot < LOG_MAX_RECORDS) ? s_next_slot : LOG_MAX_RECORDS;
}
