// joypad/devices/sony/ds4.h
// Sony DualShock 4 — pure USB HID parser + feedback builder.
//
// Covers Sony's PS4 DualShock 4 plus the wide set of third-party PS4-mode
// controllers (Hori, Razer, Brook, Mad Catz, Qanba, Nacon, PowerA, Victrix,
// and others). Most expose the same input report layout — that's why the
// VID/PID list is long but the parser is one function.
//
// Compared to DS5: no adaptive triggers, no mic LED, smaller output report.
// Same touchpad (2-finger, 1920×943), same IMU shape, same lightbar.

#ifndef JOYPAD_DEVICES_SONY_DS4_H
#define JOYPAD_DEVICES_SONY_DS4_H

#include <stdint.h>
#include <stdbool.h>
#include <joypad/input_event.h>
#include <joypad/feedback.h>
#include <joypad/capabilities.h>

#ifdef __cplusplus
extern "C" {
#endif

bool joypad_is_sony_ds4(uint16_t vid, uint16_t pid);
void joypad_sony_ds4_caps(joypad_caps_t* out);

#define JOYPAD_SONY_DS4_INPUT_REPORT_ID       0x01
#define JOYPAD_SONY_DS4_FEEDBACK_REPORT_ID    0x05
#define JOYPAD_SONY_DS4_FEEDBACK_PAYLOAD_LEN  31

// Parse a DS4 USB input report (full report including report ID byte).
// Returns true if the report was a DS4 input report (ID 0x01) of sufficient
// length and *out was populated. Returns false otherwise.
//
// On success, fills the canonical input_event_t:
//   - type / transport / layout (MODERN_4FACE) / button_count = 10
//   - buttons (including A2 touchpad click, L4/R4 touchpad halves)
//   - analog[LX,LY,RX,RY,L2,R2] raw HID values, no nonzero clamping
//   - gyro[3], accel[3], has_motion, gyro_range = 2000 dps,
//     accel_range = 4000 milli-g
//   - touch[2] absolute coordinates with active flag, has_touch
//   - battery_level + battery_charging (if report long enough)
bool joypad_parse_sony_ds4(const uint8_t* report, uint16_t len, input_event_t* out);

// Build a DS4 wire-format feedback payload from `state`. Caller sends the
// result as USB output report ID JOYPAD_SONY_DS4_FEEDBACK_REPORT_ID.
// Returns the number of bytes written (always JOYPAD_SONY_DS4_FEEDBACK_PAYLOAD_LEN
// on success, 0 on failure).
uint16_t joypad_build_sony_ds4_feedback(const joypad_feedback_t* state,
                                        uint8_t* out_buf,
                                        uint16_t out_buf_size);

#ifdef __cplusplus
}
#endif

#endif // JOYPAD_DEVICES_SONY_DS4_H
