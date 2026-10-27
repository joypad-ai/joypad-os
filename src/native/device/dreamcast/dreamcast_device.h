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
#ifndef MAPLE_PIN1
#define MAPLE_PIN1      2    // Data line A (Dreamcast controller Pin 1)
#endif
#ifndef MAPLE_PIN5
#define MAPLE_PIN5      3    // Data line B (Dreamcast controller Pin 5)
#endif

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
// BUTTON MAPPING (JP_BUTTON_* to DC)
// ============================================================================
//
// JP -> DC mapping:
//   B1-B4      -> A, B, X, Y (face buttons)
//   L1         -> (unmapped)
//   R1         -> D button (N64 Z, GC Z)
//   L2         -> L trigger (analog; 255 if digital-only)
//   R2         -> R trigger (analog; 255 if digital-only)
//   L3/R3      -> Z, C (extra face buttons)
//   S1         -> D (arcade stick 2nd start)
//   S2         -> Start
//   D-pad      -> D-pad
//   A1 (guide) -> Start
//
// This allows:
//   - N64: L/R (L2/R2) -> DC triggers, Z (R1) -> DC D, C-Up/C-Right (L3/R3) -> DC Z/C
//   - USB: Triggers (L2/R2) -> DC triggers, RB (R1) -> DC D, LB (L1) unused
//

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

// Initialization
void dreamcast_init(void);

// Core 1 task (real-time Maple Bus handling)
void __not_in_flash_func(dreamcast_core1_task)(void);

// Returns current Maple Bus RX packet count (increments each packet Core 1 processes).
// Use to detect bus idle: if count hasn't changed, no packet is in flight.
uint32_t dreamcast_rx_count(void);

// Pause/resume Maple Bus TX responses — set before flash erase/program,
// clear immediately after. Core 1 skips sending while paused; DC retries
// on next 16.7ms frame. Keep pause under ~10ms to avoid DC dropout.
void dreamcast_set_maple_pause(bool pause);

// LED timeout — set by VMU activity handlers, cleared by dreamcast_task()
extern uint32_t vmu_led_timeout_ms;

// Core 0 task (periodic maintenance)
void dreamcast_task(void);

// Update output state from router
void __not_in_flash_func(dreamcast_update_output)(void);

// Direct state update for low-latency input sources (bypasses router)
// buttons: DC format (active-low: 0xFFFF = none pressed)
// axes: 0-255 with 128 = center
void dreamcast_set_controller_state(uint8_t port, uint16_t buttons,
                                     uint8_t joy_x, uint8_t joy_y,
                                     uint8_t joy2_x, uint8_t joy2_y,
                                     uint8_t lt, uint8_t rt);

// Get rumble state from DC (for feedback to input controllers)
// Returns rumble power level (0 = off, 1-7 = intensity)
uint8_t dreamcast_get_rumble(uint8_t port);

// Get raw Puru Puru state for detailed rumble control
// Returns true if rumble enabled, fills power (0-7), freq, inc
bool dreamcast_get_purupuru_state(uint8_t port, uint8_t* power, uint8_t* freq, uint8_t* inc);

// Enable VMU sub-peripheral advertisement
// Call once VMU is initialized and ready to respond to Maple Bus queries
// Rebuilds controller Device Info packets to include ADDRESS_SUBPERIPHERAL0
void dreamcast_enable_vmu(void);

// OutputInterface accessor
#include "core/output_interface.h"
extern const OutputInterface dreamcast_output_interface;

#endif // DREAMCAST_DEVICE_H
