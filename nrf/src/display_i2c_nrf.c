// display_i2c_nrf.c - I2C display transport for nRF52840 boards
//
// Implements display_i2c_init() using Zephyr I2C driver.
// XIAO nRF52840:   SSD1306 on i2c1 (XIAO Expansion Board)
// Feather nRF52840: SH1107 on i2c0 (FeatherWing OLED)
//
// Display flush runs in a low-priority background thread so I2C
// transfers (~35ms at 400kHz) don't block the main loop.

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
    size_t chunk = (len > (sizeof(buf) - 1)) ? (sizeof(buf) - 1) : len;
    memcpy(buf + 1, data, chunk);
    i2c_write(i2c_dev, buf, chunk + 1, i2c_addr);
}

// ============================================================================
// BACKGROUND FLUSH THREAD
// ============================================================================

#define DISPLAY_THREAD_STACK_SIZE 1024
#define DISPLAY_THREAD_PRIORITY  K_PRIO_PREEMPT(14)  // Low priority
#define DISPLAY_FLUSH_INTERVAL_MS 50  // 20fps

K_THREAD_STACK_DEFINE(display_stack, DISPLAY_THREAD_STACK_SIZE);
static struct k_thread display_thread;

static void display_thread_fn(void *p1, void *p2, void *p3)
{
    (void)p1; (void)p2; (void)p3;
    while (1) {
        if (display_is_dirty()) {
            display_flush();
        }
        k_msleep(DISPLAY_FLUSH_INTERVAL_MS);
    }
}

static void display_start_thread(void)
{
    display_set_async(true);
    k_thread_create(&display_thread, display_stack,
                     DISPLAY_THREAD_STACK_SIZE,
                     display_thread_fn, NULL, NULL, NULL,
                     DISPLAY_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&display_thread, "display");
    printf("[display] Background flush thread started\n");
}

// ============================================================================
// TRANSPORT INIT
// ============================================================================

void display_i2c_init(const display_i2c_config_t* config)
{
#ifdef BOARD_FEATHER_NRF52840
    // Feather I2C bus: i2c0 (SDA=P0.12, SCL=P0.11)
    i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));
#else
    // XIAO I2C bus: i2c1 (SDA=P0.04, SCL=P0.05)
    i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c1));
#endif
    if (!device_is_ready(i2c_dev)) {
        printf("[display] I2C device not ready\n");
        return;
    }

    // Probe: check if a display is actually connected at this address
    uint8_t probe = 0x00;
    int ret = i2c_write(i2c_dev, &probe, 1, config->addr);
    if (ret) {
        printf("[display] No I2C device at 0x%02X\n", config->addr);
        i2c_dev = NULL;
        return;
    }

    i2c_addr = config->addr;

    // Register transport callbacks
    display_set_transport(i2c_write_cmd, i2c_write_data);
    display_set_col_offset(0);

    // Start background thread for async I2C flush
    display_start_thread();

    printf("[display] I2C transport initialized (addr=0x%02X)\n", i2c_addr);
}
