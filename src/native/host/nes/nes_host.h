// nes_host.h - Native Nintendo Entertainment System (NES) Host Driver
//
// Generates Latch and Pulse signals and polls for responses.
// Responses are forwarded to the router.


#ifndef NES_HOST_H
#define NES_HOST_H

#include "core/input_interface.h"

#ifndef NES_PIN_CLOCK
#define NES_PIN_CLOCK 5
#endif

#ifndef NES_PIN_LATCH
#define NES_PIN_LATCH 6
#endif

#ifndef NES_PIN_DATA0
#define NES_PIN_DATA0 8
#endif
#ifndef NES_PIN_DATA1
#define NES_PIN_DATA1 9
#endif
#ifndef NES_PIN_DATA2
#define NES_PIN_DATA2 10
#endif
#ifndef NES_PIN_DATA3
#define NES_PIN_DATA3 11
#endif

#define NES_MAX_PORTS 4

void nes_host_init(void);

void nes_host_task(void);

bool nes_host_is_connected(void);

extern const InputInterface nes_input_interface;

#endif
