// nuon_host.h - Native Nuon Controller Host Driver
//
// Reads native Nuon controllers (Polyface peripherals) and submits
// input events to the router. This is the reverse of the nuon_device
// which emulates a controller for a Nuon console.

#ifndef NUON_HOST_H
#define NUON_HOST_H

#include <stdint.h>
#include <stdbool.h>
#include "core/input_interface.h"

// ============================================================================
// CONFIGURATION
// ============================================================================

// GPIO pins for Polyface bus (configurable via compile definitions)
#ifndef NUON_PIN_DATA
#define NUON_PIN_DATA  2   // Data I/O (bidirectional)
#endif
#define NUON_PIN_CLK   (NUON_PIN_DATA + 1)  // Clock (we generate this as host)

// Polling interval (ms) - not a rate, we poll as fast as practical
#define NUON_POLL_INTERVAL_MS  16  // ~60Hz target

// Maximum number of Nuon controllers
#define NUON_MAX_PORTS 1

// ============================================================================
// NUON BUTTON DEFINITIONS (from nuon_device.h)
// ============================================================================

// Nuon button bits in SWITCH response (16-bit word)
#define NUON_BTN_UP      0x0200
#define NUON_BTN_DOWN    0x0800
#define NUON_BTN_LEFT    0x0400
#define NUON_BTN_RIGHT   0x0100
#define NUON_BTN_A       0x4000
#define NUON_BTN_B       0x0008
#define NUON_BTN_L       0x0020
#define NUON_BTN_R       0x0010
#define NUON_BTN_C_UP    0x0002
#define NUON_BTN_C_DOWN  0x8000
#define NUON_BTN_C_LEFT  0x0004
#define NUON_BTN_C_RIGHT 0x0001
#define NUON_BTN_START   0x2000
#define NUON_BTN_NUON    0x1000  // Z button

// ============================================================================
// POLYFACE PROTOCOL CONSTANTS
// ============================================================================

// Command addresses
#define PF_CMD_RESET   0xB1  // WRITE, S=0x00, C=0x00 - Reset device state
#define PF_CMD_ALIVE   0x80  // READ, S=0x04, C=0x40 - Check if alive
#define PF_CMD_MAGIC   0x90  // READ - Returns "JUDE" (0x4A554445)
#define PF_CMD_PROBE   0x94  // READ, S=0x04, C=0x00 - Get device info
#define PF_CMD_BRAND   0xB4  // WRITE, S=0x00, C=<id> - Assign ID
#define PF_CMD_STATE   0x99  // READ/WRITE, S=0x01 - Device state
#define PF_CMD_CHANNEL 0x34  // WRITE, S=0x01, C=<channel> - Select analog channel
#define PF_CMD_SWITCH  0x30  // READ, S=0x02, C=0x00 - Get buttons
#define PF_CMD_ANALOG  0x35  // READ, S=0x01, C=0x00 - Get analog value

// Analog channels
#define PF_CHANNEL_NONE 0x00
#define PF_CHANNEL_MODE 0x01
#define PF_CHANNEL_X1   0x02
#define PF_CHANNEL_Y1   0x03
#define PF_CHANNEL_X2   0x04
#define PF_CHANNEL_Y2   0x05

// Packet types
#define PF_TYPE_WRITE 0
#define PF_TYPE_READ  1

// Magic response
#define PF_MAGIC_RESPONSE 0x4A554445  // "JUDE"

// State flags
#define PF_STATE_ENABLE 0x80  // Bit 7: Enable device
#define PF_STATE_ROOT   0x40  // Bit 6: Root device

// CRC polynomial
#define PF_CRC16 0x8005

// ============================================================================
// PUBLIC API
// ============================================================================

// Initialize Nuon host driver
void nuon_host_init(void);

// Core 1 entry point - handles clock generation and protocol
void nuon_host_core1_task(void);

// Core 0 task - submits input to router
void nuon_host_task(void);

// Check if controller is connected
bool nuon_host_is_connected(void);

// Get device count
uint8_t nuon_host_get_device_count(void);

// Nuon input interface (implements InputInterface pattern)
extern const InputInterface nuon_input_interface;

// ============================================================================
// SHARED STATE (Core 1 -> Core 0)
// ============================================================================

// These volatiles are written by Core 1 and read by Core 0
extern volatile bool nuon_controller_connected;
extern volatile uint16_t nuon_buttons;
extern volatile uint8_t nuon_analog_x1;
extern volatile uint8_t nuon_analog_y1;
extern volatile uint8_t nuon_analog_x2;
extern volatile uint8_t nuon_analog_y2;
extern volatile uint32_t nuon_poll_count;

#endif // NUON_HOST_H
