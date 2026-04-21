// psx_host.h - Native PS1 / PS2 controller host driver
//
// Reads a PSX controller (DualShock / DualShock 2 / original digital pad) using
// the 5-wire SIO-like protocol: CMD, DAT, CLK, ATT, ACK. Supports digital,
// analog (DualShock), and pressure-sensitive (DualShock 2) modes. The driver
// sends the standard poll command (0x42) every task invocation and decodes the
// response into input_event_t.

#ifndef PSX_HOST_H
#define PSX_HOST_H

#include <stdint.h>
#include <stdbool.h>
#include "core/input_interface.h"

// ============================================================================
// DEFAULT PIN CONFIGURATION
// ============================================================================
// Defaults follow PicoGamepadConverter convention so existing wiring works.
// Only DAT needs a pull-up; the other three lines are driven by the Pico.

#ifndef PSX_PIN_CMD
#define PSX_PIN_CMD  19   // host -> controller command line
#endif
#ifndef PSX_PIN_CLK
#define PSX_PIN_CLK  20   // host -> controller clock (~250 kHz)
#endif
#ifndef PSX_PIN_ATT
#define PSX_PIN_ATT  21   // host -> controller attention (chip-select, active low)
#endif
#ifndef PSX_PIN_DAT
#define PSX_PIN_DAT  22   // controller -> host data (open-drain; pull-up required)
#endif

// ============================================================================
// PUBLIC API
// ============================================================================

// Initialise with default pins above.
void psx_host_init(void);

// Initialise with explicit pins.
void psx_host_init_pins(uint8_t cmd, uint8_t clk, uint8_t att, uint8_t dat);

// Poll the controller once (sends 0x01 0x42 ...) and submit a router event if
// the state changed. Call from the app task loop; ~1 kHz is plenty.
void psx_host_task(void);

// True if the most recent poll saw a valid controller response.
bool psx_host_is_connected(void);

extern const InputInterface psx_input_interface;

#endif // PSX_HOST_H
