// loopy_device.h

#ifndef LOOPY_DEVICE_H
#define LOOPY_DEVICE_H

#include <pico/stdlib.h>
// #include <hardware/pio.h>

#include "tusb.h"
#include "loopy.pio.h"
#include "core/globals.h"

// Define constants
#undef MAX_PLAYERS
#define MAX_PLAYERS 4               // Loopy supports up to 4 players

// The pinout when looking into the front of the console is as follows 
//  (pin numbers taken from mainboard markings):
//
// 9                                   16
// ROW1 bit0 bit3 bit4 bit5 ROW5 ROW3 GND
// ROW0 ROW2 bit1 bit2 bit6 bit7 ROW4 VCC
// 1                                    8
//

// ADAFRUIT_KB2040                  // build for Adafruit KB2040 board
#define ROW0_PIN    26
#define ROW1_PIN    ROW0_PIN + 1    // 27
#define ROW2_PIN    ROW0_PIN + 2    // 28
#define ROW3_PIN    ROW0_PIN + 3    // 29
#define ROW4_PIN    18
#define ROW5_PIN    19
#define BIT0_PIN    2               // Note - out pins must be a consecutive 'out' group
#define BIT1_PIN    BIT0_PIN + 1
#define BIT2_PIN    BIT0_PIN + 2
#define BIT3_PIN    BIT0_PIN + 3
#define BIT4_PIN    BIT0_PIN + 4
#define BIT5_PIN    BIT0_PIN + 5
#define BIT6_PIN    BIT0_PIN + 6
#define BIT7_PIN    BIT0_PIN + 7

#define LOOPY_BIT0 (1<<0)
#define LOOPY_BIT1 (1<<1)
#define LOOPY_BIT2 (1<<2)
#define LOOPY_BIT3 (1<<3)
#define LOOPY_BIT4 (1<<4)
#define LOOPY_BIT5 (1<<5)
#define LOOPY_BIT6 (1<<6)
#define LOOPY_BIT7 (1<<7)

extern PIO pio;
extern uint sm1, sm2, sm3; // sm1 = ROW0, sm2 = ROW1, sm3 = ROW2

// Function declarations
void loopy_init(void);
void __not_in_flash_func(core1_entry)(void);
void __not_in_flash_func(update_output)(void);

#endif // LOOPY_DEVICE_H
