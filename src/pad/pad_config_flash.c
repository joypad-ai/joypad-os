// pad_config_flash.c - Platform-agnostic pad GPIO configuration
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Conversion between runtime pad_device_config_t and flash storage format.
// Storage backend provided by pad_config_storage_*.c (per-platform).

#include "pad_config_flash.h"
#include "pad_config_storage.h"
#include <string.h>
#include <stdio.h>

// Static runtime config (filled by pad_config_from_flash)
static pad_device_config_t runtime_config;
static char runtime_name[PAD_CONFIG_NAME_LEN + 1];

// ============================================================================
// CONVERSION FUNCTIONS
// ============================================================================

void pad_config_to_flash(const pad_device_config_t* config, pad_config_flash_t* flash)
{
    memset(flash, 0, sizeof(pad_config_flash_t));
    flash->magic = PAD_CONFIG_MAGIC;

    if (config->name) {
        strncpy(flash->name, config->name, PAD_CONFIG_NAME_LEN - 1);
        flash->name[PAD_CONFIG_NAME_LEN - 1] = '\0';
    }

    flash->flags = 0;
    if (config->active_high)        flash->flags |= PAD_FLAG_ACTIVE_HIGH;
    if (config->dpad_toggle_invert) flash->flags |= PAD_FLAG_DPAD_TOGGLE_INVERT;
    if (config->invert_lx)          flash->flags |= PAD_FLAG_INVERT_LX;
    if (config->invert_ly)          flash->flags |= PAD_FLAG_INVERT_LY;
    if (config->invert_rx)          flash->flags |= PAD_FLAG_INVERT_RX;
    if (config->invert_ry)          flash->flags |= PAD_FLAG_INVERT_RY;

    flash->i2c_sda = config->i2c_sda;
    flash->i2c_scl = config->i2c_scl;
    flash->deadzone = config->deadzone;

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
    flash->buttons[PAD_BTN_A3]  = config->a3;
    flash->buttons[PAD_BTN_A4]  = config->a4;
    flash->buttons[PAD_BTN_L4]  = config->l4;
    flash->buttons[PAD_BTN_R4]  = config->r4;

    flash->dpad_toggle = config->dpad_toggle;

    flash->adc_channels[0] = config->adc_lx;
    flash->adc_channels[1] = config->adc_ly;
    flash->adc_channels[2] = config->adc_rx;
    flash->adc_channels[3] = config->adc_ry;

    flash->led_pin = config->led_pin;
    flash->led_count = config->led_count;

    flash->speaker_pin = config->speaker_pin;
    flash->speaker_enable_pin = config->speaker_enable_pin;

    flash->display_spi = config->display_spi;
    flash->display_sck = config->display_sck;
    flash->display_mosi = config->display_mosi;
    flash->display_cs = config->display_cs;
    flash->display_dc = config->display_dc;
    flash->display_rst = config->display_rst;

    flash->qwiic_tx = config->qwiic_tx;
    flash->qwiic_rx = config->qwiic_rx;
    flash->qwiic_i2c_inst = config->qwiic_i2c_inst;

    flash->usb_host_dp = config->usb_host_dp;

    for (int i = 0; i < 2; i++) {
        flash->joywing[i].i2c_bus = config->joywing[i].i2c_bus;
        flash->joywing[i].sda = config->joywing[i].sda;
        flash->joywing[i].scl = config->joywing[i].scl;
    }
}

const pad_device_config_t* pad_config_from_flash(const pad_config_flash_t* flash)
{
    memset(&runtime_config, 0, sizeof(pad_device_config_t));

    strncpy(runtime_name, flash->name, PAD_CONFIG_NAME_LEN - 1);
    runtime_name[PAD_CONFIG_NAME_LEN - 1] = '\0';
    runtime_config.name = runtime_name;

    runtime_config.active_high        = (flash->flags & PAD_FLAG_ACTIVE_HIGH) != 0;
    runtime_config.dpad_toggle_invert = (flash->flags & PAD_FLAG_DPAD_TOGGLE_INVERT) != 0;
    runtime_config.invert_lx          = (flash->flags & PAD_FLAG_INVERT_LX) != 0;
    runtime_config.invert_ly          = (flash->flags & PAD_FLAG_INVERT_LY) != 0;
    runtime_config.invert_rx          = (flash->flags & PAD_FLAG_INVERT_RX) != 0;
    runtime_config.invert_ry          = (flash->flags & PAD_FLAG_INVERT_RY) != 0;

    runtime_config.i2c_sda = flash->i2c_sda;
    runtime_config.i2c_scl = flash->i2c_scl;
    runtime_config.deadzone = flash->deadzone;

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
    runtime_config.a3  = flash->buttons[PAD_BTN_A3];
    runtime_config.a4  = flash->buttons[PAD_BTN_A4];
    runtime_config.l4  = flash->buttons[PAD_BTN_L4];
    runtime_config.r4  = flash->buttons[PAD_BTN_R4];

    runtime_config.dpad_toggle = flash->dpad_toggle;

    runtime_config.adc_lx = flash->adc_channels[0];
    runtime_config.adc_ly = flash->adc_channels[1];
    runtime_config.adc_rx = flash->adc_channels[2];
    runtime_config.adc_ry = flash->adc_channels[3];

    runtime_config.led_pin = flash->led_pin;
    runtime_config.led_count = flash->led_count;

    runtime_config.speaker_pin = flash->speaker_pin;
    runtime_config.speaker_enable_pin = flash->speaker_enable_pin;

    runtime_config.display_spi = flash->display_spi;
    runtime_config.display_sck = flash->display_sck;
    runtime_config.display_mosi = flash->display_mosi;
    runtime_config.display_cs = flash->display_cs;
    runtime_config.display_dc = flash->display_dc;
    runtime_config.display_rst = flash->display_rst;

    runtime_config.qwiic_tx = flash->qwiic_tx;
    runtime_config.qwiic_rx = flash->qwiic_rx;
    runtime_config.qwiic_i2c_inst = flash->qwiic_i2c_inst;

    runtime_config.usb_host_dp = flash->usb_host_dp;

    for (int i = 0; i < 2; i++) {
        runtime_config.joywing[i].i2c_bus = flash->joywing[i].i2c_bus;
        runtime_config.joywing[i].sda = flash->joywing[i].sda;
        runtime_config.joywing[i].scl = flash->joywing[i].scl;
    }

    return &runtime_config;
}

// ============================================================================
// PUBLIC API (delegates to platform storage)
// ============================================================================

void pad_config_flash_init(void) {
    pad_config_storage_init();
}

const pad_device_config_t* pad_config_load_runtime(void) {
    pad_config_flash_t flash_data;
    if (!pad_config_storage_load(&flash_data)) {
        return NULL;
    }
    const pad_device_config_t* config = pad_config_from_flash(&flash_data);
    printf("[pad_config] Loaded: %s\n", config->name);
    return config;
}

void pad_config_save(const pad_device_config_t* config) {
    pad_config_flash_t flash_data;
    pad_config_to_flash(config, &flash_data);
    pad_config_storage_save(&flash_data);
}

void pad_config_reset(void) {
    pad_config_storage_erase();
    printf("[pad_config] Reset to default\n");
}

bool pad_config_has_custom(void) {
    pad_config_flash_t tmp;
    return pad_config_storage_load(&tmp);
}

const char* pad_config_get_name(void) {
    pad_config_flash_t tmp;
    if (!pad_config_storage_load(&tmp)) return NULL;
    static char name[PAD_CONFIG_NAME_LEN];
    strncpy(name, tmp.name, PAD_CONFIG_NAME_LEN - 1);
    name[PAD_CONFIG_NAME_LEN - 1] = '\0';
    return name;
}
