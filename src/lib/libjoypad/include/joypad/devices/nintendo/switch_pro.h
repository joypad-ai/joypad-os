// joypad/devices/nintendo/switch_pro.h
// Nintendo Switch Pro Controller and Joy-Con (over USB Charging Grip) — pure
// input parser. Pure functions; no transport, no state machine.
//
// Caller is responsible for:
//   - the handshake / baud / enable_usb / vibration_enable subcommand dance
//     before the controller starts emitting 0x30 reports
//   - per-stick calibration (libjoypad uses default center 2048)
//   - feedback (rumble + player LED) — joypad_build_nintendo_switch_pro_*
//     coming in a later phase

#ifndef JOYPAD_DEVICES_NINTENDO_SWITCH_PRO_H
#define JOYPAD_DEVICES_NINTENDO_SWITCH_PRO_H

#include <stdint.h>
#include <stdbool.h>
#include <joypad/input_event.h>
#include <joypad/capabilities.h>

#ifdef __cplusplus
extern "C" {
#endif

// ----------------------------------------------------------------------------
// VID/PID
// ----------------------------------------------------------------------------

// Matches:
//   0x057e:0x2009 Nintendo Switch Pro Controller (USB)
//   0x057e:0x200e Joy-Con Charging Grip (USB; presents one HID per Joy-Con)
//   0x057e:0x2017 SNES Controller for Nintendo Switch Online
bool joypad_is_nintendo_switch_pro(uint16_t vid, uint16_t pid);

// ----------------------------------------------------------------------------
// Capabilities
// ----------------------------------------------------------------------------

void joypad_nintendo_switch_pro_caps(joypad_caps_t* out);

// ----------------------------------------------------------------------------
// Input report IDs handled by joypad_parse_nintendo_switch_pro
// ----------------------------------------------------------------------------

// Standard full-mode input report (continuously streamed once full_report_mode
// has been enabled via 0x03 subcommand).
#define JOYPAD_NINTENDO_SWITCH_PRO_INPUT_REPORT_ID  0x30

// Subcommand reply — carries the same input data prefix as 0x30 plus the
// subcommand reply payload. We treat it as input too because after sending
// player-LED or vibration-enable subcommands the controller emits 0x21 for
// a few frames and we must not ignore that input.
#define JOYPAD_NINTENDO_SWITCH_PRO_REPLY_REPORT_ID  0x21

// Default 12-bit stick center when no per-controller calibration has run.
#define JOYPAD_NINTENDO_SWITCH_PRO_DEFAULT_STICK_CENTER  2048

// ----------------------------------------------------------------------------
// Parser
// ----------------------------------------------------------------------------

// Parse a full Switch Pro / Joy-Con USB input report (including the report ID
// byte) into an input_event_t.
//
// Returns true if the report ID was 0x30 or 0x21 and the report was long
// enough for the input prefix. On success `*out` is fully populated:
//   - type / transport / layout (LAYOUT_NINTENDO_4FACE) / button_count
//   - buttons (canonical JP_BUTTON_*); B→B1, A→B2, Y→B3, X→B4 mapping
//   - analog[0..3] (LX/LY/RX/RY) scaled to 8-bit assuming center=2048;
//     Y axes inverted so 0=up, 255=down (HID convention)
//   - battery_level + battery_charging
//   - keys = 0, motion = false, touch = false
//
// `*out` is left untouched on failure.
bool joypad_parse_nintendo_switch_pro(const uint8_t* report, uint16_t len, input_event_t* out);

#ifdef __cplusplus
}
#endif

#endif // JOYPAD_DEVICES_NINTENDO_SWITCH_PRO_H
