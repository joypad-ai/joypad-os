// joypad/devices/sony/psc.h
// Sony PlayStation Classic controller (and 8BitDo USB Adapter for PS Classic)
// — pure input parser.
//
// Smallest Sony driver: 8 face/shoulder buttons, d-pad, no sticks, no triggers,
// no rumble, no LEDs. Useful for verifying the libjoypad pattern with a
// minimum-surface device.

#ifndef JOYPAD_DEVICES_SONY_PSC_H
#define JOYPAD_DEVICES_SONY_PSC_H

#include <stdint.h>
#include <stdbool.h>
#include <joypad/input_event.h>
#include <joypad/capabilities.h>

#ifdef __cplusplus
extern "C" {
#endif

bool joypad_is_sony_psc(uint16_t vid, uint16_t pid);
void joypad_sony_psc_caps(joypad_caps_t* out);

// PSC reports are 3 bytes with no report ID prefix. `len` must be >= 3.
// On success, fills the canonical input_event_t:
//   - type = INPUT_TYPE_GAMEPAD, transport = INPUT_TRANSPORT_USB
//   - layout = LAYOUT_MODERN_4FACE, button_count = 8
//   - buttons (JP_BUTTON_*) for face, shoulder, system, and d-pad
//   - analog axes left at centered defaults (no sticks on PSC)
bool joypad_parse_sony_psc(const uint8_t* report, uint16_t len, input_event_t* out);

#ifdef __cplusplus
}
#endif

#endif // JOYPAD_DEVICES_SONY_PSC_H
