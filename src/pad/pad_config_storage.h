// pad_config_storage.h - Platform-agnostic pad config storage interface
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Each platform implements these functions to persist pad_config_flash_t.
// RP2040: flash sector journaling. ESP32/nRF: NVS blob storage.

#ifndef PAD_CONFIG_STORAGE_H
#define PAD_CONFIG_STORAGE_H

#include <stdint.h>
#include <stdbool.h>
#include "pad_config_flash.h"

// Initialize platform-specific pad config storage
void pad_config_storage_init(void);

// Load the newest valid pad config from storage
// Returns true if valid config found, fills *out
bool pad_config_storage_load(pad_config_flash_t* out);

// Save pad config to storage (immediate write)
void pad_config_storage_save(const pad_config_flash_t* config);

// Erase all stored pad config (revert to compile-time default on reboot)
void pad_config_storage_erase(void);

#endif // PAD_CONFIG_STORAGE_H
