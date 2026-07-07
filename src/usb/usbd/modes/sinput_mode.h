// sinput_mode.h - Transport-neutral SInput report helpers
// SPDX-License-Identifier: Apache-2.0
//
// The SInput report/feature/output logic lives in sinput_mode.c (the USB output
// mode). These entry points expose it so the BLE SInput device mode
// (ble_output.c) can reuse the exact same builders — one canonical SInput
// implementation across USB and BLE. Shared device-info/feature state is safe
// because only one transport is the active SInput output at any time.

#ifndef SINPUT_MODE_H
#define SINPUT_MODE_H

#include <stdint.h>
#include <stdbool.h>

// Reuse the transport-neutral SInput report structs/descriptor without pulling
// in tusb.h (the USB device descriptor) — TinyUSB's hid.h clashes with BTstack's
// btstack_hid.h in the BLE build.
#define SINPUT_DESCRIPTORS_NO_USB
#include "usb/usbd/descriptors/sinput_descriptors.h"  // sinput_report_t
#include "core/input_event.h"                          // input_event_t

// Build the 64-byte input report (ID 1) from a router output event.
void sinput_report_build_from_event(sinput_report_t* out, const input_event_t* event);

// Tilt steering (roll → left stick X). Pass -1 to leave a field unchanged;
// sign: +1/-1 to set, 0 = leave. Tunable live over CDC/NUS (TILT.STEER command).
void sinput_set_tilt_steer(int on, int range_deg, int dead_deg, int sign);

// Build the 63-byte feature-response payload (ID 2). Returns the length (63).
uint16_t sinput_build_feature_response(uint8_t feature_response[63]);

// If a feature refresh is pending, fill out[63]/len and clear the flag; else
// return false. (A device change during report build sets the pending flag.)
bool sinput_feature_response_take(uint8_t out[63], uint16_t* len);

// Feed a received output report (ID 3: haptic / player LED / RGB / features).
void sinput_output_received(const uint8_t* data, uint16_t len);

// Rumble amplitudes from the last haptic output report.
void sinput_get_rumble_lr(uint8_t* left, uint8_t* right);

// Diagnostic: feature responses built since boot (see sinput_mode.c).
uint32_t sinput_get_feature_count(void);

#endif // SINPUT_MODE_H
