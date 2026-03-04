// ble_output.h - BLE Gamepad Output Interface (HOGP Peripheral)
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Outputs gamepad data as a BLE HID peripheral using BTstack's hids_device API.
// Appears as a wireless gamepad to PCs, phones, and consoles.

#ifndef BLE_OUTPUT_H
#define BLE_OUTPUT_H

#include "core/output_interface.h"

extern const OutputInterface ble_output_interface;

void ble_output_init(void);
void ble_output_task(void);

#endif // BLE_OUTPUT_H
