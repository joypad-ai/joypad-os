// xboxone_device.h

#ifndef XBOXONE_DEVICE_H
#define XBOXONE_DEVICE_H

#include <stdint.h>

#include "hardware/i2c.h"
#include "pico/i2c_slave.h"
#include "core/globals.h"

// Define constants
#undef MAX_PLAYERS
#define MAX_PLAYERS 4

// i2c addresses
#define I2C_SLAVE_ADDRESS 0x21
#define MCP4728_I2C_ADDR0 0x60
#define MCP4728_I2C_ADDR1 0x61

// button combos
#define XBOX_GUIDE_COMBO USBR_BUTTON_S1 | USBR_BUTTON_S2 | USBR_BUTTON_DU

// init gpio
#ifdef ADAFRUIT_QTPY_RP2040

#define I2C_SLAVE_PORT i2c0
#define I2C_SLAVE_SDA_PIN 4 // TP33
#define I2C_SLAVE_SCL_PIN 5 // TP34

#define I2C_DAC_PORT i2c1
#define I2C_DAC_SDA_PIN 22
#define I2C_DAC_SCL_PIN 23

#define XBOX_R3_BTN_PIN 25  // TP43
#define XBOX_L3_BTN_PIN 24  // TP42
#define XBOX_GUIDE_PIN 20   // Cathode side of D27
#define XBOX_B_BTN_PIN 21   // TP41

#define PICO_DEFAULT_WS2812_PIN 12
#define NEOPIXEL_POWER_PIN 11
#define BOOT_BUTTON_PIN 21

#else // #ifdef ADAFRUIT_KB2040

#define I2C_SLAVE_PORT i2c1
#define I2C_SLAVE_SDA_PIN 2
#define I2C_SLAVE_SCL_PIN 3

#define I2C_DAC_PORT i2c0
#define I2C_DAC_SDA_PIN 12
#define I2C_DAC_SCL_PIN 13

#define XBOX_R3_BTN_PIN 6
#define XBOX_L3_BTN_PIN 7
#define XBOX_GUIDE_PIN 8
#define XBOX_B_BTN_PIN 9

#endif

static uint8_t i2c_slave_read_buffer[2] = {0xFA, 0xFF};
static uint8_t i2c_slave_write_buffer[256];
static int i2c_slave_write_buffer_index = 0;

// Declaration of global variables

// Function declarations
void xb1_init(void);
void mcp4728_write_dac(i2c_inst_t *i2c, uint8_t address, uint8_t channel, uint16_t value);
void mcp4728_set_config(i2c_inst_t *i2c, uint8_t address, uint8_t channel, uint8_t gain, uint8_t power_down);
void mcp4728_power_down(i2c_inst_t *i2c, uint8_t address, uint8_t channel, uint8_t pd_mode);
void i2c_slave_handler(i2c_inst_t *i2c, i2c_slave_event_t event);

void __not_in_flash_func(core1_entry)(void);
void __not_in_flash_func(update_output)(void);

#endif // XBOXONE_DEVICE_H
