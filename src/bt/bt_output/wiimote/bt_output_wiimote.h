// bt_output_wiimote.h - Wiimote-over-Bluetooth Classic HID device output
// SPDX-License-Identifier: Apache-2.0
//
// Pretends to be a Nintendo RVL-CNT-01 (Wiimote) over Classic Bluetooth.
// The Pico W (or other supported board) becomes a wireless Wiimote that can
// be sync'd to a real Wii/Wii U.
//
// STATUS: scaffold only — BTstack SDP registration, PIN handshake, report
// generation and extension emulation are TODO. See
// .dev/docs/picogamepad_absorption.md Phase 1 for the work breakdown.

#ifndef BT_OUTPUT_WIIMOTE_H
#define BT_OUTPUT_WIIMOTE_H

#include "core/output_interface.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// EMULATION MODES
// ============================================================================
// Switched at runtime via a button combo (X + Y by convention). The
// extension byte stream is regenerated when the mode changes and the host
// (Wii) reads the attached-extension ID bytes at 0xFA.

typedef enum {
    WIIMOTE_EXT_NONE = 0,       // Core Wiimote (no extension)
    WIIMOTE_EXT_NUNCHUK,        // Wiimote + Nunchuk
    WIIMOTE_EXT_CLASSIC,        // Classic Controller (SNES-like face buttons + 2 sticks)
    WIIMOTE_EXT_CLASSIC_PRO,    // Classic Controller Pro (digital L/R)
    WIIMOTE_EXT_COUNT,
} wiimote_ext_mode_t;

// ============================================================================
// PUBLIC API
// ============================================================================

extern const OutputInterface bt_output_wiimote_interface;

// Runtime emulation-mode switch. Triggers extension hot-plug event
// (Wii sees the extension unplugged+replugged).
void bt_output_wiimote_set_ext(wiimote_ext_mode_t mode);
wiimote_ext_mode_t bt_output_wiimote_get_ext(void);

#endif // BT_OUTPUT_WIIMOTE_H
