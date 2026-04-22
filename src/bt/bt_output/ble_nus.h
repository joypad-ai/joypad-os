// ble_nus.h - BLE Nordic UART Service (NUS) bridge for CDC protocol
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Bridges the CDC binary protocol over BLE NUS, enabling wireless
// configuration using the same command set as USB CDC.

#ifndef BLE_NUS_H
#define BLE_NUS_H

#include <stdint.h>

// Initialize NUS service and protocol bridge.
// Must be called after att_server_init() with the standard GATT profile
// (which includes nordic_spp_service.gatt).
void ble_nus_init(void);

// Process any pending NUS protocol work (call from main loop)
void ble_nus_task(void);

#endif // BLE_NUS_H
