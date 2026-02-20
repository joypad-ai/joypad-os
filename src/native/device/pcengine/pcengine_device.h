// pcengine_device.h

#ifndef PCENGINE_DEVICE_H
#define PCENGINE_DEVICE_H

#include <stdint.h>

#include "tusb.h"
#include "hardware/pio.h"
#include "plex.pio.h"
#include "clock.pio.h"
#include "select.pio.h"
#include "core/buttons.h"
#include "core/uart.h"

// Define constants
#undef MAX_PLAYERS
#define MAX_PLAYERS 5               // PCE supports up to 5 players

// Defaults to ADAFRUIT_KB2040, build for Adafruit KB2040 board
#define DATAIN_PIN  18
#define CLKIN_PIN   DATAIN_PIN + 1  // Note - in pins must be a consecutive 'in' group

#ifdef RPI_PICO_BUILD
  #define OUTD0_PIN   4
  #define OUTD1_PIN   5
  #define OUTD2_PIN   6
  #define OUTD3_PIN   7
#else
  // Default configuration
  #define OUTD0_PIN   26 // Note - out pins must be a consecutive 'out' group
  #define OUTD1_PIN   27
  #define OUTD2_PIN   28
  #define OUTD3_PIN   29
#endif

// PCE button modes
#define BUTTON_MODE_2 0x00
#define BUTTON_MODE_6 0x01
#define BUTTON_MODE_3_SEL 0x02
#define BUTTON_MODE_3_RUN 0x03

extern PIO pio;
extern uint sm1, sm2, sm3; // sm1 = plex; sm2 = clock, sm3 = select

// Function declarations
void pce_init(void);
void pce_task(void);
void __not_in_flash_func(core1_task)(void);
void __not_in_flash_func(update_output)(void);

#endif // PCENGINE_DEVICE_H
