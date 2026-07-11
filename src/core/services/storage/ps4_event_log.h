// ps4_event_log.h - Flash-persistent event log for PS4 auth debugging
//
// Uses 3072 bytes (bytes 1024–4095) of the PS4 auth flash sector to store
// up to 48 compact event records that survive USB power cycles.
//
// Write cost: ~1 ms per record (one 256-byte flash page program).
// Clearing: erases auth sector and rewrites auth data (~5 ms).
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#ifndef PS4_EVENT_LOG_H
#define PS4_EVENT_LOG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Maximum message length per record (including null terminator)
#define PS4_EVENT_LOG_TEXT_SIZE  56
// Maximum number of records
#define PS4_EVENT_LOG_MAX        48

// Initialize — scans flash to find next available slot.
// Must be called after flash_init() and before any write.
void ps4_event_log_init(void);

// Write a log message (up to 55 printable chars; truncated if longer).
// Silently drops the entry if the log is full.
void ps4_event_log_write(const char *msg);

// Dump all valid log entries into out as "[Nms] message\n" lines.
// Returns number of bytes written (excluding the final null).
int ps4_event_log_dump(char *out, size_t maxlen);

// Erase all log entries, preserving auth data.
// Takes ~5 ms (sector erase + auth rewrite).
void ps4_event_log_clear(void);

// Number of valid entries currently stored.
uint8_t ps4_event_log_count(void);

#ifdef __cplusplus
}
#endif

#endif // PS4_EVENT_LOG_H
