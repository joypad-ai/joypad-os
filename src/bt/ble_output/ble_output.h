// ble_output.h - BLE HID Output Interface (HOGP Peripheral)
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Composite BLE HID output: gamepad + keyboard + mouse.
// Appears as a wireless HID peripheral to PCs, phones, and consoles.

#ifndef BLE_OUTPUT_H
#define BLE_OUTPUT_H

#include "core/output_interface.h"
#include <stdint.h>

// ============================================================================
// OUTPUT MODES
// ============================================================================

typedef enum {
    BLE_MODE_STANDARD = 0,  // Composite: gamepad + keyboard + mouse
    BLE_MODE_XBOX,          // Xbox BLE gamepad (future Phase 2)
    BLE_MODE_COUNT
} ble_output_mode_t;

// ============================================================================
// PUBLIC API
// ============================================================================

extern const OutputInterface ble_output_interface;

void ble_output_init(void);
void ble_output_late_init(void);
void ble_output_task(void);

// Mode selection
ble_output_mode_t ble_output_get_mode(void);
void ble_output_set_mode(ble_output_mode_t mode);
ble_output_mode_t ble_output_get_next_mode(void);
const char* ble_output_get_mode_name(ble_output_mode_t mode);
void ble_output_get_mode_color(ble_output_mode_t mode, uint8_t *r, uint8_t *g, uint8_t *b);

#endif // BLE_OUTPUT_H
