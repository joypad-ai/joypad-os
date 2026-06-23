// pce_host.h - Native PCEngine / TurboGrafx-16 Host Driver
//
// Reads a real PCEngine controller (2-button standard pad or 6-button
// Avenue Pad 6) by bit-banging the SEL / CLR mux lines and sampling the
// four data lines.  Decoded input is forwarded to the router.
//
// PCEngine pads are a 74157-style 2:1 multiplexer (no shift-register clock):
//   SEL = HIGH -> D0..D3 = Up, Right, Down, Left   (d-pad nibble)
//   SEL = LOW  -> D0..D3 = I,  II,  Select, Run     (button nibble)
// All signals are active-LOW (0 = pressed).  CLR resets / advances the mux
// bank; a 6-button pad exposes III..VI in a second bank flagged by an
// all-zero "signature" nibble.

#ifndef PCE_HOST_H
#define PCE_HOST_H

#include "core/input_interface.h"

// --- Pin configuration (KB2040 defaults; override in app.h) ---------------
// SEL and CLR are MCU outputs to the controller; D0..D3 are MCU inputs and
// MUST be four consecutive GPIOs starting at PCE_PIN_D0.
#ifndef PCE_PIN_SEL
#define PCE_PIN_SEL 5
#endif

#ifndef PCE_PIN_CLR
#define PCE_PIN_CLR 6
#endif

#ifndef PCE_PIN_D0
#define PCE_PIN_D0 8   // D0=8, D1=9, D2=10, D3=11 (consecutive)
#endif

#define PCE_MAX_PORTS 5   // single pad, or up to 5 via PCEngine multitap

void pce_host_init(void);

void pce_host_task(void);

bool pce_host_is_connected(void);

extern const InputInterface pce_input_interface;

#endif // PCE_HOST_H
