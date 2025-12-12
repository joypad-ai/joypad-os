// 3do.h
// 3DO Console Protocol Header
// Based on USBTo3DO by FCare (https://github.com/FCare/USBTo3DO)
// Integrated into Joypad multi-console architecture

#ifndef THREEDOH
#define THREEDOH

#include <stdint.h>

#include "tusb.h"
#include "hardware/pio.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"

// Define constants
#undef MAX_PLAYERS
#define MAX_PLAYERS 8               // 3DO supports up to 8 controllers

// GPIO Pin definitions (from USBTo3DO)
// These match the Waveshare RP2040 Zero pinout
#define CLK_PIN 2           // Clock input from 3DO console
#define DATA_OUT_PIN 3      // Data output to 3DO console
#define DATA_IN_PIN 4       // Data input from next controller (daisy chain)
#define CS_CTRL_PIN 5       // Chip Select / Control signal

// UART Debug pins (avoid pins 2-5 used by 3DO protocol)
#define UART_ID uart0
#define BAUD_RATE 115200
#define UART_TX_PIN 0       // UART0 TX
#define UART_RX_PIN 1       // UART0 RX

// Include PIO headers AFTER pin definitions
#include "sampling.pio.h"
#include "output.pio.h"

// 3DO Report Structures

// 3DO Joypad Report (2 bytes / 16 bits)
// Standard gamepad with digital buttons
typedef struct {
  // LSB byte
  uint16_t A : 1;       // A button (primary action)
  uint16_t left : 1;    // D-pad left
  uint16_t right : 1;   // D-pad right
  uint16_t up : 1;      // D-pad up
  uint16_t down : 1;    // D-pad down
  uint16_t id: 3;       // Device ID (0b100 = standard pad)

  // MSB byte
  uint16_t tail : 2;    // Tail bits (0b00)
  uint16_t L : 1;       // L button (left shoulder)
  uint16_t R : 1;       // R button (right shoulder)
  uint16_t X : 1;       // X button
  uint16_t P : 1;       // P button (Play/Pause)
  uint16_t C : 1;       // C button
  uint16_t B : 1;       // B button
} __attribute__((packed)) _3do_joypad_report;

// 3DO Joystick Report (9 bytes / 72 bits)
// Flight stick with analog axes and digital buttons
typedef struct {
  uint8_t id_0;         // ID byte 0 (0x01)
  uint8_t id_1;         // ID byte 1 (0x7B)
  uint8_t id_2;         // ID byte 2 (0x08)

  uint8_t analog1;      // Analog axis 1 (X axis)
  uint8_t analog2;      // Analog axis 2 (Y axis)
  uint8_t analog3;      // Analog axis 3 (Z axis / twist)
  uint8_t analog4;      // Analog axis 4 (throttle)

  uint8_t left  : 1;    // D-pad left
  uint8_t right : 1;    // D-pad right
  uint8_t down  : 1;    // D-pad down
  uint8_t up    : 1;    // D-pad up
  uint8_t C     : 1;    // C button
  uint8_t B     : 1;    // B button
  uint8_t A     : 1;    // A button
  uint8_t FIRE  : 1;    // FIRE button (trigger)

  uint8_t tail  : 4;    // Tail bits (0x0)
  uint8_t R     : 1;    // R button (right shoulder)
  uint8_t L     : 1;    // L button (left shoulder)
  uint8_t X     : 1;    // X button
  uint8_t P     : 1;    // P button (Play/Pause)
} __attribute__((packed)) _3do_joystick_report;

// 3DO Mouse Report (4 bytes / 32 bits)
// Mouse with relative motion and buttons
typedef struct {
  uint8_t id;           // Device ID (0x49)

  uint8_t dy_up     : 4;  // Y delta upper nibble
  uint8_t shift     : 1;  // Shift button
  uint8_t right     : 1;  // Right mouse button
  uint8_t middle    : 1;  // Middle mouse button
  uint8_t left      : 1;  // Left mouse button

  uint8_t dx_up     : 2;  // X delta upper 2 bits
  uint8_t dy_low    : 6;  // Y delta lower 6 bits

  uint8_t dx_low;         // X delta lower 8 bits
} __attribute__((packed)) _3do_mouse_report;

// 3DO Silly Control Pad Report (2 bytes / 16 bits)
// Used for arcade JAMMA integration (Orbatak, etc.)
// ID: 0xC0 0x00
typedef struct {
  // LSB byte - ID 0xC0
  uint8_t id;             // Device ID (0xC0)

  // MSB byte - Button data
  uint8_t service   : 1;  // Service button (bit 0)
  uint8_t unused1   : 1;  // Unused (bit 1)
  uint8_t p2_start  : 1;  // Player 2 Start (bit 2)
  uint8_t unused2   : 1;  // Unused (bit 3)
  uint8_t p2_coin   : 1;  // Player 2 Coin (bit 4)
  uint8_t unused3   : 1;  // Unused (bit 5)
  uint8_t p1_start  : 1;  // Player 1 Start (bit 6)
  uint8_t p1_coin   : 1;  // Player 1 Coin (bit 7)
} __attribute__((packed)) _3do_silly_report;

// Controller type enumeration
typedef enum {
  CONTROLLER_NONE = 0,
  CONTROLLER_JOYPAD,
  CONTROLLER_JOYSTICK,
  CONTROLLER_MOUSE,
  CONTROLLER_SILLY,     // Arcade JAMMA silly pad
} controller_type_3do;

// 3DO output mode (toggleable via hotkey)
typedef enum {
  TDO_MODE_NORMAL = 0,  // Normal joypad/joystick output
  TDO_MODE_SILLY,       // Silly control pad (arcade JAMMA)
} tdo_output_mode_t;

// 3DO extension port mode
typedef enum {
  TDO_EXT_PASSTHROUGH = 0,  // Relay extension data unchanged (default)
  TDO_EXT_MANAGED,          // Parse extension controllers through player system
} tdo_extension_mode_t;

// Declaration of global variables
extern PIO pio;
extern uint sm_sampling, sm_output;

// Report buffers
extern uint8_t current_reports[MAX_PLAYERS][9];  // Max 9 bytes per report (joystick)
extern uint8_t report_sizes[MAX_PLAYERS];
extern volatile bool device_attached[MAX_PLAYERS];
extern uint8_t controller_buffer[201];           // DMA output buffer

// Function declarations
void _3do_init(void);
void _3do_task(void);
void setup_3do_dma_output(void);
void setup_3do_dma_input(void);
void on_pio0_irq(void);

void __not_in_flash_func(core1_task)(void);
void __not_in_flash_func(update_3do_report)(uint8_t player_index);

// post_globals() and post_mouse_globals() removed - replaced by router architecture
// USB drivers now call router_submit_input() instead

// Report constructor functions
_3do_joypad_report new_3do_joypad_report(void);
_3do_joystick_report new_3do_joystick_report(void);
_3do_mouse_report new_3do_mouse_report(void);
_3do_silly_report new_3do_silly_report(void);

// Report update functions
void update_3do_joypad(_3do_joypad_report report, uint8_t instance);
void update_3do_joystick(_3do_joystick_report report, uint8_t instance);
void update_3do_mouse(_3do_mouse_report report, uint8_t instance);
void update_3do_silly(_3do_silly_report report, uint8_t instance);

// Mode management
tdo_output_mode_t tdo_get_output_mode(void);
void tdo_set_output_mode(tdo_output_mode_t mode);
void tdo_toggle_output_mode(void);

// Extension mode management
tdo_extension_mode_t tdo_get_extension_mode(void);
void tdo_set_extension_mode(tdo_extension_mode_t mode);
void tdo_toggle_extension_mode(void);

#endif // THREEDOH
