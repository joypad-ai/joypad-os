// display_i2c.c - I2C transport for SH1107 OLED
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// I2C transport for Adafruit FeatherWing OLED 128x64 (SH1107).

#include "display.h"
#include "display_transport.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

static i2c_inst_t* i2c = NULL;
static uint8_t i2c_addr = 0x3C;

static void i2c_write_cmd(uint8_t cmd) {
    uint8_t buf[2] = { 0x00, cmd };  // Co=0, D/C#=0 (command)
    i2c_write_blocking(i2c, i2c_addr, buf, 2, false);
}

static void i2c_write_data(const uint8_t* data, size_t len) {
    // I2C data write: control byte 0x40 followed by data
    // Send in one transaction: [0x40, data0, data1, ...]
    static uint8_t buf[129];  // 1 control + 128 data max
    buf[0] = 0x40;  // Co=0, D/C#=1 (data)
    size_t chunk = (len > 128) ? 128 : len;
    memcpy(buf + 1, data, chunk);
    i2c_write_blocking(i2c, i2c_addr, buf, chunk + 1, false);
}

bool display_i2c_init(const display_i2c_config_t* config) {
    i2c = (config->i2c_inst == 0) ? i2c0 : i2c1;
    i2c_addr = config->addr;

    // Initialize I2C at 400kHz
    i2c_init(i2c, 400 * 1000);
    gpio_set_function(config->pin_sda, GPIO_FUNC_I2C);
    gpio_set_function(config->pin_scl, GPIO_FUNC_I2C);
    gpio_pull_up(config->pin_sda);
    gpio_pull_up(config->pin_scl);

    // Probe: a single-byte write with a short timeout. If no device ACKs,
    // the call returns PICO_ERROR_GENERIC immediately and we bail without
    // registering transport. Without this probe a missing OLED would let
    // the rest of the firmware spend cycles building+shipping a framebuffer
    // every loop iteration, NACKing on every byte.
    uint8_t probe = 0x00;
    int rc = i2c_write_timeout_us(i2c, i2c_addr, &probe, 1, false, 2000);
    if (rc < 0) {
        printf("[display] No I2C device at 0x%02X (rc=%d) — skipping OLED init\n",
               i2c_addr, rc);
        return false;
    }

    // Register transport
    display_set_transport(i2c_write_cmd, i2c_write_data);
    display_set_col_offset(0);  // SH1107 uses no column offset
    return true;
}
