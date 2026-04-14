// pad_config_storage_nrf.c - nRF52840 Zephyr NVS storage for pad config
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "pad/pad_config_storage.h"
#include <zephyr/fs/nvs.h>
#include <string.h>
#include <stdio.h>

// NVS key for pad config (must not collide with flash_nrf.c key=1 or btstack TLV keys)
#define NVS_PAD_CONFIG_KEY 0x50  // High key to avoid collision with flash_nrf (1) and btstack TLV

// Get shared NVS instance from flash_nrf.c
extern struct nvs_fs* flash_nrf_get_nvs(void);

void pad_config_storage_init(void) {
    // NVS is already mounted by flash_nrf.c
    printf("[pad_config] nRF NVS storage ready\n");
}

bool pad_config_storage_load(pad_config_flash_t* out) {
    struct nvs_fs* nvs = flash_nrf_get_nvs();
    if (!nvs) {
        printf("[pad_config] NVS not available for load\n");
        return false;
    }

    int rc = nvs_read(nvs, NVS_PAD_CONFIG_KEY, out, sizeof(pad_config_flash_t));
    if (rc < 0) return false;  // Key not found or error
    if ((size_t)rc != sizeof(pad_config_flash_t)) return false;
    if (out->magic != PAD_CONFIG_MAGIC) return false;

    return true;
}

void pad_config_storage_save(const pad_config_flash_t* config) {
    struct nvs_fs* nvs = flash_nrf_get_nvs();
    if (!nvs) {
        printf("[pad_config] NVS not available\n");
        return;
    }

    pad_config_flash_t write_data;
    memcpy(&write_data, config, sizeof(write_data));
    write_data.magic = PAD_CONFIG_MAGIC;

    // Increment sequence
    pad_config_flash_t existing;
    if (pad_config_storage_load(&existing)) {
        write_data.sequence = existing.sequence + 1;
    } else {
        write_data.sequence = 1;
    }

    int rc = nvs_write(nvs, NVS_PAD_CONFIG_KEY, &write_data, sizeof(write_data));
    if (rc >= 0) {
        printf("[pad_config] Saved to NVS (seq=%lu)\n", (unsigned long)write_data.sequence);
    } else {
        printf("[pad_config] NVS write failed: %d\n", rc);
    }
}

void pad_config_storage_erase(void) {
    struct nvs_fs* nvs = flash_nrf_get_nvs();
    if (!nvs) return;

    nvs_delete(nvs, NVS_PAD_CONFIG_KEY);
    printf("[pad_config] NVS config erased\n");
}
