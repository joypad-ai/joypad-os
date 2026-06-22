// keymap.h - Configurable HID keyboard-key -> JP_BUTTON_* mapping.
//
// Lets a keyboard (USB or BT HID), or a MouthPad running a key-emitting
// `.mkprofile`, drive the gamepad button bitmap so it flows through the normal
// router / profile remap pipeline and out any gamepad output (SInput, consoles).
//
// The MouthPad's firmware FSM maps touch sectors / sip / puff / swipes to HID
// keystrokes (configured host-side via `.mkprofile`, no firmware change). This
// helper turns those keystrokes into JP_BUTTON_* bits — the "Profile Bridge":
// MouthPad does touch->HID, JoypadOS does HID->gamepad. No private telemetry.
//
// Pure data-in / bits-out; no platform deps. The active map is overridable so it
// can be made user-configurable (CDC / profile) without code changes.

#ifndef KEYMAP_H
#define KEYMAP_H

#include <stdint.h>

typedef struct {
    uint8_t  keycode;   // HID usage (Page 0x07), e.g. HID_KEY_ARROW_UP
    uint32_t button;    // JP_BUTTON_* bit(s) to set when that key is down
} keymap_entry_t;

// Convert up to `nkeys` pressed HID keycodes (+ modifier byte) into an OR'd
// JP_BUTTON_* bitmap using the active map. Returns 0 if nothing maps.
uint32_t keymap_keys_to_buttons(const uint8_t* keys, uint8_t nkeys, uint8_t modifier);

// Override the active key->button map. Pass (NULL, 0) to restore the built-in
// default. The array must remain valid for as long as it is active.
void keymap_set(const keymap_entry_t* map, uint8_t count);

#endif // KEYMAP_H
