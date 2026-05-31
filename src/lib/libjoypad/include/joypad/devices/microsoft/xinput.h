// joypad/devices/microsoft/xinput.h
// Microsoft XInput (Xbox 360 / Xbox One / Xbox OG) — pure transformation
// from the pre-decoded XInput gamepad state to libjoypad's input_event_t.
//
// XInput is USB-but-not-HID — TinyUSB's xinput_host library and similar
// userspace libraries decode the bus protocol into a gamepad state struct
// (the same shape Microsoft's XINPUT_GAMEPAD has used since 2005). This
// driver does *not* parse raw USB bytes; it takes the decoded state struct
// and produces a canonical input_event_t. That's the cleanest abstraction
// because every XInput consumer (Windows XInput.dll, Linux hid-xpad,
// macOS 360Controller, etc.) already exposes the same struct shape.

#ifndef JOYPAD_DEVICES_MICROSOFT_XINPUT_H
#define JOYPAD_DEVICES_MICROSOFT_XINPUT_H

#include <stdint.h>
#include <stdbool.h>
#include <joypad/input_event.h>
#include <joypad/capabilities.h>

#ifdef __cplusplus
extern "C" {
#endif

// ----------------------------------------------------------------------------
// Controller type
// ----------------------------------------------------------------------------

typedef enum {
    JOYPAD_XINPUT_TYPE_UNKNOWN          = 0,
    JOYPAD_XINPUT_TYPE_XBOX_ONE         = 1,  // GIP protocol over USB / BLE
    JOYPAD_XINPUT_TYPE_XBOX_360_WIRELESS = 2,
    JOYPAD_XINPUT_TYPE_XBOX_360_WIRED   = 3,
    JOYPAD_XINPUT_TYPE_XBOX_OG          = 4,  // Duke / S-controller (per-button analog)
} joypad_xinput_type_t;

// ----------------------------------------------------------------------------
// XInput gamepad state (matches MS XINPUT_GAMEPAD layout + OG extras)
// ----------------------------------------------------------------------------

#define JOYPAD_XINPUT_BTN_DPAD_UP           0x0001
#define JOYPAD_XINPUT_BTN_DPAD_DOWN         0x0002
#define JOYPAD_XINPUT_BTN_DPAD_LEFT         0x0004
#define JOYPAD_XINPUT_BTN_DPAD_RIGHT        0x0008
#define JOYPAD_XINPUT_BTN_START             0x0010
#define JOYPAD_XINPUT_BTN_BACK              0x0020
#define JOYPAD_XINPUT_BTN_LEFT_THUMB        0x0040
#define JOYPAD_XINPUT_BTN_RIGHT_THUMB       0x0080
#define JOYPAD_XINPUT_BTN_LEFT_SHOULDER     0x0100
#define JOYPAD_XINPUT_BTN_RIGHT_SHOULDER    0x0200
#define JOYPAD_XINPUT_BTN_GUIDE             0x0400
#define JOYPAD_XINPUT_BTN_SHARE             0x0800   // Xbox Series X|S share button
#define JOYPAD_XINPUT_BTN_A                 0x1000
#define JOYPAD_XINPUT_BTN_B                 0x2000
#define JOYPAD_XINPUT_BTN_X                 0x4000
#define JOYPAD_XINPUT_BTN_Y                 0x8000

typedef struct {
    uint16_t wButtons;                 // JOYPAD_XINPUT_BTN_* mask
    uint8_t  bLeftTrigger;             // 0..255
    uint8_t  bRightTrigger;            // 0..255
    int16_t  sThumbLX;                 // -32768..32767, positive = right
    int16_t  sThumbLY;                 // -32768..32767, positive = UP (XInput convention)
    int16_t  sThumbRX;
    int16_t  sThumbRY;

    // Xbox OG (Duke / S-controller) extras. Ignored unless type == XBOX_OG.
    uint8_t  pressure_a, pressure_b, pressure_x, pressure_y;
    uint8_t  pressure_black, pressure_white;
} joypad_xinput_gamepad_t;

// ----------------------------------------------------------------------------
// Capabilities
// ----------------------------------------------------------------------------

void joypad_xinput_caps(joypad_xinput_type_t type, joypad_caps_t* out);

// ----------------------------------------------------------------------------
// State → input_event_t
// ----------------------------------------------------------------------------

// Convert a decoded XInput gamepad state to a canonical input_event_t.
// Maps Xbox A/B/X/Y to canonical B1/B2/B3/B4 (south/east/west/north).
// Inverts thumb Y to match HID convention (0=up, 255=down).
// Sets has_pressure = true with the canonical 12-slot pressure layout when
// type == XBOX_OG and the per-button pressure values are populated.
void joypad_xinput_gamepad_to_event(const joypad_xinput_gamepad_t* pad,
                                    joypad_xinput_type_t type,
                                    input_event_t* out);

#ifdef __cplusplus
}
#endif

#endif // JOYPAD_DEVICES_MICROSOFT_XINPUT_H
