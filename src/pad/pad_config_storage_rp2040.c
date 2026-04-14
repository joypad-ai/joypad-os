// pad_config_storage_rp2040.c - RP2040 flash sector storage for pad config
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Single-sector journaled storage (16 slots × 256 bytes).

#include "pad_config_storage.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "tusb.h"
#include <string.h>
#include <stdio.h>

// Flash layout: pad config sector sits before the settings sectors
#define BTSTACK_FLASH_SIZE (FLASH_SECTOR_SIZE * 2)

#if PICO_RP2350 && PICO_RP2350_A2_SUPPORTED
#define SETTINGS_SECTOR_A_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE - BTSTACK_FLASH_SIZE - FLASH_SECTOR_SIZE)
#define SETTINGS_SECTOR_B_OFFSET (SETTINGS_SECTOR_A_OFFSET - FLASH_SECTOR_SIZE)
#define PAD_CONFIG_SECTOR_OFFSET (SETTINGS_SECTOR_B_OFFSET - FLASH_SECTOR_SIZE)
#else
#define SETTINGS_SECTOR_A_OFFSET (PICO_FLASH_SIZE_BYTES - BTSTACK_FLASH_SIZE - FLASH_SECTOR_SIZE)
#define SETTINGS_SECTOR_B_OFFSET (SETTINGS_SECTOR_A_OFFSET - FLASH_SECTOR_SIZE)
#define PAD_CONFIG_SECTOR_OFFSET (SETTINGS_SECTOR_B_OFFSET - FLASH_SECTOR_SIZE)
#endif

#define PAD_SLOT_SIZE   FLASH_PAGE_SIZE
#define PAD_SLOTS       (FLASH_SECTOR_SIZE / PAD_SLOT_SIZE)

static uint32_t pad_sequence = 0;

static const pad_config_flash_t* get_slot(uint8_t index) {
    return (const pad_config_flash_t*)(XIP_BASE + PAD_CONFIG_SECTOR_OFFSET + (index * PAD_SLOT_SIZE));
}

static int find_newest_slot(void) {
    int newest = -1;
    uint32_t highest = 0;
    for (uint8_t i = 0; i < PAD_SLOTS; i++) {
        const pad_config_flash_t* slot = get_slot(i);
        if (slot->magic == PAD_CONFIG_MAGIC && slot->sequence != 0xFFFFFFFF) {
            if (newest == -1 || slot->sequence > highest) {
                highest = slot->sequence;
                newest = i;
            }
        }
    }
    return newest;
}

static int find_empty_slot(void) {
    for (uint8_t i = 0; i < PAD_SLOTS; i++) {
        if (get_slot(i)->sequence == 0xFFFFFFFF) return i;
    }
    return -1;
}

static void flush_output(void) {
#if CFG_TUD_ENABLED
    tud_task(); sleep_ms(20); tud_task();
#else
    sleep_ms(20);
#endif
}

typedef struct { uint32_t offset; const uint8_t* data; } page_params_t;
typedef struct { uint32_t offset; } erase_params_t;

static void __no_inline_not_in_flash_func(page_worker)(void* p) {
    page_params_t* pp = (page_params_t*)p;
    flash_range_program(pp->offset, pp->data, FLASH_PAGE_SIZE);
}

static void __no_inline_not_in_flash_func(erase_worker)(void* p) {
    erase_params_t* ep = (erase_params_t*)p;
    flash_range_erase(ep->offset, FLASH_SECTOR_SIZE);
}

static void write_page(uint8_t slot, const pad_config_flash_t* config) {
    static pad_config_flash_t buf;
    memcpy(&buf, config, sizeof(buf));
    uint32_t offset = PAD_CONFIG_SECTOR_OFFSET + (slot * PAD_SLOT_SIZE);
    page_params_t params = { .offset = offset, .data = (const uint8_t*)&buf };
    if (flash_safe_execute(page_worker, &params, UINT32_MAX) != PICO_OK) {
        uint32_t ints = save_and_disable_interrupts();
        flash_range_program(offset, (const uint8_t*)&buf, FLASH_PAGE_SIZE);
        restore_interrupts(ints);
    }
}

static void erase_sector(void) {
    printf("[pad_config] Erasing sector at 0x%lX\n", (unsigned long)PAD_CONFIG_SECTOR_OFFSET);
    flush_output();
    erase_params_t params = { .offset = PAD_CONFIG_SECTOR_OFFSET };
    if (flash_safe_execute(erase_worker, &params, UINT32_MAX) != PICO_OK) {
        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(PAD_CONFIG_SECTOR_OFFSET, FLASH_SECTOR_SIZE);
        restore_interrupts(ints);
    }
}

// ============================================================================
// HAL INTERFACE
// ============================================================================

void pad_config_storage_init(void) {
    int newest = find_newest_slot();
    if (newest >= 0) {
        pad_sequence = get_slot(newest)->sequence;
        printf("[pad_config] Found config in slot %d (seq=%lu)\n",
               newest, (unsigned long)pad_sequence);
    } else {
        pad_sequence = 0;
        printf("[pad_config] No saved config found\n");
    }
}

bool pad_config_storage_load(pad_config_flash_t* out) {
    int newest = find_newest_slot();
    if (newest < 0) return false;
    memcpy(out, get_slot(newest), sizeof(pad_config_flash_t));
    pad_sequence = out->sequence;
    return true;
}

void pad_config_storage_save(const pad_config_flash_t* config) {
    pad_config_flash_t write_data;
    memcpy(&write_data, config, sizeof(write_data));
    write_data.magic = PAD_CONFIG_MAGIC;
    write_data.sequence = ++pad_sequence;

    int slot = find_empty_slot();
    if (slot < 0) {
        erase_sector();
        slot = 0;
    }

    printf("[pad_config] Writing to slot %d (seq=%lu)\n",
           slot, (unsigned long)write_data.sequence);
    write_page(slot, &write_data);
}

void pad_config_storage_erase(void) {
    erase_sector();
    pad_sequence = 0;
    printf("[pad_config] Config erased\n");
}
