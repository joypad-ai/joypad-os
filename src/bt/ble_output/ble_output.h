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
#include <stdbool.h>
#include "bluetooth.h"   // bd_addr_t

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
    // Native Switch 2 Pro Controller (proprietary GATT + 0x15 pairing), BLE. Needs a
    // radio whose link layer accepts the console's 5 ms connection interval, so it is
    // only compiled where CONFIG_SWITCH2_BLE_OUTPUT is set.
    BLE_MODE_SWITCH2,
    // Dedicated keyboard + mouse, no gamepad collection. Hosts (macOS in
    // particular) never dispatch typed input from a gamepad-primary HID device,
    // and claiming the device as a controller is wrong when it is being used as
    // a keyboard/pointer. Appended last so persisted mode indices stay stable.
    BLE_MODE_KBM,
    // Dedicated keyboard only / mouse only. Same reasoning as KBM, for hosts (or
    // uses) where even a pointer or a keyboard alongside is unwanted. Appended
    // last so persisted mode indices stay stable.
    BLE_MODE_KBD,
    BLE_MODE_MOUSE,
    BLE_MODE_COUNT
} ble_output_mode_t;

// Per-mode BLE identity.
//
// Hosts cache a bonded peer's GATT database against its ADDRESS. Every mode used
// to advertise from the same address, so switching modes handed the host a new
// report map under an identity it already had cached -- the stale-descriptor
// headache, whose only workaround was "Forget This Device" and a fresh pair.
//
// Giving each mode its own address makes them separate peers: one bond per mode,
// each permanent. The address is DERIVED deterministically from the board id and
// the nonce below, never stored and never rotated, so a mode's address is the same
// across reboots, reflashes and mode switches -- its bond keeps working forever.
//
// NEVER renumber or reuse a nonce: it invalidates that mode's bond on every host
// the device has ever paired with. New modes append a new value. Deliberately NOT
// the ble_output_mode_t index, which is only append-only by convention -- an
// insertion there would silently shift every address at once.
#define BLE_IDENT_STANDARD  0x01
#define BLE_IDENT_XBOX      0x02
#define BLE_IDENT_SINPUT    0x03
#define BLE_IDENT_KBM       0x04
#define BLE_IDENT_KBD       0x05
#define BLE_IDENT_MOUSE     0x06
// Switch 2 is excluded: the console bonds to a PUBLIC Nintendo address which must
// not move (see switch2_ble_get_public_addr), and Switch-BT is Classic.

// Fills a deterministic random-STATIC address for this mode. Returns false for
// modes that own their identity by other means (Switch 2, Switch-BT), which the
// caller must leave alone.
bool ble_output_get_mode_addr(ble_output_mode_t mode, bd_addr_t out);

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
#ifndef CONFIG_SWITCH2_BLE_OUTPUT
    if (m == BLE_MODE_SWITCH2) return false;
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

// True once this device is actually presenting itself to a host/console over BLE or
// BT Classic (advertising or connectable). Used by the LED policy: a wireless device
// role owns the colour, while blink carries whether that link is connected yet.
bool ble_output_role_is_device(void);

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
