// bt_output.h - Bluetooth HID Output Interface (BLE + Classic)
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Unified wireless output. One mode is selected at a time — either a
// BLE HOGP peripheral profile (Standard composite, Xbox, etc.) or a
// Classic BT HID-device profile (Wiimote today; DS3/DS4/etc. later).
// Only one of BLE or Classic can be active at once. Platforms without
// a Classic stack (ESP32-S3, nRF52840) report Classic modes as
// unsupported via bt_output_is_mode_supported().

#ifndef BT_OUTPUT_H
#define BT_OUTPUT_H

#include "core/output_interface.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// OUTPUT MODES
// ============================================================================
// Grouped by transport for readability. BLE modes are available on every
// platform with a BLE stack; Classic modes require BTstack Classic
// (signalled at build-time by BTSTACK_HAS_CLASSIC=1).

typedef enum {
    // BLE (HOGP) modes
    BT_MODE_BLE_STANDARD = 0,   // Composite: gamepad + keyboard + mouse
    BT_MODE_BLE_XBOX,           // Xbox BLE gamepad

    // Classic BT (L2CAP HID device) modes
    BT_MODE_CLASSIC_WIIMOTE,    // Nintendo RVL-CNT-01 emulation

    BT_MODE_COUNT
} bt_output_mode_t;

// ============================================================================
// PUBLIC API
// ============================================================================

extern const OutputInterface bt_output_interface;

void bt_output_init(void);
void bt_output_late_init(void);
void bt_output_task(void);

// Connection state
bool bt_output_is_connected(void);

// Mode selection. get_next_mode() skips modes that fail is_mode_supported()
// on the current platform, so cycling via a button always lands on a
// valid mode. set_mode() is a no-op + log for unsupported modes.
bt_output_mode_t bt_output_get_mode(void);
void bt_output_set_mode(bt_output_mode_t mode);
bt_output_mode_t bt_output_get_next_mode(void);
const char* bt_output_get_mode_name(bt_output_mode_t mode);
void bt_output_get_mode_color(bt_output_mode_t mode, uint8_t *r, uint8_t *g, uint8_t *b);

// True if `mode` is supported by the firmware / platform we're running on.
// BLE modes: always true. Classic modes: only when BTSTACK_HAS_CLASSIC is
// defined at build time.
bool bt_output_is_mode_supported(bt_output_mode_t mode);

#endif // BT_OUTPUT_H
