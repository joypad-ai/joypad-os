// jaguar_host.h - Native Atari Jaguar Controller Host Driver
//
// Reads a real Jaguar controller (standard 3-button pad or Pro Controller)
// by bit-banging the four active-low column selects (J0..J3) and sampling
// the six return lines (B0, B1, J8..J11).  Decoded input is forwarded to
// the router; the 12-key keypad is emitted as HID keyboard numpad keys via
// the event's kb_keys[] fields (SInput composite keyboard interface).
//
// The pad is a passive diode/switch matrix (no logic ICs), so the whole
// interface runs natively at 3.3V — selects driven from GPIO, returns read
// against internal pull-ups. No level shifters.
//
// Matrix (select value on J3..J0, all active LOW):
//   0xE row0: B0=Pause  B1=A       J8=Up  J9=Down  J10=Left  J11=Right
//   0xD row1: B0=C1(ID) B1=B       J8=*   J9=7     J10=4     J11=1
//   0xB row2: B0=C2(ID) B1=C       J8=0   J9=8     J10=5     J11=2
//   0x7 row3: B0=C3(ID) B1=Option  J8=#   J9=9     J10=6     J11=3
//
// B0 below row 0 is a controller-type ID column (standard pad = all HIGH;
// the 6D/rotary asserts C2 LOW), NOT buttons. The C button is row2/B1.
//
// Pro Controller: X/Y/Z/L/R are the keypad 7/8/9/4/6 positions (shared
// keys, electrically identical) — mode is a user toggle, not auto-detect.

#ifndef JAGUAR_HOST_H
#define JAGUAR_HOST_H

#include "core/input_interface.h"

// --- Pin configuration (override in app.h) ---------------------------------
// Defaults match the RetroFrog usb2jag device wiring (J0..J3=GP2..5, B0=6,
// B1=7, J8..J11=GP8..11) so one HD15 breakout can run either firmware.
// Selects are MCU outputs; B0/B1/J8..J11 are MCU inputs (pulled up).
#ifndef JAG_PIN_J0
#define JAG_PIN_J0   2
#endif
#ifndef JAG_PIN_J1
#define JAG_PIN_J1   3
#endif
#ifndef JAG_PIN_J2
#define JAG_PIN_J2   4
#endif
#ifndef JAG_PIN_J3
#define JAG_PIN_J3   5
#endif
#ifndef JAG_PIN_B0
#define JAG_PIN_B0   6
#endif
#ifndef JAG_PIN_B1
#define JAG_PIN_B1   7
#endif
#ifndef JAG_PIN_J8
#define JAG_PIN_J8   8
#endif
#ifndef JAG_PIN_J9
#define JAG_PIN_J9   9
#endif
#ifndef JAG_PIN_J10
#define JAG_PIN_J10  10
#endif
#ifndef JAG_PIN_J11
#define JAG_PIN_J11  11
#endif

void jaguar_host_init(void);

void jaguar_host_task(void);

bool jaguar_host_is_connected(void);

// Pro Controller mode: keypad 7/8/9/4/6 become X/Y/Z/L/R gamepad buttons
// instead of numpad keys. Toggled at runtime by holding Pause+Option 2s.
bool jaguar_host_get_pro_mode(void);
void jaguar_host_set_pro_mode(bool enabled);

extern const InputInterface jaguar_input_interface;

#endif // JAGUAR_HOST_H
