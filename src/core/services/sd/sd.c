// sd.c - SD card filesystem service (FatFs bridge)
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Robert Dale Smith

#include "sd.h"
#include "platform/platform_sd.h"
#include "ff.h"
#include <string.h>
#include <stdio.h>

// Single global mount point — see ffconf.h FF_VOLUMES = 1.
static FATFS g_fs;
static bool g_mounted = false;

// Block device handle exposed to diskio.c via this getter.
static platform_sd_t g_sd_dev = NULL;
platform_sd_t sd_get_block_device(void) {
    return g_sd_dev;
}

bool sd_init(platform_sd_t dev)
{
    if (!dev) {
        printf("[sd] init called with NULL block device\n");
        return false;
    }
    g_sd_dev = dev;
    g_mounted = false;

    // Mount immediately (mount opt = 1) so diskio errors surface here
    // rather than at first f_open().
    FRESULT fr = f_mount(&g_fs, "", 1);
    if (fr != FR_OK) {
        printf("[sd] f_mount failed (FRESULT=%d)\n", (int)fr);
        return false;
    }
    g_mounted = true;
    return true;
}

bool sd_mounted(void) {
    return g_mounted;
}

uint64_t sd_total_bytes(void)
{
    if (!g_mounted) return 0;
    DWORD free_clusters;
    FATFS* fs;
    if (f_getfree("", &free_clusters, &fs) != FR_OK) return 0;
    DWORD total_clusters = fs->n_fatent - 2;
    return (uint64_t)total_clusters * fs->csize * 512ULL;
}

uint64_t sd_free_bytes(void)
{
    if (!g_mounted) return 0;
    DWORD free_clusters;
    FATFS* fs;
    if (f_getfree("", &free_clusters, &fs) != FR_OK) return 0;
    return (uint64_t)free_clusters * fs->csize * 512ULL;
}

int sd_read_file(const char* path, void* buf, size_t bufsize)
{
    if (!g_mounted || !path || !buf) return -1;
    FIL f;
    if (f_open(&f, path, FA_READ) != FR_OK) return -1;
    UINT got = 0;
    FRESULT fr = f_read(&f, buf, (UINT)bufsize, &got);
    f_close(&f);
    if (fr != FR_OK) return -1;
    return (int)got;
}

int sd_write_file(const char* path, const void* buf, size_t len)
{
    if (!g_mounted || !path) return -1;
    FIL f;
    if (f_open(&f, path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return -1;
    UINT wrote = 0;
    FRESULT fr = f_write(&f, buf, (UINT)len, &wrote);
    if (fr == FR_OK) fr = f_sync(&f);
    f_close(&f);
    if (fr != FR_OK || wrote != len) return -1;
    return 0;
}
