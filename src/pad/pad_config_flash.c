// pad_config_flash.c - Flash-storable pad GPIO configuration
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Persists pad_device_config_t pin mappings to a dedicated flash sector.
// Uses the same dual-sector journaled pattern as flash.c.

#include "pad_config_flash.h"
#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/flash.h"
#include "tusb.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// FLASH LAYOUT
// ============================================================================
//
// Pad config gets its own sector, placed before the settings sectors.
// Layout (from end of flash):
//   [... code ...] [PadCfg] [Settings B] [Settings A] [BTstack 8KB] [end]
//
// We use a single sector (16 slots) — pad config changes rarely enough
// that a single sector is sufficient.

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

#define PAD_SLOT_SIZE   FLASH_PAGE_SIZE  // 256 bytes
#define PAD_SLOTS       (FLASH_SECTOR_SIZE / PAD_SLOT_SIZE)  // 16 slots

static uint32_t pad_current_sequence = 0;

// Static runtime config (filled by pad_config_from_flash)
static pad_device_config_t runtime_config;
static char runtime_name[PAD_CONFIG_NAME_LEN];

// ============================================================================
// FLASH HELPERS
// ============================================================================

static const pad_config_flash_t* get_pad_slot(uint8_t index)
{
    return (const pad_config_flash_t*)(XIP_BASE + PAD_CONFIG_SECTOR_OFFSET + (index * PAD_SLOT_SIZE));
}

static int find_newest_pad_slot(void)
{
    int newest = -1;
    uint32_t highest_seq = 0;

    for (uint8_t i = 0; i < PAD_SLOTS; i++) {
        const pad_config_flash_t* slot = get_pad_slot(i);
        if (slot->magic == PAD_CONFIG_MAGIC && slot->sequence != 0xFFFFFFFF) {
            if (newest == -1 || slot->sequence > highest_seq) {
                highest_seq = slot->sequence;
                newest = i;
            }
        }
    }

    return newest;
}

static int find_empty_pad_slot(void)
{
    for (uint8_t i = 0; i < PAD_SLOTS; i++) {
        const pad_config_flash_t* slot = get_pad_slot(i);
        if (slot->sequence == 0xFFFFFFFF) {
            return i;
        }
    }
    return -1;
}

// Helper to flush debug output before critical sections
static void flush_output(void)
{
#if CFG_TUD_ENABLED
    tud_task();
    sleep_ms(20);
    tud_task();
#else
    sleep_ms(20);
#endif
}

typedef struct {
    uint32_t offset;
    const uint8_t* data;
} page_program_params_t;

static void __no_inline_not_in_flash_func(pad_page_program_worker)(void* param)
{
    page_program_params_t* p = (page_program_params_t*)param;
    flash_range_program(p->offset, p->data, FLASH_PAGE_SIZE);
}

typedef struct {
    uint32_t offset;
} sector_erase_params_t;

static void __no_inline_not_in_flash_func(pad_sector_erase_worker)(void* param)
{
    sector_erase_params_t* p = (sector_erase_params_t*)param;
    flash_range_erase(p->offset, FLASH_SECTOR_SIZE);
}

static void pad_flash_write_page(uint8_t slot_index, const pad_config_flash_t* config)
{
    static pad_config_flash_t write_buffer;
    memcpy(&write_buffer, config, sizeof(pad_config_flash_t));

    uint32_t offset = PAD_CONFIG_SECTOR_OFFSET + (slot_index * PAD_SLOT_SIZE);

    page_program_params_t params = {
        .offset = offset,
        .data = (const uint8_t*)&write_buffer
    };

    int result = flash_safe_execute(pad_page_program_worker, &params, UINT32_MAX);
    if (result != PICO_OK) {
        uint32_t ints = save_and_disable_interrupts();
        flash_range_program(offset, (const uint8_t*)&write_buffer, FLASH_PAGE_SIZE);
        restore_interrupts(ints);
    }
}

static void pad_flash_erase_sector(void)
{
    printf("[pad_config] Erasing pad config sector at 0x%lX\n",
           (unsigned long)PAD_CONFIG_SECTOR_OFFSET);
    flush_output();

    sector_erase_params_t params = { .offset = PAD_CONFIG_SECTOR_OFFSET };

    int result = flash_safe_execute(pad_sector_erase_worker, &params, UINT32_MAX);
    if (result != PICO_OK) {
        uint32_t ints = save_and_disable_interrupts();
        flash_range_erase(PAD_CONFIG_SECTOR_OFFSET, FLASH_SECTOR_SIZE);
        restore_interrupts(ints);
    }
}

// ============================================================================
// CONVERSION FUNCTIONS
// ============================================================================

void pad_config_to_flash(const pad_device_config_t* config, pad_config_flash_t* flash)
{
    memset(flash, 0, sizeof(pad_config_flash_t));

    flash->magic = PAD_CONFIG_MAGIC;

    // Name
    if (config->name) {
        strncpy(flash->name, config->name, PAD_CONFIG_NAME_LEN - 1);
        flash->name[PAD_CONFIG_NAME_LEN - 1] = '\0';
    }

    // Flags
    flash->flags = 0;
    if (config->active_high)        flash->flags |= PAD_FLAG_ACTIVE_HIGH;
    if (config->dpad_toggle_invert) flash->flags |= PAD_FLAG_DPAD_TOGGLE_INVERT;
    if (config->invert_lx)          flash->flags |= PAD_FLAG_INVERT_LX;
    if (config->invert_ly)          flash->flags |= PAD_FLAG_INVERT_LY;
    if (config->invert_rx)          flash->flags |= PAD_FLAG_INVERT_RX;
    if (config->invert_ry)          flash->flags |= PAD_FLAG_INVERT_RY;

    // I2C
    flash->i2c_sda = config->i2c_sda;
    flash->i2c_scl = config->i2c_scl;

    // Deadzone
    flash->deadzone = config->deadzone;

    // Buttons
    flash->buttons[PAD_BTN_DPAD_UP]    = config->dpad_up;
    flash->buttons[PAD_BTN_DPAD_DOWN]  = config->dpad_down;
    flash->buttons[PAD_BTN_DPAD_LEFT]  = config->dpad_left;
    flash->buttons[PAD_BTN_DPAD_RIGHT] = config->dpad_right;
    flash->buttons[PAD_BTN_B1]  = config->b1;
    flash->buttons[PAD_BTN_B2]  = config->b2;
    flash->buttons[PAD_BTN_B3]  = config->b3;
    flash->buttons[PAD_BTN_B4]  = config->b4;
    flash->buttons[PAD_BTN_L1]  = config->l1;
    flash->buttons[PAD_BTN_R1]  = config->r1;
    flash->buttons[PAD_BTN_L2]  = config->l2;
    flash->buttons[PAD_BTN_R2]  = config->r2;
    flash->buttons[PAD_BTN_S1]  = config->s1;
    flash->buttons[PAD_BTN_S2]  = config->s2;
    flash->buttons[PAD_BTN_L3]  = config->l3;
    flash->buttons[PAD_BTN_R3]  = config->r3;
    flash->buttons[PAD_BTN_A1]  = config->a1;
    flash->buttons[PAD_BTN_A2]  = config->a2;
    flash->buttons[PAD_BTN_L4]  = config->l4;
    flash->buttons[PAD_BTN_R4]  = config->r4;

    // D-pad toggle
    flash->dpad_toggle = config->dpad_toggle;

    // ADC
    flash->adc_channels[0] = config->adc_lx;
    flash->adc_channels[1] = config->adc_ly;
    flash->adc_channels[2] = config->adc_rx;
    flash->adc_channels[3] = config->adc_ry;

    // LED
    flash->led_pin = config->led_pin;
    flash->led_count = config->led_count;

    // Speaker
    flash->speaker_pin = config->speaker_pin;
    flash->speaker_enable_pin = config->speaker_enable_pin;

    // Display
    flash->display_spi = config->display_spi;
    flash->display_sck = config->display_sck;
    flash->display_mosi = config->display_mosi;
    flash->display_cs = config->display_cs;
    flash->display_dc = config->display_dc;
    flash->display_rst = config->display_rst;

    // QWIIC
    flash->qwiic_tx = config->qwiic_tx;
    flash->qwiic_rx = config->qwiic_rx;
    flash->qwiic_i2c_inst = config->qwiic_i2c_inst;
}

const pad_device_config_t* pad_config_from_flash(const pad_config_flash_t* flash)
{
    memset(&runtime_config, 0, sizeof(pad_device_config_t));

    // Name — copy to persistent buffer
    strncpy(runtime_name, flash->name, PAD_CONFIG_NAME_LEN - 1);
    runtime_name[PAD_CONFIG_NAME_LEN - 1] = '\0';
    runtime_config.name = runtime_name;

    // Flags
    runtime_config.active_high        = (flash->flags & PAD_FLAG_ACTIVE_HIGH) != 0;
    runtime_config.dpad_toggle_invert = (flash->flags & PAD_FLAG_DPAD_TOGGLE_INVERT) != 0;
    runtime_config.invert_lx          = (flash->flags & PAD_FLAG_INVERT_LX) != 0;
    runtime_config.invert_ly          = (flash->flags & PAD_FLAG_INVERT_LY) != 0;
    runtime_config.invert_rx          = (flash->flags & PAD_FLAG_INVERT_RX) != 0;
    runtime_config.invert_ry          = (flash->flags & PAD_FLAG_INVERT_RY) != 0;

    // I2C
    runtime_config.i2c_sda = flash->i2c_sda;
    runtime_config.i2c_scl = flash->i2c_scl;

    // Deadzone
    runtime_config.deadzone = flash->deadzone;

    // Buttons
    runtime_config.dpad_up    = flash->buttons[PAD_BTN_DPAD_UP];
    runtime_config.dpad_down  = flash->buttons[PAD_BTN_DPAD_DOWN];
    runtime_config.dpad_left  = flash->buttons[PAD_BTN_DPAD_LEFT];
    runtime_config.dpad_right = flash->buttons[PAD_BTN_DPAD_RIGHT];
    runtime_config.b1  = flash->buttons[PAD_BTN_B1];
    runtime_config.b2  = flash->buttons[PAD_BTN_B2];
    runtime_config.b3  = flash->buttons[PAD_BTN_B3];
    runtime_config.b4  = flash->buttons[PAD_BTN_B4];
    runtime_config.l1  = flash->buttons[PAD_BTN_L1];
    runtime_config.r1  = flash->buttons[PAD_BTN_R1];
    runtime_config.l2  = flash->buttons[PAD_BTN_L2];
    runtime_config.r2  = flash->buttons[PAD_BTN_R2];
    runtime_config.s1  = flash->buttons[PAD_BTN_S1];
    runtime_config.s2  = flash->buttons[PAD_BTN_S2];
    runtime_config.l3  = flash->buttons[PAD_BTN_L3];
    runtime_config.r3  = flash->buttons[PAD_BTN_R3];
    runtime_config.a1  = flash->buttons[PAD_BTN_A1];
    runtime_config.a2  = flash->buttons[PAD_BTN_A2];
    runtime_config.l4  = flash->buttons[PAD_BTN_L4];
    runtime_config.r4  = flash->buttons[PAD_BTN_R4];

    // D-pad toggle
    runtime_config.dpad_toggle = flash->dpad_toggle;

    // ADC
    runtime_config.adc_lx = flash->adc_channels[0];
    runtime_config.adc_ly = flash->adc_channels[1];
    runtime_config.adc_rx = flash->adc_channels[2];
    runtime_config.adc_ry = flash->adc_channels[3];

    // LED (colors/button_map/pulse_mask stay zeroed — compile-time only)
    runtime_config.led_pin = flash->led_pin;
    runtime_config.led_count = flash->led_count;

    // Speaker
    runtime_config.speaker_pin = flash->speaker_pin;
    runtime_config.speaker_enable_pin = flash->speaker_enable_pin;

    // Display
    runtime_config.display_spi = flash->display_spi;
    runtime_config.display_sck = flash->display_sck;
    runtime_config.display_mosi = flash->display_mosi;
    runtime_config.display_cs = flash->display_cs;
    runtime_config.display_dc = flash->display_dc;
    runtime_config.display_rst = flash->display_rst;

    // QWIIC
    runtime_config.qwiic_tx = flash->qwiic_tx;
    runtime_config.qwiic_rx = flash->qwiic_rx;
    runtime_config.qwiic_i2c_inst = flash->qwiic_i2c_inst;

    return &runtime_config;
}

// ============================================================================
// PUBLIC API
// ============================================================================

void pad_config_flash_init(void)
{
    int newest = find_newest_pad_slot();
    if (newest >= 0) {
        pad_current_sequence = get_pad_slot(newest)->sequence;
        printf("[pad_config] Found saved config in slot %d (seq=%lu)\n",
               newest, (unsigned long)pad_current_sequence);
    } else {
        pad_current_sequence = 0;
        printf("[pad_config] No saved pad config found\n");
    }
}

const pad_device_config_t* pad_config_load_runtime(void)
{
    int newest = find_newest_pad_slot();
    if (newest < 0) {
        return NULL;  // No saved config
    }

    const pad_config_flash_t* slot = get_pad_slot(newest);
    const pad_device_config_t* config = pad_config_from_flash(slot);
    printf("[pad_config] Loaded config: %s\n", config->name);
    return config;
}

void pad_config_save(const pad_device_config_t* config)
{
    pad_config_flash_t flash_data;
    pad_config_to_flash(config, &flash_data);
    flash_data.sequence = ++pad_current_sequence;

    int slot = find_empty_pad_slot();
    if (slot < 0) {
        // Sector full — erase and write to slot 0
        pad_flash_erase_sector();
        slot = 0;
    }

    printf("[pad_config] Writing config to slot %d (seq=%lu)\n",
           slot, (unsigned long)flash_data.sequence);
    pad_flash_write_page(slot, &flash_data);

    // Verify
    const pad_config_flash_t* verify = get_pad_slot(slot);
    printf("[pad_config] Verify: magic=0x%08lX, name=%s\n",
           (unsigned long)verify->magic, verify->name);
}

void pad_config_reset(void)
{
    // Erase the entire pad config sector
    pad_flash_erase_sector();
    pad_current_sequence = 0;
    printf("[pad_config] Config reset (reverts to compile-time default on reboot)\n");
}

bool pad_config_has_custom(void)
{
    return find_newest_pad_slot() >= 0;
}

const char* pad_config_get_name(void)
{
    int newest = find_newest_pad_slot();
    if (newest < 0) return NULL;

    const pad_config_flash_t* slot = get_pad_slot(newest);
    return slot->name;
}
