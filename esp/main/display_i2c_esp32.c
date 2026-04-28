// display_i2c_esp32.c - I2C display transport for ESP32-S3
//
// Implements display_i2c_init() using ESP-IDF I2C driver via platform HAL.
// Used with the Adafruit SH1107 FeatherWing OLED on Feather ESP32-S3.
//
// Display flush runs in a low-priority FreeRTOS task so I2C
// transfers don't block the main loop.

#include "core/services/display/display.h"
#include "core/services/display/display_transport.h"
#include "platform/platform_i2c.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdio.h>

static platform_i2c_t i2c_bus = NULL;
static uint8_t i2c_addr = 0x3C;

static void i2c_write_cmd(uint8_t cmd)
{
    uint8_t buf[2] = { 0x00, cmd };  // Co=0, D/C#=0 (command)
    platform_i2c_write(i2c_bus, i2c_addr, buf, 2);
}

static void i2c_write_data(const uint8_t* data, size_t len)
{
    // I2C data write: control byte 0x40 followed by data
    static uint8_t buf[129];  // 1 control + 128 data max
    buf[0] = 0x40;  // Co=0, D/C#=1 (data)
    size_t chunk = (len > 128) ? 128 : len;
    memcpy(buf + 1, data, chunk);
    platform_i2c_write(i2c_bus, i2c_addr, buf, chunk + 1);
}

// ============================================================================
// BACKGROUND FLUSH TASK
// ============================================================================

#define DISPLAY_TASK_STACK_SIZE 2048
#define DISPLAY_TASK_PRIORITY  2    // Low priority
#define DISPLAY_FLUSH_INTERVAL_MS 50  // 20fps

static void display_task_fn(void *arg)
{
    (void)arg;
    while (1) {
        if (display_is_dirty()) {
            display_flush();
        }
        vTaskDelay(pdMS_TO_TICKS(DISPLAY_FLUSH_INTERVAL_MS));
    }
}

static void display_start_task(void)
{
    display_set_async(true);
    xTaskCreate(display_task_fn, "display", DISPLAY_TASK_STACK_SIZE,
                NULL, DISPLAY_TASK_PRIORITY, NULL);
    printf("[display] Background flush task started\n");
}

// ============================================================================
// TRANSPORT INIT
// ============================================================================

// Feather ESP32-S3 I2C pins
#define I2C_SDA_PIN  3
#define I2C_SCL_PIN  4

bool display_i2c_init(const display_i2c_config_t* config)
{
    // Initialize I2C bus via platform HAL
    platform_i2c_config_t i2c_cfg = {
        .bus = 0,
        .sda_pin = I2C_SDA_PIN,
        .scl_pin = I2C_SCL_PIN,
        .freq_hz = 400000,
    };
    i2c_bus = platform_i2c_init(&i2c_cfg);
    if (!i2c_bus) {
        printf("[display] I2C bus init failed\n");
        return false;
    }

    // Probe: check if a display is actually connected at this address
    uint8_t probe = 0x00;
    int ret = platform_i2c_write(i2c_bus, config->addr, &probe, 1);
    if (ret) {
        printf("[display] No I2C device at 0x%02X\n", config->addr);
        i2c_bus = NULL;
        return false;
    }

    i2c_addr = config->addr;

    // Register transport callbacks
    display_set_transport(i2c_write_cmd, i2c_write_data);
    display_set_col_offset(0);

    // Start background task for async I2C flush
    display_start_task();

    printf("[display] I2C transport initialized (addr=0x%02X)\n", i2c_addr);
    return true;
}
