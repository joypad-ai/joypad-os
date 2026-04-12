// battery_esp32.c - Battery level monitoring for Feather ESP32-S3
//
// Reads battery percentage from I2C fuel gauge chip.
// Supports MAX17048 (addr 0x36) and LC709203F (addr 0x0B).

#include "platform/platform_i2c.h"
#include "ble/gatt-service/battery_service_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

// I2C addresses
#define MAX17048_ADDR   0x36
#define LC709203F_ADDR  0x0B

// MAX17048 registers
#define MAX17048_SOC    0x04  // State of Charge (%)

// LC709203F registers
#define LC709203F_RSOC  0x0D  // Relative State of Charge (%)

// I2C pins (same as display — shared bus)
#define I2C_SDA_PIN  3
#define I2C_SCL_PIN  4

static platform_i2c_t i2c_bus = NULL;
static uint8_t fuel_gauge_addr = 0;
static bool initialized = false;

#define BATTERY_TASK_STACK_SIZE 2048
#define BATTERY_TASK_PRIORITY   1
#define BATTERY_UPDATE_INTERVAL_MS 30000  // Update every 30 seconds

static uint8_t read_max17048_soc(void)
{
    uint8_t reg = MAX17048_SOC;
    uint8_t data[2] = {0};

    if (platform_i2c_write(i2c_bus, MAX17048_ADDR, &reg, 1) != 0) return 0;
    if (platform_i2c_read(i2c_bus, MAX17048_ADDR, data, 2) != 0) return 0;

    // SOC register: high byte = whole %, low byte = 1/256 %
    uint8_t pct = data[0];
    if (pct > 100) pct = 100;
    return pct;
}

static uint8_t read_lc709203f_soc(void)
{
    uint8_t reg = LC709203F_RSOC;
    uint8_t data[2] = {0};

    if (platform_i2c_write(i2c_bus, LC709203F_ADDR, &reg, 1) != 0) return 0;
    if (platform_i2c_read(i2c_bus, LC709203F_ADDR, data, 2) != 0) return 0;

    // RSOC: 16-bit little-endian percentage
    uint16_t pct = data[0] | (data[1] << 8);
    if (pct > 100) pct = 100;
    return (uint8_t)pct;
}

static uint8_t read_battery_percent(void)
{
    if (!initialized || !i2c_bus) return 100;

    if (fuel_gauge_addr == MAX17048_ADDR) {
        return read_max17048_soc();
    } else if (fuel_gauge_addr == LC709203F_ADDR) {
        return read_lc709203f_soc();
    }
    return 100;
}

static void battery_task_fn(void *arg)
{
    (void)arg;
    while (1) {
        uint8_t pct = read_battery_percent();
        battery_service_server_set_battery_value(pct);
        vTaskDelay(pdMS_TO_TICKS(BATTERY_UPDATE_INTERVAL_MS));
    }
}

void battery_monitor_init(void)
{
    // Initialize I2C bus (may already be initialized by display)
    platform_i2c_config_t i2c_cfg = {
        .bus = 0,
        .sda_pin = I2C_SDA_PIN,
        .scl_pin = I2C_SCL_PIN,
        .freq_hz = 400000,
    };
    i2c_bus = platform_i2c_init(&i2c_cfg);
    if (!i2c_bus) {
        printf("[battery] I2C init failed\n");
        return;
    }

    // Probe for fuel gauge chips
    uint8_t probe = 0x00;
    if (platform_i2c_write(i2c_bus, MAX17048_ADDR, &probe, 1) == 0) {
        fuel_gauge_addr = MAX17048_ADDR;
        printf("[battery] MAX17048 found at 0x%02X\n", fuel_gauge_addr);
    } else if (platform_i2c_write(i2c_bus, LC709203F_ADDR, &probe, 1) == 0) {
        fuel_gauge_addr = LC709203F_ADDR;
        printf("[battery] LC709203F found at 0x%02X\n", fuel_gauge_addr);
    } else {
        printf("[battery] No fuel gauge found, defaulting to 100%%\n");
        return;
    }

    initialized = true;

    // Read initial level
    uint8_t pct = read_battery_percent();
    battery_service_server_set_battery_value(pct);
    printf("[battery] Initial level: %d%%\n", pct);

    // Start background task
    xTaskCreate(battery_task_fn, "battery", BATTERY_TASK_STACK_SIZE,
                NULL, BATTERY_TASK_PRIORITY, NULL);
}
