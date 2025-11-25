// nuon_device.h

#ifndef NUON_DEVICE_H
#define NUON_DEVICE_H

#include <stdint.h>
#include "hardware/pio.h"
#include "polyface_read.pio.h"
#include "polyface_send.pio.h"
#include "core/globals.h"
// #include "pico/util/queue.h"

// Define constants
#undef MAX_PLAYERS
#define MAX_PLAYERS       4

// Nuon GPIO pins
#define DATAIO_PIN        2
#define CLKIN_PIN         DATAIO_PIN + 1  // Note - in pins must be a consecutive 'in' group

// for internal in-game reset
#define POWER_PIN         4
#define STOP_PIN          11

// Nuon packet start bit type
#define PACKET_TYPE_READ  1
#define PACKET_TYPE_WRITE 0

// Nuon analog modes
#define ATOD_CHANNEL_NONE 0x00
#define ATOD_CHANNEL_MODE 0x01
#define ATOD_CHANNEL_X1 0x02
#define ATOD_CHANNEL_Y1 0x03
#define ATOD_CHANNEL_X2 0x04
#define ATOD_CHANNEL_Y2 0x05

// Nuon controller PROBE options
#define DEFCFG 1
#define VERSION 11
#define TYPE 3
#define MFG 0
#define CRC16 0x8005
#define MAGIC 0x4A554445 // HEX to ASCII == "JUDE" (The Polyface inventor)

// buttons
#define NUON_BUTTON_UP      0x0200
#define NUON_BUTTON_DOWN    0x0800
#define NUON_BUTTON_LEFT    0x0400
#define NUON_BUTTON_RIGHT   0x0100
#define NUON_BUTTON_A       0x4000
#define NUON_BUTTON_B       0x0008
#define NUON_BUTTON_L       0x0020
#define NUON_BUTTON_R       0x0010
#define NUON_BUTTON_C_UP    0x0002
#define NUON_BUTTON_C_DOWN  0x8000
#define NUON_BUTTON_C_LEFT  0x0004
#define NUON_BUTTON_C_RIGHT 0x0001
#define NUON_BUTTON_START   0x2000
#define NUON_BUTTON_NUON    0x1000 // Z

// fun
#undef KONAMI_CODE
#define KONAMI_CODE {NUON_BUTTON_UP, NUON_BUTTON_UP, NUON_BUTTON_DOWN, NUON_BUTTON_DOWN, NUON_BUTTON_LEFT, NUON_BUTTON_RIGHT, NUON_BUTTON_LEFT, NUON_BUTTON_RIGHT, NUON_BUTTON_B, NUON_BUTTON_A}

// Declaration of global variables
extern PIO pio;
extern uint sm1, sm2; // sm1 = send; sm2 = read

extern int crc_lut[256]; // crc look up table
// queue_t packet_queue;

// Function declarations
void nuon_init(void);
void nuon_task(void);
uint32_t __rev(uint32_t);
uint8_t eparity(uint32_t);
int crc_calc(unsigned char data,int crc);
uint32_t crc_data_packet(int32_t value, int8_t size);

void __not_in_flash_func(core1_entry)(void);
void __not_in_flash_func(update_output)(void);

#endif // NUON_DEVICE_H
