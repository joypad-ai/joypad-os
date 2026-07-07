// imu_nrf.h - Onboard IMU reader (XIAO nRF52840 Sense LSM6DS3TR-C)
#ifndef IMU_NRF_H
#define IMU_NRF_H

// Initialize the onboard IMU. No-op if the board has no IMU node.
void imu_init(void);

// Poll the IMU (throttled internally to ~100 Hz) and push motion to the router.
void imu_task(void);

// Cut the IMU power rail (P1.08) before deep sleep so it draws nothing in
// System OFF. Called from platform_deep_sleep().
void imu_power_off(void);

#endif // IMU_NRF_H
