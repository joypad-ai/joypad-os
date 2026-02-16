// arcade_host.h - Native Arcade Controller Host Driver
//
// Polls native Arcade controllers and submits input events to the router.
// Supports Arcade controllers or sticks 4-8 buttons.

#ifndef ARCADE_HOST_H
#define ARCADE_HOST_H

#include <stdint.h>
#include <stdbool.h>

#include "core/input_interface.h"

// ============================================================================
// HELPER MACROS FOR CONFIG DEFINITIONS
// ============================================================================

// GPIO
#ifndef GPIO_DISABLED
#define GPIO_DISABLED 0xFF
#endif

#ifndef GPIO_MASK
#define GPIO_MASK(pin) ((pin >= 30) ? 0u : (1u << (pin)))
#endif

// Initialize all pins to disabled
#define PORT_CONFIG_INIT { \
    .pin_du = GPIO_DISABLED, \
    .pin_dd = GPIO_DISABLED, \
    .pin_dl = GPIO_DISABLED, \
    .pin_dr = GPIO_DISABLED, \
    .pin_p1 = GPIO_DISABLED, \
    .pin_p2 = GPIO_DISABLED, \
    .pin_p3 = GPIO_DISABLED, \
    .pin_p4 = GPIO_DISABLED, \
    .pin_k1 = GPIO_DISABLED, \
    .pin_k2 = GPIO_DISABLED, \
    .pin_k3 = GPIO_DISABLED, \
    .pin_k4 = GPIO_DISABLED, \
    .pin_s1 = GPIO_DISABLED, \
    .pin_s2 = GPIO_DISABLED, \
    .pin_a1 = GPIO_DISABLED, \
    .pin_a2 = GPIO_DISABLED, \
    .pin_l3 = GPIO_DISABLED, \
    .pin_r3 = GPIO_DISABLED, \
    .pin_l4 = GPIO_DISABLED, \
    .pin_r4 = GPIO_DISABLED, \
}

// ============================================================================
// D-PAD MODE (S1+S2+direction hotkeys, GP2040-CE compatible)
// ============================================================================

typedef enum {
    DPAD_MODE_DPAD,        // D-pad → d-pad buttons (default)
    DPAD_MODE_LEFT_STICK,  // D-pad → left analog stick
    DPAD_MODE_RIGHT_STICK, // D-pad → right analog stick
} dpad_mode_t;

#define DPAD_MODE_DP_COMBO_MASK (JP_BUTTON_S1 | JP_BUTTON_S2 | JP_BUTTON_DD)
#define DPAD_MODE_LS_COMBO_MASK (JP_BUTTON_S1 | JP_BUTTON_S2 | JP_BUTTON_DL)
#define DPAD_MODE_RS_COMBO_MASK (JP_BUTTON_S1 | JP_BUTTON_S2 | JP_BUTTON_DR)
#define DPAD_MODE_HOLD_DURATION 2000

// ============================================================================
// CONTROLLER STATE
// ============================================================================

// Device types
#define ARCADEPAD_NONE       -1
#define ARCADEPAD_CONTROLLER  0

// Maximum number of Arcade ports
#define ARCADE_MAX_PORTS 1

typedef struct {
    // Device type (-1 = none, 0 = controller)
    int8_t type;

    // Pin Mask Configuration
    uint32_t mask_p1;
    uint32_t mask_p2;
    uint32_t mask_p3;
    uint32_t mask_p4;
    uint32_t mask_k1;
    uint32_t mask_k2;
    uint32_t mask_k3;
    uint32_t mask_k4;
    uint32_t mask_s1;
    uint32_t mask_s2;
    uint32_t mask_a1;
    uint32_t mask_a2;
    uint32_t mask_l3;
    uint32_t mask_r3;
    uint32_t mask_l4;
    uint32_t mask_r4;
    uint32_t mask_du;
    uint32_t mask_dd;
    uint32_t mask_dl;
    uint32_t mask_dr;

    uint32_t gpio_mask;

    // Digital buttons (active-high after parsing)
    bool button_p1;
    bool button_p2;
    bool button_p3;
    bool button_p4;
    bool button_k1;
    bool button_k2;
    bool button_k3;
    bool button_k4;

    bool button_s1;
    bool button_s2;

    bool button_a1;
    bool button_a2;

    bool button_l3;
    bool button_r3;
    bool button_l4;
    bool button_r4;

    // D-pad
    bool dpad_up;
    bool dpad_down;
    bool dpad_left;
    bool dpad_right;

    dpad_mode_t dpad_mode;

    // Debug/internal
    uint32_t last_read;
} arcade_controller_t;

typedef struct {
    // GPIO Pins
    // Action buttons
    uint8_t pin_p1;
    uint8_t pin_p2;
    uint8_t pin_p3;
    uint8_t pin_p4;
    uint8_t pin_k1;
    uint8_t pin_k2;
    uint8_t pin_k3;
    uint8_t pin_k4;

    // Systema buttons
    uint8_t pin_s1;
    uint8_t pin_s2;
    uint8_t pin_a1;
    uint8_t pin_a2;

    // Extra buttons
    uint8_t pin_l3;
    uint8_t pin_r3;
    uint8_t pin_l4;
    uint8_t pin_r4;

    // D-pad
    uint8_t pin_du;
    uint8_t pin_dd;
    uint8_t pin_dl;
    uint8_t pin_dr;

} arcade_config_t;

// ============================================================================
// CONFIGURATION
// ============================================================================

// Default GPIO pins for Arcade controller defined in apps

// ============================================================================
// PUBLIC API
// ============================================================================

// Initialize Arcade host driver
// Sets up GPIO pins
void arcade_host_init(void);

// Initialize with custom pin configuration
void arcade_host_init_pins(arcade_config_t* conf);

// Poll Arcade controllers and submit events to router
// Call this regularly from main loop (typically from app's task function)
void arcade_host_task(void);

// Get detected device type for a port
// Returns: -1=none, 0=ARCADE controller
int8_t arcade_host_get_device_type(uint8_t port);

// Check if any Arcade controller is connected
bool arcade_host_is_connected(void);

// ============================================================================
// INPUT INTERFACE
// ============================================================================

// Arcade input interface (implements InputInterface pattern for app declaration)
extern const InputInterface arcade_input_interface;

#endif // ARCADE_HOST_H
