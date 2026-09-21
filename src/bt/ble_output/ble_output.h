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
    BLE_MODE_SINPUT,        // SInput gamepad (SDL/Steam: buttons + IMU + battery)
    // NOTE: this is a Bluetooth *Classic* (BR/EDR) HID-device mode, not BLE — the
    // Switch only pairs controllers over Classic. It shares this selector because
    // it's the app's single "wireless output mode" list. Only builds with a
    // Classic-capable radio (CYW43) compile the implementation; see
    // ble_output_mode_available(). Kept last so mode indices stay stable across
    // builds (the selected mode is persisted to flash by index).
    BLE_MODE_SWITCH_BT,     // Nintendo Switch Pro Controller over BT Classic
    BLE_MODE_COUNT
} ble_output_mode_t;

// True if this wireless output mode is compiled into the current build. Modes
// that need a Bluetooth Classic radio are absent on BLE-only targets (ESP32-S3,
// nRF52840), so the mode-list/select plumbing hides + rejects them there. Used by
// both ble_output.c and the CDC mode commands so web config only ever offers
// modes this build can actually run.
static inline bool ble_output_mode_available(ble_output_mode_t m)
{
    if ((int)m < 0 || m >= BLE_MODE_COUNT) return false;
#ifndef CONFIG_BT_CLASSIC_OUTPUT
    if (m == BLE_MODE_SWITCH_BT) return false;   // no Classic radio on this build
#endif
#ifndef CONFIG_BLE_STANDARD_MODE
    // SInput BLE (composite: SInput gamepad + kbd + mouse) supersedes the
    // legacy Standard composite. The code stays for custom branches; define
    // CONFIG_BLE_STANDARD_MODE to re-enable it in the selector.
    if (m == BLE_MODE_STANDARD) return false;
#endif
    return true;
}

// ============================================================================
// PUBLIC API
// ============================================================================

extern const OutputInterface ble_output_interface;

void ble_output_init(void);
void ble_output_late_init(void);
void ble_output_task(void);

// Connection state
bool ble_output_is_connected(void);

// Set the GPIO (raw chip pin) to wake from deep sleep on, plus its pressed
// level (active_high). When set (>=0), a deliberate host disconnect (not a
// dropped link) powers the device down instead of re-advertising; a press on
// this pin wakes/reboots it. <0 disables.
void ble_output_set_sleep_wake_pin(int gpio, bool active_high);

// Mode selection
ble_output_mode_t ble_output_get_mode(void);
void ble_output_set_mode(ble_output_mode_t mode);
ble_output_mode_t ble_output_get_next_mode(void);
const char* ble_output_get_mode_name(ble_output_mode_t mode);
void ble_output_get_mode_color(ble_output_mode_t mode, uint8_t *r, uint8_t *g, uint8_t *b);

#endif // BLE_OUTPUT_H
