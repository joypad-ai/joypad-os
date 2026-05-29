// msc_diskio.c - FatFs <-> USB MSC host bridge
//
// Implements the FatFs disk_io interface against msc_host (TinyUSB mass
// storage). Used by the VMU's USB-flash persistence backend. Single-volume
// build (FF_VOLUMES=1), so pdrv is always 0. Only linked into targets that
// define CONFIG_USB_MSC, and is mutually exclusive with the SD diskio.c
// (a build links one block backend, never both).

#include "ff.h"
#include "diskio.h"
#include "msc_host.h"

DSTATUS disk_initialize(BYTE pdrv) {
    (void)pdrv;
    return msc_host_mounted() ? 0 : STA_NODISK;
}

DSTATUS disk_status(BYTE pdrv) {
    (void)pdrv;
    return msc_host_mounted() ? 0 : STA_NODISK;
}

DRESULT disk_read(BYTE pdrv, BYTE* buff, LBA_t sector, UINT count) {
    (void)pdrv;
    if (!msc_host_mounted()) return RES_NOTRDY;
    return msc_host_read_blocks((uint32_t)sector, buff, count) ? RES_OK : RES_ERROR;
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count) {
    (void)pdrv;
    if (!msc_host_mounted()) return RES_NOTRDY;
    return msc_host_write_blocks((uint32_t)sector, buff, count) ? RES_OK : RES_ERROR;
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void* buff) {
    (void)pdrv;
    if (!msc_host_mounted()) return RES_NOTRDY;
    switch (cmd) {
        case CTRL_SYNC:
            return RES_OK;  // writes are synchronous (msc_host blocks)
        case GET_SECTOR_COUNT:
            *(LBA_t*)buff = msc_host_block_count();
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD*)buff = (WORD)msc_host_block_size();
            return RES_OK;
        case GET_BLOCK_SIZE:
            *(DWORD*)buff = 1;  // erase block size unknown; conservative default
            return RES_OK;
        default:
            return RES_PARERR;
    }
}
