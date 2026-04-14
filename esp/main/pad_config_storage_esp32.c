// pad_config_storage_esp32.c - ESP32 NVS storage for pad config
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "pad/pad_config_storage.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

#define NVS_NAMESPACE "joypad"
#define NVS_PAD_KEY   "pad_config"

void pad_config_storage_init(void) {
    // NVS is already initialized by flash_esp32.c
    printf("[pad_config] ESP32 NVS storage ready\n");
}

bool pad_config_storage_load(pad_config_flash_t* out) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) return false;

    size_t size = sizeof(pad_config_flash_t);
    err = nvs_get_blob(handle, NVS_PAD_KEY, out, &size);
    nvs_close(handle);

    if (err != ESP_OK || size != sizeof(pad_config_flash_t)) return false;
    if (out->magic != PAD_CONFIG_MAGIC) return false;

    printf("[pad_config] Loaded from NVS (seq=%lu)\n", (unsigned long)out->sequence);
    return true;
}

void pad_config_storage_save(const pad_config_flash_t* config) {
    pad_config_flash_t write_data;
    memcpy(&write_data, config, sizeof(write_data));
    write_data.magic = PAD_CONFIG_MAGIC;

    // Increment sequence for consistency (not strictly needed for NVS)
    pad_config_flash_t existing;
    if (pad_config_storage_load(&existing)) {
        write_data.sequence = existing.sequence + 1;
    } else {
        write_data.sequence = 1;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        printf("[pad_config] NVS open failed: %d\n", err);
        return;
    }

    err = nvs_set_blob(handle, NVS_PAD_KEY, &write_data, sizeof(write_data));
    if (err == ESP_OK) {
        nvs_commit(handle);
        printf("[pad_config] Saved to NVS (seq=%lu)\n", (unsigned long)write_data.sequence);
    } else {
        printf("[pad_config] NVS write failed: %d\n", err);
    }
    nvs_close(handle);
}

void pad_config_storage_erase(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return;

    nvs_erase_key(handle, NVS_PAD_KEY);
    nvs_commit(handle);
    nvs_close(handle);
    printf("[pad_config] NVS config erased\n");
}
