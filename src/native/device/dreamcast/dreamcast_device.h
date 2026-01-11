// dreamcast_device.h - Dreamcast Maple Bus output interface
// Emulates a Dreamcast controller for connecting USB/BT controllers to a Dreamcast console

#ifndef DREAMCAST_DEVICE_H
#define DREAMCAST_DEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/pio.h"
#include "core/buttons.h"

// Dreamcast supports up to 4 controllers
#undef MAX_PLAYERS
#define MAX_PLAYERS 4

// ============================================================================
// GPIO PIN ASSIGNMENTS (KB2040)
// ============================================================================
// Maple Bus uses differential signaling on two consecutive pins
//
// Reference implementations:
//   - MaplePad: GPIO 11/12
//   - USB4Maple (RP2040): GPIO 14/15
//
// KB2040 uses GPIO 2/3 for convenience. WS2812_PIN=17 avoids conflict.
// TODO: Make configurable via web config per board.

#define MAPLE_PIN1      2   // Data line A (Dreamcast controller Pin 1)
#define MAPLE_PIN5      3   // Data line B (Dreamcast controller Pin 5)

// ============================================================================
// MAPLE BUS PROTOCOL CONSTANTS
// ============================================================================

// Frame types
#define MAPLE_FRAME_HOST      0x00  // Request from console
#define MAPLE_FRAME_DEVICE    0x01  // Response from peripheral

// Command codes
#define MAPLE_CMD_DEVICE_INFO     0x01  // Device info request
#define MAPLE_CMD_EXT_DEV_INFO    0x02  // Extended device info
#define MAPLE_CMD_RESET           0x03  // Device reset
#define MAPLE_CMD_KILL            0x04  // Device shutdown
#define MAPLE_CMD_GET_CONDITION   0x09  // Poll controller state
#define MAPLE_CMD_GET_MEDIA_INFO  0x0A  // Get media info (VMU)
#define MAPLE_CMD_BLOCK_READ      0x0B  // Read memory block
#define MAPLE_CMD_BLOCK_WRITE     0x0C  // Write memory block
#define MAPLE_CMD_SET_CONDITION   0x0E  // Set peripheral settings (rumble)

// Response codes
#define MAPLE_RESP_DEVICE_INFO    0x05  // Device info response
#define MAPLE_RESP_EXT_DEV_INFO   0x06  // Extended device info response
#define MAPLE_RESP_ACK            0x07  // Command acknowledged
#define MAPLE_RESP_DATA_TRANSFER  0x08  // Data transfer response

// Function types (device capabilities)
#define MAPLE_FT_CONTROLLER  0x00000001  // FT0: Standard controller
#define MAPLE_FT_MEMCARD     0x00000002  // FT1: VMU/Memory card
#define MAPLE_FT_LCD         0x00000004  // FT2: LCD display
#define MAPLE_FT_TIMER       0x00000008  // FT3: Timer/RTC
#define MAPLE_FT_AUDIO       0x00000010  // FT4: Audio input
#define MAPLE_FT_ARGUN       0x00000080  // FT7: AR Gun
#define MAPLE_FT_KEYBOARD    0x00000040  // FT6: Keyboard
#define MAPLE_FT_GUN         0x00000080  // FT7: Light gun
#define MAPLE_FT_VIBRATION   0x00000100  // FT8: Puru Puru (rumble)

// Addressing
#define MAPLE_PORT_MASK       0xC0  // Bits 7-6: Port number (0-3)
#define MAPLE_PERIPHERAL_MASK 0x3F  // Bits 5-0: Peripheral ID
#define MAPLE_ADDR_MAIN       0x20  // Main peripheral (controller)
#define MAPLE_ADDR_SUB1       0x01  // Subperipheral 1 (VMU slot A)
#define MAPLE_ADDR_SUB2       0x02  // Subperipheral 2 (VMU slot B)

// Timing (microseconds)
#define MAPLE_RESPONSE_DELAY_US  50   // Min delay before response

// ============================================================================
// DREAMCAST BUTTON DEFINITIONS
// ============================================================================
// Active-low in hardware (0 = pressed), but we handle this in the driver

#define DC_BTN_C      (1 << 0)   // C button
#define DC_BTN_B      (1 << 1)   // B button (right face)
#define DC_BTN_A      (1 << 2)   // A button (bottom face)
#define DC_BTN_START  (1 << 3)   // Start button
#define DC_BTN_UP     (1 << 4)   // D-pad up
#define DC_BTN_DOWN   (1 << 5)   // D-pad down
#define DC_BTN_LEFT   (1 << 6)   // D-pad left
#define DC_BTN_RIGHT  (1 << 7)   // D-pad right
#define DC_BTN_Z      (1 << 8)   // Z button (left trigger digital)
#define DC_BTN_Y      (1 << 9)   // Y button (top face)
#define DC_BTN_X      (1 << 10)  // X button (left face)
#define DC_BTN_D      (1 << 11)  // D button (second start, arcade stick)

// ============================================================================
// DATA STRUCTURES
// ============================================================================

// Controller state (what we send to Dreamcast)
typedef struct {
    uint16_t buttons;      // Button state (active-low: 0xFFFF = none pressed)
    uint8_t rt;            // Right trigger (0 = released, 255 = full)
    uint8_t lt;            // Left trigger (0 = released, 255 = full)
    uint8_t joy_x;         // Left stick X (0-255, 128 = center)
    uint8_t joy_y;         // Left stick Y (0-255, 128 = center)
    uint8_t joy2_x;        // Right stick X (for extended controllers)
    uint8_t joy2_y;        // Right stick Y (for extended controllers)
} dc_controller_state_t;

// ============================================================================
// BUTTON MAPPING (JP_BUTTON_* to DC buttons)
// ============================================================================

// Default mapping: W3C gamepad order to Dreamcast
// B1 (Cross/A) -> A, B2 (Circle/B) -> B, B3 (Square/X) -> X, B4 (Triangle/Y) -> Y
#define DC_MAP_B1     DC_BTN_A
#define DC_MAP_B2     DC_BTN_B
#define DC_MAP_B3     DC_BTN_X
#define DC_MAP_B4     DC_BTN_Y
#define DC_MAP_L1     DC_BTN_Z      // L1 -> Z (no L bumper on DC)
#define DC_MAP_R1     DC_BTN_C      // R1 -> C (no R bumper on DC, use C)
#define DC_MAP_S1     DC_BTN_D      // Select -> D (arcade stick 2nd start)
#define DC_MAP_S2     DC_BTN_START  // Start
#define DC_MAP_DU     DC_BTN_UP
#define DC_MAP_DD     DC_BTN_DOWN
#define DC_MAP_DL     DC_BTN_LEFT
#define DC_MAP_DR     DC_BTN_RIGHT
#define DC_MAP_A1     DC_BTN_START  // Guide/Home -> Start

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

// Initialization
void dreamcast_init(void);

// Core 1 task (real-time Maple Bus handling)
void __not_in_flash_func(dreamcast_core1_task)(void);

// Core 0 task (periodic maintenance)
void dreamcast_task(void);

// Update output state from router
void __not_in_flash_func(dreamcast_update_output)(void);

// OutputInterface accessor
#include "core/output_interface.h"
extern const OutputInterface dreamcast_output_interface;

#endif // DREAMCAST_DEVICE_H
