// bt_output.h - Bluetooth Classic HID Device Output
// SPDX-License-Identifier: Apache-2.0
//
// Mirror of ble_output but for Bluetooth Classic HID-device role. Currently
// hosts the Wiimote emulator (bt_output_wiimote), which advertises as
// "Nintendo RVL-CNT-01" with a custom SDP profile and PIN-code pairing
// handshake (reversed Wii BD_ADDR trick).
//
// Usage (app layer):
//   - Declare &bt_output_wiimote_interface in app_get_output_interfaces()
//   - router submits input_event_t; bt_output forwards to the Wiimote report
//     generator; BTstack ships HID INPUT reports over L2CAP
//
// This header intentionally stays thin — per-emulator details live in the
// bt_output_<emulator>.h files.

#ifndef BT_OUTPUT_H
#define BT_OUTPUT_H

#include "core/output_interface.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// SHARED API
// ============================================================================

// Initialise the BTstack Classic HID device stack and whatever emulator is
// currently selected. Called from app_init before btstack_host_init.
void bt_output_init(void);

// Periodic task — flush pending reports, drive reconnect logic, etc.
void bt_output_task(void);

// True when L2CAP HID channels are connected to the target (Wii, etc.).
bool bt_output_is_connected(void);

#endif // BT_OUTPUT_H
