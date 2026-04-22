// wiimote_runtime.h - Wiimote mode runtime (state + report pump)
// SPDX-License-Identifier: Apache-2.0
//
// Runtime state and report-generation logic for bt_output's
// BT_MODE_CLASSIC_WIIMOTE mode. Called from bt_output.c's mode dispatch —
// not an OutputInterface of its own. BTstack glue lives next door in
// wiimote_sdp.{c,h}; this module owns the Wiimote state machine.

#ifndef WIIMOTE_RUNTIME_H
#define WIIMOTE_RUNTIME_H

#include <stdint.h>
#include <stdbool.h>
#include "core/input_event.h"

// Extension kinds surfaced to callers (apps that cycle Nunchuk/Classic
// via a button). Internally mapped to wm_ext_kind_t from wiimote_ext.h.
typedef enum {
    WIIMOTE_EXT_NONE = 0,
    WIIMOTE_EXT_NUNCHUK,
    WIIMOTE_EXT_CLASSIC,
    WIIMOTE_EXT_CLASSIC_PRO,
    WIIMOTE_EXT_COUNT,
} wiimote_ext_mode_t;

// Initialise state + register the BTstack HID device and SDP records.
// Call once at bt_output_late_init() time, only when mode = CLASSIC_WIIMOTE.
void wiimote_runtime_init(void);

// Periodic task — polls router_get_output(OUTPUT_TARGET_BT, 0) and pumps
// the currently-selected Wiimote report to the Wii.
void wiimote_runtime_task(void);

// Called from wiimote_sdp's set-report callback when the Wii writes a HID
// output report to us (0x11-0x19). Decodes and replies as needed.
void wiimote_runtime_handle_output_report(const uint8_t* report, uint16_t len);

// Runtime extension hot-plug — fires the 0x20 status handshake.
void wiimote_runtime_set_ext(wiimote_ext_mode_t mode);
wiimote_ext_mode_t wiimote_runtime_get_ext(void);

// True while the L2CAP HID channels to the Wii are up.
bool wiimote_runtime_is_connected(void);

#endif // WIIMOTE_RUNTIME_H
