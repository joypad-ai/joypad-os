// wii_host.h - Native Wii extension controller host driver
//
// Reads a Wii Nunchuck / Classic Controller / Classic Pro via PIO I2C
// (cut extension cable wired directly to the Pico) and submits input
// events to the router. Single port for v1; multi-port is a future
// extension (see .dev/docs/WII_EXTENSION_PLAN.md).

#ifndef WII_HOST_H
#define WII_HOST_H

#include <stdint.h>
#include <stdbool.h>
#include "core/input_interface.h"

// Default GPIO pins. Overridable per-app (see src/apps/wii2usb/app.h).
#ifndef WII_PIN_SDA
#define WII_PIN_SDA   2
#endif
#ifndef WII_PIN_SCL
#define WII_PIN_SCL   3
#endif
// Target I2C clock. Wii extensions tolerate 100..400 kHz; 100 kHz is the
// most forgiving with long/cheap cables and clone accessories.
#ifndef WII_I2C_FREQ_HZ
#define WII_I2C_FREQ_HZ  100000
#endif

void wii_host_init(void);
void wii_host_init_pins(uint8_t sda, uint8_t scl);
void wii_host_task(void);
bool wii_host_is_connected(void);

// Current extension type as a raw wii_ext_type_t value (0 = none).
// Exposed as int to keep this header free of the lib's internal enum.
int  wii_host_get_ext_type(void);

extern const InputInterface wii_input_interface;

#endif // WII_HOST_H
