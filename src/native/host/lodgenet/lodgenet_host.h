// lodgenet_host.h - LodgeNet Controller Host Driver
//
// Reads LodgeNet hotel gaming controllers via proprietary serial protocols
// reverse-engineered from SNES tester ROM and Arduino reference implementation.
//
// Two protocol families, auto-detected:
//   MCU (N64/GC): hello pulse + 80 clocked bits, MSB-first, ~60Hz
//   SR (SNES):    dual-clock shift register, 16 bits + presence, ~1kHz
//
// Physical connector: RJ11 (4-pin)
//   Pin 1: +5V, Pin 2: CLOCK, Pin 3: DATA, Pin 4: GND

#ifndef LODGENET_HOST_H
#define LODGENET_HOST_H

#include <stdint.h>
#include <stdbool.h>
#include "core/input_interface.h"

// ============================================================================
// CONFIGURATION
// ============================================================================

// Default GPIO pins (can be overridden by app before #include)
#ifndef LODGENET_PIN_CLOCK
#define LODGENET_PIN_CLOCK  5   // CLK1 output to controller
#endif

#ifndef LODGENET_PIN_DATA
#define LODGENET_PIN_DATA   7   // Data input from controller
#endif

#ifndef LODGENET_PIN_CLOCK2
#define LODGENET_PIN_CLOCK2 5   // CLK2 output (SNES SR protocol only)
#endif

#ifndef LODGENET_PIN_VCC
#define LODGENET_PIN_VCC    4   // VCC output (drives controller power)
#endif

// Detected controller type
typedef enum {
    LODGENET_DEVICE_NONE,
    LODGENET_DEVICE_N64,
    LODGENET_DEVICE_GC,
    LODGENET_DEVICE_SNES,
} lodgenet_device_t;

// ============================================================================
// PUBLIC API
// ============================================================================

// Initialize with default pins
void lodgenet_host_init(void);

// Initialize with custom pin configuration
void lodgenet_host_init_pins(uint8_t clock_pin, uint8_t data_pin, uint8_t clock2_pin, uint8_t vcc_pin);

// Poll controller and submit events to router
void lodgenet_host_task(void);

// Check if a controller is connected
bool lodgenet_host_is_connected(void);

// Get detected device type
lodgenet_device_t lodgenet_host_get_device_type(void);

// Input interface for app declaration
extern const InputInterface lodgenet_input_interface;

#endif // LODGENET_HOST_H
