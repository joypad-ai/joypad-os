// imu_nrf.c - Onboard IMU reader for the XIAO nRF52840 Sense (LSM6DS3TR-C)
//
// Direct I2C register driver (NOT the Zephyr sensor driver, whose boot-time
// init proved fragile). Runs at runtime, after the regulator has powered the
// IMU. Reads accel + gyro and stamps motion into the router, from where
// sinput_mode emits it in the SInput IMU report. No-op if no IMU node.

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <stdio.h>

#include "imu_nrf.h"

#if DT_NODE_EXISTS(DT_NODELABEL(lsm6ds3tr_c))

#include <zephyr/drivers/i2c.h>
#include <hal/nrf_gpio.h>
#include "core/router/router.h"
#include "platform/platform.h"

#define IMU_PWR_PIN (32 + 8)  // P1.08 = LSM6DS3TR-C power enable (active high)

// LSM6DS3TR-C registers.
#define IMU_ADDR        0x6a
#define REG_WHO_AM_I    0x0F
#define REG_CTRL1_XL    0x10   // accel ODR + full-scale
#define REG_CTRL2_G     0x11   // gyro  ODR + full-scale
#define REG_CTRL3_C     0x12   // BDU / auto-increment
#define REG_OUTX_L_G    0x22   // gyro X,Y,Z then accel X,Y,Z (12 bytes, auto-inc)
#define WHOAMI_VALUE    0x6A

// The raw 16-bit output at these full-scales already matches the int16 range
// SInput expects, so no conversion is needed — just hand it over with the range.
#define IMU_ACCEL_RANGE_MG  4000    // ±4 g  → CTRL1_XL FS_XL = 0b10
#define IMU_GYRO_RANGE_DPS  2000    // ±2000 dps → CTRL2_G FS_G = 0b11
#define CTRL1_XL_VAL  0x48          // ODR 104Hz (0100), ±4g (10)
#define CTRL2_G_VAL   0x4C          // ODR 104Hz (0100), ±2000dps (11)
#define CTRL3_C_VAL   0x44          // BDU=1, IF_INC=1
#define IMU_POLL_MS   10            // ~100 Hz

static const struct device *i2c_dev;
static uint8_t imu_addr;
static bool imu_ok;

void imu_init(void)
{
    i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));
    if (!device_is_ready(i2c_dev)) {
        printf("[imu] i2c0 not ready\n");
        return;
    }

    // Power the IMU: the regulator-fixed node on P1.08 does NOT reliably hold
    // the rail on, so drive P1.08 high ourselves (high drive), then let the IMU
    // boot. This runs after USB is up, so it can never block enumeration.
    nrf_gpio_cfg(IMU_PWR_PIN, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_DISCONNECT,
                 NRF_GPIO_PIN_NOPULL, NRF_GPIO_PIN_H0H1, NRF_GPIO_PIN_NOSENSE);
    nrf_gpio_pin_set(IMU_PWR_PIN);
    k_msleep(30);

    // Probe both possible LSM6DS3TR-C addresses (SA0 strap → 0x6a or 0x6b).
    uint8_t who = 0;
    if (i2c_reg_read_byte(i2c_dev, 0x6a, REG_WHO_AM_I, &who) == 0 && who == WHOAMI_VALUE) {
        imu_addr = 0x6a;
    } else if (i2c_reg_read_byte(i2c_dev, 0x6b, REG_WHO_AM_I, &who) == 0 && who == WHOAMI_VALUE) {
        imu_addr = 0x6b;
    } else {
        printf("[imu] LSM6DS3TR-C not found (who=0x%02x)\n", who);
        return;
    }

    i2c_reg_write_byte(i2c_dev, imu_addr, REG_CTRL1_XL, CTRL1_XL_VAL);
    i2c_reg_write_byte(i2c_dev, imu_addr, REG_CTRL2_G,  CTRL2_G_VAL);
    i2c_reg_write_byte(i2c_dev, imu_addr, REG_CTRL3_C,  CTRL3_C_VAL);
    imu_ok = true;

    // Mark motion valid (imu_task fills in real values every ~10ms).
    int16_t z[3] = {0};
    router_set_onboard_motion(z, z, IMU_ACCEL_RANGE_MG, IMU_GYRO_RANGE_DPS);
    printf("[imu] LSM6DS3TR-C @0x%02x configured (104 Hz, ±4 g, ±2000 dps)\n", imu_addr);
}

void imu_task(void)
{
    if (!imu_ok) return;

    static uint32_t last_ms = 0;
    uint32_t now = platform_time_ms();
    if (now - last_ms < IMU_POLL_MS) return;
    last_ms = now;

    uint8_t b[12];
    if (i2c_burst_read(i2c_dev, imu_addr, REG_OUTX_L_G, b, sizeof(b)) != 0) return;

    int16_t gyro[3] = {
        (int16_t)(b[0]  | (b[1]  << 8)),
        (int16_t)(b[2]  | (b[3]  << 8)),
        (int16_t)(b[4]  | (b[5]  << 8)),
    };
    int16_t accel[3] = {
        (int16_t)(b[6]  | (b[7]  << 8)),
        (int16_t)(b[8]  | (b[9]  << 8)),
        (int16_t)(b[10] | (b[11] << 8)),
    };
    router_set_onboard_motion(accel, gyro, IMU_ACCEL_RANGE_MG, IMU_GYRO_RANGE_DPS);
}

#else  // no IMU node on this board — no-op

void imu_init(void) {}
void imu_task(void) {}

#endif
