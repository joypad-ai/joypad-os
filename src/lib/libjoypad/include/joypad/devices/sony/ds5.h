// joypad/devices/sony/ds5.h
// Sony DualSense (PS5) — USB HID parser + feedback builder.
//
// Pure functions: stateless transformations between raw HID bytes and
// libjoypad's normalized input_event_t / joypad_feedback_t. No platform deps,
// no I/O, no static state.
//
// Caller is responsible for:
//   - transport (TinyUSB / HIDAPI / WebHID / etc.)
//   - dev_addr / instance assignment
//   - submitting events to the consumer's input pipeline
//   - sending the feedback report bytes back to the device

#ifndef JOYPAD_DEVICES_SONY_DS5_H
#define JOYPAD_DEVICES_SONY_DS5_H

#include <stdint.h>
#include <stdbool.h>
#include <joypad/input_event.h>
#include <joypad/feedback.h>
#include <joypad/capabilities.h>

#ifdef __cplusplus
extern "C" {
#endif

// ----------------------------------------------------------------------------
// VID/PID identification
// ----------------------------------------------------------------------------

// Returns true for Sony DualSense and pin-compatible derivatives (Victrix Pro
// FS for PS5).
bool joypad_is_sony_ds5(uint16_t vid, uint16_t pid);

// ----------------------------------------------------------------------------
// Capabilities
// ----------------------------------------------------------------------------

// Populate caps for a DS5-class controller. Identity fields (vendor_id /
// product_id / names) are filled by the caller from the actual device.
void joypad_sony_ds5_caps(joypad_caps_t* out);

// ----------------------------------------------------------------------------
// Input parser
// ----------------------------------------------------------------------------

// USB HID report ID for DualSense input data (state report).
#define JOYPAD_SONY_DS5_INPUT_REPORT_ID  0x01

// Parse a raw DS5 USB HID report into an input_event_t.
//
// `report` must point at the full report including the report ID byte.
// Returns true if the report was a DS5 input report (ID 0x01) of sufficient
// length and `*out` was populated. Returns false otherwise; `*out` is left
// untouched on failure.
//
// On success, fills:
//   - type = INPUT_TYPE_GAMEPAD
//   - transport = INPUT_TRANSPORT_USB
//   - buttons (JP_BUTTON_* bitmap including A2=touchpad-click, A3=mute)
//   - analog[ANALOG_LX..ANALOG_R2] (raw HID values, no nonzero clamping)
//   - gyro[3], accel[3], has_motion, gyro_range, accel_range
//   - touch[2], has_touch (DS5 touchpad coordinates)
//   - battery_level, battery_charging (if report long enough to include them)
//   - layout = LAYOUT_MODERN_4FACE
//   - button_count = 10
//
// Caller is responsible for setting dev_addr / instance and submitting.
bool joypad_parse_sony_ds5(const uint8_t* report, uint16_t len, input_event_t* out);

// ----------------------------------------------------------------------------
// Feedback builder
// ----------------------------------------------------------------------------

// USB HID report ID for DualSense feedback (output report).
#define JOYPAD_SONY_DS5_FEEDBACK_REPORT_ID  0x05

// Size of the DS5 feedback report payload (excluding report ID).
#define JOYPAD_SONY_DS5_FEEDBACK_PAYLOAD_LEN  47

// Build a DS5 USB feedback report payload from `state`. Writes up to
// JOYPAD_SONY_DS5_FEEDBACK_PAYLOAD_LEN bytes to `out_buf`. Returns the
// number of bytes written, or 0 if `out_buf_size` is too small.
//
// The caller sends this as USB output report ID JOYPAD_SONY_DS5_FEEDBACK_REPORT_ID.
//
// Fields not marked dirty in `state` are emitted as no-effect / off so the
// controller's previous state is overwritten predictably. Future revisions
// may add a delta variant that emits only changed fields.
uint16_t joypad_build_sony_ds5_feedback(const joypad_feedback_t* state,
                                        uint8_t* out_buf,
                                        uint16_t out_buf_size);

#ifdef __cplusplus
}
#endif

#endif // JOYPAD_DEVICES_SONY_DS5_H
