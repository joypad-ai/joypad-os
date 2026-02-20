// 3do_host.h - Native 3DO Controller Host Driver
//
// Polls native 3DO controllers and submits input events to the router.
// Supports joypad, joystick (flightstick), and mouse.
//
// Master mode: Generates CLK signal to read controllers (standalone operation)
// Can be used for apps that need 3DO controller input without 3DO console output.
//
// BUILD INTEGRATION:
// To use this driver in an app, add to CMakeLists.txt:
//
//   # Sources
//   target_sources(joypad_<app> PUBLIC
//       ${CMAKE_CURRENT_SOURCE_DIR}/native/host/3do/3do_host.c
//   )
//
//   # PIO header generation
//   pico_generate_pio_header(joypad_<app>
//       ${CMAKE_CURRENT_LIST_DIR}/native/host/3do/3do_host.pio
//   )
//
//   # Include directory
//   target_include_directories(joypad_<app> PUBLIC
//       ${CMAKE_CURRENT_SOURCE_DIR}/native/host/3do
//   )

#ifndef TDO_HOST_H
#define TDO_HOST_H

#include <stdint.h>
#include <stdbool.h>
// ============================================================================
// CONFIGURATION
// ============================================================================

// Default GPIO pins (match 3DO device pins for hardware compatibility)
#ifndef TDO_HOST_PIN_CLK
#define TDO_HOST_PIN_CLK   2    // Clock output (directly connected to extension CLK or dedicated CLK port)
#endif

#ifndef TDO_HOST_PIN_DATA
#define TDO_HOST_PIN_DATA  4    // Data input (directly connected to extension or dedicated DATA port)
#endif

// Maximum controllers in daisy chain
#define TDO_HOST_MAX_CONTROLLERS 8

// 3DO PBUS timing (microseconds)
// Based on reverse engineering - PBUS runs at approximately 1MHz
#define TDO_CLK_HALF_PERIOD_US  1   // 500kHz clock (conservative)
#define TDO_LATCH_DELAY_US      2   // Delay after frame start

// ============================================================================
// DEVICE TYPES
// ============================================================================

typedef enum {
    TDO_DEVICE_NONE = 0,
    TDO_DEVICE_JOYPAD,      // Standard 3DO gamepad (2 bytes)
    TDO_DEVICE_JOYSTICK,    // Flightstick (9 bytes)
    TDO_DEVICE_MOUSE,       // Mouse (4 bytes)
    TDO_DEVICE_LIGHTGUN,    // Lightgun (4 bytes)
    TDO_DEVICE_ARCADE,      // Arcade/JAMMA (2 bytes)
} tdo_device_type_t;

// ============================================================================
// CONTROLLER STATE
// ============================================================================

typedef struct {
    tdo_device_type_t type;

    // Digital buttons (active-high after parsing)
    bool button_a;
    bool button_b;
    bool button_c;
    bool button_l;
    bool button_r;
    bool button_x;      // Stop/X
    bool button_p;      // Play/Pause

    // D-pad
    bool dpad_up;
    bool dpad_down;
    bool dpad_left;
    bool dpad_right;

    // Analog axes (joystick only, 0-255)
    uint8_t analog_x;   // Stick X
    uint8_t analog_y;   // Stick Y
    uint8_t analog_z;   // Twist/Rudder
    uint8_t throttle;   // Throttle

    // Mouse (relative motion)
    int8_t mouse_dx;
    int8_t mouse_dy;
    bool mouse_left;
    bool mouse_right;
    bool mouse_middle;

    // Joystick extras
    bool fire;          // Trigger/Fire button

    // Raw report data for debugging
    uint8_t raw_report[9];
    uint8_t raw_report_size;
} tdo_controller_t;

// ============================================================================
// PUBLIC API
// ============================================================================

// Initialize 3DO host driver with default pins
void tdo_host_init(void);

// Initialize with custom pin configuration
void tdo_host_init_pins(uint8_t clk_pin, uint8_t data_pin);

// Poll all controllers and submit events to router
// Call this regularly from main loop
void tdo_host_task(void);

// Get detected device type for a slot
// Returns TDO_DEVICE_NONE if no controller
tdo_device_type_t tdo_host_get_device_type(uint8_t slot);

// Get controller state (for direct access without router)
const tdo_controller_t* tdo_host_get_controller(uint8_t slot);

// Check if any controller is connected
bool tdo_host_is_connected(void);

// Get number of detected controllers
uint8_t tdo_host_get_controller_count(void);

#endif // TDO_HOST_H
