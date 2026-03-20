// lodgenet_host.h - LodgeNet Controller Host Driver
//
// Reads LodgeNet hotel gaming controllers via a proprietary 3-wire serial
// protocol reverse-engineered from a SNES-based controller tester ROM.
//
// Protocol: synchronous serial, host-clocked, one-way (controller → host)
//   - CLOCK: host output (GPIO), ~20 kHz
//   - DATA:  controller output (GPIO input), sampled on falling clock edge
//   - Frame: double-strobe init + 64 clocked bits = 8 bytes
//   - Supports both N64 and GameCube LodgeNet controllers (same protocol)
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

// Default GPIO pins (can be overridden by app_config.h)
#ifndef LODGENET_PIN_CLOCK
#define LODGENET_PIN_CLOCK  2   // Clock output to controller
#endif

#ifndef LODGENET_PIN_DATA
#define LODGENET_PIN_DATA   3   // Data input from controller
#endif

// ============================================================================
// PUBLIC API
// ============================================================================

// Initialize with default pins
void lodgenet_host_init(void);

// Initialize with custom pin configuration
void lodgenet_host_init_pins(uint8_t clock_pin, uint8_t data_pin);

// Poll controller and submit events to router
void lodgenet_host_task(void);

// Check if a controller is connected (received valid data recently)
bool lodgenet_host_is_connected(void);

// Input interface for app declaration
extern const InputInterface lodgenet_input_interface;

#endif // LODGENET_HOST_H
