// diskio.c - FatFs ↔ platform_sd_t bridge
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Robert Dale Smith
//
// Implements the FatFs disk_io interface (declared in src/lib/fatfs/diskio.h)
// against our platform_sd HAL. FatFs only ever calls into here — it has
// zero knowledge of SPI / SDIO / which MCU we're on.

#include "ff.h"
#include "diskio.h"
#include "sd.h"
#include "platform/platform_sd.h"

// Single-volume build (FF_VOLUMES=1 in ffconf.h) so pdrv is always 0.

DSTATUS disk_initialize(BYTE pdrv) {
    (void)pdrv;
    platform_sd_t dev = sd_get_block_device();
    if (!dev || !platform_sd_present(dev)) return STA_NODISK;
    return 0;
}

DSTATUS disk_status(BYTE pdrv) {
    (void)pdrv;
    platform_sd_t dev = sd_get_block_device();
    if (!dev) return STA_NOINIT;
    if (!platform_sd_present(dev)) return STA_NODISK;
    return 0;
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    (void)pdrv;
    platform_sd_t dev = sd_get_block_device();
    if (!dev) return RES_NOTRDY;
    return (platform_sd_read_blocks(dev, (uint32_t)sector, buff, count) == 0)
        ? RES_OK : RES_ERROR;
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    (void)pdrv;
    platform_sd_t dev = sd_get_block_device();
    if (!dev) return RES_NOTRDY;
    return (platform_sd_write_blocks(dev, (uint32_t)sector, buff, count) == 0)
        ? RES_OK : RES_ERROR;
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    (void)pdrv;
    platform_sd_t dev = sd_get_block_device();
    if (!dev) return RES_NOTRDY;

    switch (cmd) {
        case CTRL_SYNC:
            return (platform_sd_sync(dev) == 0) ? RES_OK : RES_ERROR;
        case GET_SECTOR_COUNT:
            *(LBA_t*)buff = platform_sd_block_count(dev);
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD*)buff = PLATFORM_SD_BLOCK_SIZE;
            return RES_OK;
        case GET_BLOCK_SIZE:
            // Erase block size in sectors; SD doesn't expose this so
            // FatFs just gets a conservative default.
            *(DWORD*)buff = 1;
            return RES_OK;
        default:
            return RES_PARERR;
    }
}
