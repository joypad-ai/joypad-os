// platform_i2c_nrf.c - nRF52840 I2C implementation (Zephyr driver)
//
// Wraps Zephyr I2C driver for the platform I2C HAL.
// Used by JoyWing seesaw input on Feather nRF52840.

#include "platform/platform_i2c.h"
#include <zephyr/drivers/i2c.h>
#include <stdio.h>

#define MAX_I2C_BUSES 2

// Static bus state (nRF52840 has i2c0 and i2c1)
static struct platform_i2c {
    const struct device *dev;
    bool initialized;
} i2c_buses[MAX_I2C_BUSES];

platform_i2c_t platform_i2c_init(const platform_i2c_config_t* config)
{
    if (!config || config->bus >= MAX_I2C_BUSES) return NULL;

    struct platform_i2c* bus = &i2c_buses[config->bus];
    if (bus->initialized) return bus;

    // Get Zephyr I2C device (pins configured via devicetree). _OR_NULL so
    // boards that leave a bus disabled in DT (e.g. Makerdiary MDK dongle has
    // no i2c0) still compile — the bus just reports not-ready at runtime.
    if (config->bus == 0) {
        bus->dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(i2c0));
    } else {
        bus->dev = DEVICE_DT_GET_OR_NULL(DT_NODELABEL(i2c1));
    }

    if (!bus->dev || !device_is_ready(bus->dev)) {
        printf("[i2c:nrf] Bus %d device not ready\n", config->bus);
        return NULL;
    }

    bus->initialized = true;
    printf("[i2c:nrf] Bus %d initialized (SDA=%d, SCL=%d)\n",
           config->bus, config->sda_pin, config->scl_pin);

    return bus;
}

int platform_i2c_write(platform_i2c_t bus, uint8_t addr, const uint8_t* data, size_t len)
{
    if (!bus || !bus->initialized) return -1;
    return i2c_write(bus->dev, data, len, addr);
}

int platform_i2c_read(platform_i2c_t bus, uint8_t addr, uint8_t* data, size_t len)
{
    if (!bus || !bus->initialized) return -1;
    return i2c_read(bus->dev, data, len, addr);
}

int platform_i2c_write_read(platform_i2c_t bus, uint8_t addr,
                            const uint8_t* wr, size_t wr_len,
                            uint8_t* rd, size_t rd_len)
{
    if (!bus || !bus->initialized) return -1;
    return i2c_write_read(bus->dev, addr, wr, wr_len, rd, rd_len);
}
