// pmu_esp32.c - SY6970/BQ25896 charger PMU on the LilyGo T-Display S3 AMOLED
// Plus (I2C: SDA=IO3, SCL=IO2). Provides battery telemetry (voltage, charge
// state, VBUS) and configures sane charging for a small LiPo: ~320mA charge
// current instead of the chip's 2A power-on default, watchdog off so the
// settings stick. Only built for the LilyGo board.
#ifdef BOARD_LILYGO_TDISPLAY_S3_AMOLED

#include <stdint.h>
#include <stdbool.h>
#include "driver/i2c_master.h"
#include "esp_log.h"

#define PMU_SDA 3
#define PMU_SCL 2
// SY6970 = 0x6A; TI BQ25896 (same register map) = 0x6B
static const uint8_t PMU_ADDRS[] = { 0x6A, 0x6B };

static const char* TAG = "pmu";
static i2c_master_dev_handle_t s_dev = NULL;

static bool reg_read(uint8_t reg, uint8_t* val)
{
    return s_dev &&
           i2c_master_transmit_receive(s_dev, &reg, 1, val, 1, 50) == ESP_OK;
}

static bool reg_write(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return s_dev && i2c_master_transmit(s_dev, buf, 2, 50) == ESP_OK;
}

bool pmu_init(void)
{
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = -1,
        .sda_io_num = PMU_SDA,
        .scl_io_num = PMU_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    if (i2c_new_master_bus(&bus_cfg, &bus) != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus init failed");
        return false;
    }
    for (size_t i = 0; i < sizeof(PMU_ADDRS); i++) {
        if (i2c_master_probe(bus, PMU_ADDRS[i], 50) == ESP_OK) {
            i2c_device_config_t dev_cfg = {
                .dev_addr_length = I2C_ADDR_BIT_LEN_7,
                .device_address = PMU_ADDRS[i],
                .scl_speed_hz = 100000,
            };
            if (i2c_master_bus_add_device(bus, &dev_cfg, &s_dev) == ESP_OK) {
                ESP_LOGI(TAG, "charger PMU at 0x%02X", PMU_ADDRS[i]);
                break;
            }
        }
    }
    if (!s_dev) {
        ESP_LOGW(TAG, "no charger PMU found");
        return false;
    }
    // ADC continuous conversion (REG02: CONV_START|CONV_RATE)
    uint8_t r02;
    if (reg_read(0x02, &r02)) reg_write(0x02, r02 | 0xC0);
    // Watchdog off (REG07 WATCHDOG[5:4]=00) so charge config persists
    uint8_t r07;
    if (reg_read(0x07, &r07)) reg_write(0x07, r07 & ~0x30);
    // Charge current ~320mA (REG04 ICHG[6:0], 64mA/LSB) — small-LiPo safe,
    // replacing the 2048mA power-on default
    uint8_t r04;
    if (reg_read(0x04, &r04)) reg_write(0x04, (r04 & 0x80) | 5);
    return true;
}

// Battery voltage in mV (0 if unavailable). REG0E BATV: 2304mV + 20mV/LSB.
int pmu_batt_mv(void)
{
    uint8_t v;
    if (!reg_read(0x0E, &v)) return 0;
    return 2304 + (v & 0x7F) * 20;
}

// VBUS voltage in mV (0 if unavailable). REG11 VBUSV: 2600mV + 100mV/LSB.
int pmu_vbus_mv(void)
{
    uint8_t v;
    if (!reg_read(0x11, &v)) return 0;
    return 2600 + (v & 0x7F) * 100;
}

// Charge status: 0=not charging, 1=pre-charge, 2=fast charge, 3=done.
int pmu_charge_state(void)
{
    uint8_t v;
    if (!reg_read(0x0B, &v)) return 0;
    return (v >> 3) & 0x3;
}

// Charge current in mA (REG12 ICHGR: 50mA/LSB).
int pmu_charge_ma(void)
{
    uint8_t v;
    if (!reg_read(0x12, &v)) return 0;
    return (v & 0x7F) * 50;
}

#endif // BOARD_LILYGO_TDISPLAY_S3_AMOLED
