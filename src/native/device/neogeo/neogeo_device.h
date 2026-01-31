// neogeo_device.h

#ifndef NEOGEO_DEVICE_H
#define NEOGEO_DEVICE_H

#include <stdint.h>

#include "tusb.h"
#include "core/buttons.h"
#include "core/uart.h"

// Define constants
#undef NEOGEO_MAX_PLAYERS
#define NEOGEO_MAX_PLAYERS 1 // NEOGEO DB15 1 player

// KB2040 Board
#define NEOGEO_DU_PIN 1 << 29 // GPIO 29
#define NEOGEO_DD_PIN 1 <<  2 // GPIO 2
#define NEOGEO_DR_PIN 1 <<  3 // GPIO 3
#define NEOGEO_DL_PIN 1 << 28 // GPIO 28
#define NEOGEO_S1_PIN 1 <<  6 // GPIO 6
#define NEOGEO_S2_PIN 1 << 18 // GPIO 18
#define NEOGEO_B1_PIN 1 << 27 // GPIO 27
#define NEOGEO_B2_PIN 1 <<  4 // GPIO 4
#define NEOGEO_B3_PIN 1 << 26 // GPIO 26
#define NEOGEO_B4_PIN 1 << 5  // GPIO 5
#define NEOGEO_B5_PIN 1 << 20 // GPIO 20
#define NEOGEO_B6_PIN 1 << 7  // GPIO 7
#define NEOGEO_GPIO_MASK (NEOGEO_DU_PIN | NEOGEO_DD_PIN | NEOGEO_DR_PIN | NEOGEO_DL_PIN | NEOGEO_S1_PIN | NEOGEO_S2_PIN | NEOGEO_B1_PIN | NEOGEO_B2_PIN | NEOGEO_B3_PIN | NEOGEO_B4_PIN | NEOGEO_B5_PIN | NEOGEO_B6_PIN)

// Function declarations
void neogeo_init(void);
void neogeo_task(void);
void __not_in_flash_func(core1_task)(void);
void __not_in_flash_func(update_output)(void);

#endif // NEOGEO_DEVICE_H
