// pad_config_flash.h - Flash-storable pad GPIO configuration
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Compact struct for persisting pad_device_config_t pin mappings to flash.
// Stored in a dedicated flash sector with journaled writes (same pattern as flash.c).

#ifndef PAD_CONFIG_FLASH_H
#define PAD_CONFIG_FLASH_H

#include <stdint.h>
#include <stdbool.h>
#include "pad_input.h"

// ============================================================================
// FLASH STORAGE FORMAT
// ============================================================================

#define PAD_CONFIG_MAGIC    0x50414443  // "PADC"
#define PAD_CONFIG_NAME_LEN 32

// Compact flash representation of pad_device_config_t (256 bytes = 1 flash page)
// Only stores pin assignments + basic settings — LED colors/button maps are
// too large and rarely changed, so they stay compile-time for now.
typedef struct {
    // Header (8 bytes)
    uint32_t magic;             // PAD_CONFIG_MAGIC
    uint32_t sequence;          // Journaling sequence (higher = newer, 0xFFFFFFFF = empty)

    // Config name (16 bytes)
    char name[PAD_CONFIG_NAME_LEN];

    // Flags (1 byte)
    // Bit 0: active_high
    // Bit 1: dpad_toggle_invert
    // Bit 2: invert_lx
    // Bit 3: invert_ly
    // Bit 4: invert_rx
    // Bit 5: invert_ry
    uint8_t flags;

    // I2C configuration (2 bytes)
    int8_t i2c_sda;
    int8_t i2c_scl;

    // Deadzone (1 byte)
    uint8_t deadzone;

    // Digital button pins — 22 buttons (44 bytes)
    // Order: dpad UDLR, b1-b4, l1/r1/l2/r2, s1/s2, l3/r3, a1/a2, l4/r4
    int16_t buttons[22];

    // D-pad toggle pin (2 bytes)
    int16_t dpad_toggle;

    // ADC channels (4 bytes)
    int8_t adc_channels[4];     // lx, ly, rx, ry

    // LED config (2 bytes)
    int8_t led_pin;
    uint8_t led_count;

    // Speaker config (2 bytes)
    int8_t speaker_pin;
    int8_t speaker_enable_pin;

    // Display config (6 bytes)
    int8_t display_spi;
    int8_t display_sck;
    int8_t display_mosi;
    int8_t display_cs;
    int8_t display_dc;
    int8_t display_rst;

    // QWIIC config (3 bytes)
    int8_t qwiic_tx;
    int8_t qwiic_rx;
    int8_t qwiic_i2c_inst;

    // USB host config (1 byte)
    // PIO-USB D+ pin (D- is always D+1). PAD_PIN_DISABLED = no USB host.
    int8_t usb_host_dp;

    // Reserved for future use (pad to 256 bytes)
    // 256 - 8 - 32 - 1 - 2 - 1 - 44 - 2 - 4 - 2 - 2 - 6 - 3 - 1 = 148
    uint8_t reserved[148];
} pad_config_flash_t;

_Static_assert(sizeof(pad_config_flash_t) == 256, "pad_config_flash_t must be exactly 256 bytes");

// ============================================================================
// BUTTON INDEX MAPPING
// ============================================================================
// Index into pad_config_flash_t.buttons[] array

#define PAD_BTN_DPAD_UP     0
#define PAD_BTN_DPAD_DOWN   1
#define PAD_BTN_DPAD_LEFT   2
#define PAD_BTN_DPAD_RIGHT  3
#define PAD_BTN_B1          4
#define PAD_BTN_B2          5
#define PAD_BTN_B3          6
#define PAD_BTN_B4          7
#define PAD_BTN_L1          8
#define PAD_BTN_R1          9
#define PAD_BTN_L2          10
#define PAD_BTN_R2          11
#define PAD_BTN_S1          12
#define PAD_BTN_S2          13
#define PAD_BTN_L3          14
#define PAD_BTN_R3          15
#define PAD_BTN_A1          16
#define PAD_BTN_A2          17
#define PAD_BTN_A3          18
#define PAD_BTN_A4          19
#define PAD_BTN_L4          20
#define PAD_BTN_R4          21
#define PAD_BTN_COUNT       22

// ============================================================================
// FLAGS
// ============================================================================

#define PAD_FLAG_ACTIVE_HIGH        (1 << 0)
#define PAD_FLAG_DPAD_TOGGLE_INVERT (1 << 1)
#define PAD_FLAG_INVERT_LX          (1 << 2)
#define PAD_FLAG_INVERT_LY          (1 << 3)
#define PAD_FLAG_INVERT_RX          (1 << 4)
#define PAD_FLAG_INVERT_RY          (1 << 5)

// ============================================================================
// API
// ============================================================================

// Initialize pad config flash system (must call before load/save)
void pad_config_flash_init(void);

// Convert runtime config to flash format
void pad_config_to_flash(const pad_device_config_t* config, pad_config_flash_t* flash);

// Convert flash format to runtime config
// Returns a pointer to a static pad_device_config_t (valid until next call)
const pad_device_config_t* pad_config_from_flash(const pad_config_flash_t* flash);

// Load pad config from flash. Returns runtime config, or NULL if no saved config.
const pad_device_config_t* pad_config_load_runtime(void);

// Save pad config to flash
void pad_config_save(const pad_device_config_t* config);

// Delete saved pad config from flash (reverts to compile-time default on reboot)
void pad_config_reset(void);

// Check if a custom pad config is stored in flash
bool pad_config_has_custom(void);

// Get the name of the stored config (or NULL if none)
const char* pad_config_get_name(void);

#endif // PAD_CONFIG_FLASH_H
