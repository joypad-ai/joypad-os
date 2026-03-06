// display_i2c_nrf.c - I2C display transport for Seeed XIAO nRF52840
//
// Implements display_i2c_init() using Zephyr I2C driver.
// Used with SSD1306 OLED on the XIAO Expansion Board (128x64, I2C, 0x3C).

#include "core/services/display/display.h"
#include "core/services/display/display_transport.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/i2c.h>
#include <string.h>
#include <stdio.h>

static const struct device *i2c_dev;
static uint8_t i2c_addr = 0x3C;

static void i2c_write_cmd(uint8_t cmd)
{
    uint8_t buf[2] = { 0x00, cmd };  // Co=0, D/C#=0 (command)
    i2c_write(i2c_dev, buf, 2, i2c_addr);
}

static void i2c_write_data(const uint8_t* data, size_t len)
{
    // I2C data write: control byte 0x40 followed by data
    static uint8_t buf[129];  // 1 control + 128 data max
    buf[0] = 0x40;  // Co=0, D/C#=1 (data)
    size_t chunk = (len > 128) ? 128 : len;
    memcpy(buf + 1, data, chunk);
    i2c_write(i2c_dev, buf, chunk + 1, i2c_addr);
}

void display_i2c_init(const display_i2c_config_t* config)
{
    // Get I2C device from devicetree (i2c1 = XIAO I2C bus, SDA=P0.04, SCL=P0.05)
    i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));
    if (!device_is_ready(i2c_dev)) {
        printf("[display] I2C device not ready\n");
        return;
    }

    i2c_addr = config->addr;

    // Register transport callbacks
    display_set_transport(i2c_write_cmd, i2c_write_data);
    display_set_col_offset(0);  // SSD1306 uses no column offset

    printf("[display] I2C transport initialized (addr=0x%02X)\n", i2c_addr);
}
