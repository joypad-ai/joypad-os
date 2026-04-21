// wiimote_sdp.h - BTstack Classic HID + SDP setup for Wiimote emulation
// SPDX-License-Identifier: Apache-2.0
//
// Registers the HID device record with BTstack, configures the Wii-specific
// GAP parameters that make a Wii accept us as a genuine RVL-CNT-01, and
// wires the PIN-code handshake that uses the reversed Wii BD_ADDR as the
// PIN. Callers interact through bt_output_wiimote — this header is
// internal to the bt_output_wiimote module.

#ifndef WIIMOTE_SDP_H
#define WIIMOTE_SDP_H

#include <stdint.h>
#include <stdbool.h>

// Forward declare BTstack's bd_addr_t to avoid pulling btstack headers into
// users of this header.
typedef uint8_t bd_addr_t[6];

// Register SDP records, HID device, GAP parameters, and event handlers.
// Call once, after l2cap_init / sdp_init but before hci_power_control().
// Must be called on the BTstack thread.
void wiimote_sdp_register(void);

// Store the bonded Wii's BD_ADDR. The PIN handler reverses this address
// to answer PIN_CODE_REQUEST events — the classic Wiimote pairing trick.
// Setting to all-zero disables the PIN response (use this before pairing
// so the Wii's Sync button flow is used instead).
void wiimote_sdp_set_wii_addr(const bd_addr_t addr);

// Fetch the currently stored Wii BD_ADDR. Returns false if none set.
bool wiimote_sdp_get_wii_addr(bd_addr_t out);

// Attempt an outgoing HID connect to the stored Wii BD_ADDR. Returns the
// BTstack status code (ERROR_CODE_SUCCESS on success). Safe to call
// repeatedly — will no-op if no Wii is bonded or if already connected.
uint8_t wiimote_sdp_reconnect(void);

// Disconnect any active HID session.
void wiimote_sdp_disconnect(void);

// Queue a Wiimote HID input report (LSB byte is the report ID). The SDP
// module owns the BTstack HID CID and pushes via
// hid_device_send_interrupt_message once the channel is ready.
// Returns true if queued, false if not connected / not ready.
bool wiimote_sdp_send_report(const uint8_t* report, uint16_t len);

// Connection state for the bt_output_wiimote layer.
bool wiimote_sdp_is_connected(void);

#endif // WIIMOTE_SDP_H
