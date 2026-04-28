// platform_sd.h - SD card block-IO HAL
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Robert Dale Smith
//
// Minimal block-device interface for SD cards. Drives the FatFs disk_io
// layer (src/core/services/sd/diskio.c) and any code that wants raw
// block access. Each platform implements this against its preferred
// transport — RP2040 ships SPI; ESP32-S3 / nRF can later add SDIO.

#ifndef PLATFORM_SD_H
#define PLATFORM_SD_H

#include <stdint.h>
#include <stdbool.h>

#define PLATFORM_SD_BLOCK_SIZE  512u

// Pin used as "no card-detect wired" sentinel.
#define PLATFORM_SD_NO_CD       0xFF

typedef struct platform_sd_config {
    uint8_t spi_inst;       // 0 or 1 (RP2040)
    uint8_t sck_pin;
    uint8_t mosi_pin;
    uint8_t miso_pin;
    uint8_t cs_pin;
    uint8_t cd_pin;         // PLATFORM_SD_NO_CD if unused
    uint32_t init_freq_hz;  // initial clock for init phase (~100-400kHz)
    uint32_t run_freq_hz;   // post-init clock (up to 25MHz for SDSC, 50MHz HC)
} platform_sd_config_t;

typedef struct platform_sd* platform_sd_t;

// Initialize the SD interface (claims pins, sets up SPI). Returns NULL
// on failure. Does NOT mount a filesystem — see core/services/sd.
platform_sd_t platform_sd_init(const platform_sd_config_t* config);

// True if a card is currently inserted. Uses card-detect pin if wired,
// otherwise probes the SPI bus.
bool platform_sd_present(platform_sd_t dev);

// Total number of 512-byte blocks the card reports. 0 if uninitialised
// or no card.
uint32_t platform_sd_block_count(platform_sd_t dev);

// Read `count` blocks starting at logical block `lba` into `buf`.
// `buf` must be at least count * PLATFORM_SD_BLOCK_SIZE bytes.
// Returns 0 on success, non-zero on I/O error.
int platform_sd_read_blocks(platform_sd_t dev, uint32_t lba,
                            uint8_t* buf, uint32_t count);

// Write `count` blocks starting at `lba`. Returns 0 on success.
int platform_sd_write_blocks(platform_sd_t dev, uint32_t lba,
                             const uint8_t* buf, uint32_t count);

// Flush any in-flight writes to the card (CMD12 / wait-busy).
int platform_sd_sync(platform_sd_t dev);

#endif // PLATFORM_SD_H
