// sd.h - SD card filesystem service
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Robert Dale Smith
//
// Mount + thin file-IO helpers built on FatFs (src/lib/fatfs/) over a
// platform_sd_t block device (src/platform/platform_sd.h). Apps that
// only need plain file IO can use these helpers; for raw FatFs (e.g.
// mkfs, dir listing) include "ff.h" directly.

#ifndef SD_SERVICE_H
#define SD_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "platform/platform_sd.h"

// Initialize the SD service against an already-initialized block device.
// On success, attempts to mount the FAT filesystem on volume 0.
// Returns true if the filesystem mounted; false if no card / not FAT /
// init failure. The block device pointer must remain valid for the
// lifetime of the service.
bool sd_init(platform_sd_t dev);

// Internal — diskio.c uses this to reach the block device.
platform_sd_t sd_get_block_device(void);

// True after a successful mount — apps can gate file IO on this.
bool sd_mounted(void);

// Total bytes / free bytes on the mounted volume (0 if not mounted).
uint64_t sd_total_bytes(void);
uint64_t sd_free_bytes(void);

// Convenience: read whole file into caller buffer. Returns bytes read,
// or -1 on error.
int sd_read_file(const char* path, void* buf, size_t bufsize);

// Convenience: write/replace a whole file with the given bytes.
// Returns 0 on success.
int sd_write_file(const char* path, const void* buf, size_t len);

#endif // SD_SERVICE_H
