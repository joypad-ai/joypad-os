// n64_device.h - N64 Output Device
//
// Emulates an N64 controller connected to an N64 console.
// Takes USB/BT controller input and outputs N64 joybus protocol.

#ifndef N64_DEVICE_H
#define N64_DEVICE_H

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include "tusb.h"
#include "hardware/pio.h"
#include "lib/joybus-pio/include/n64_definitions.h"
#include "core/buttons.h"
#include "core/uart.h"

// Define constants
#undef MAX_PLAYERS
#define MAX_PLAYERS 1   // N64 is single player per adapter (no multitap yet)

#define SHIELD_PIN_L 4   // Connector shielding mounted to GPIOs [4, 5, 26, 27]
#define SHIELD_PIN_R 26

#define BOOTSEL_PIN 11
#define N64_DATA_PIN 7   // N64 joybus data line
#define N64_3V3_PIN 6    // N64 3.3V detection pin

// Global variables
extern PIO pio;

// Function declarations
void n64_init(void);

void __not_in_flash_func(core1_task)(void);
void __not_in_flash_func(update_output)(void);

#endif // N64_DEVICE_H
