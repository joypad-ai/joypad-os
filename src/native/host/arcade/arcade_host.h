// arcade_host.h - Native Arcade Controller Host Driver
//
// Polls native Arcade controllers and submits input events to the router.
// Supports Arcade controllers or sticks 4-8 buttons.

#ifndef ARCADE_HOST_H
#define ARCADE_HOST_H

#include <stdint.h>
#include <stdbool.h>
#include "native/host/host_interface.h"
#include "core/input_interface.h"

// ============================================================================
// CONFIGURATION
// ============================================================================

// Default GPIO pins for Arcade controller in -1 to define in apps
#ifndef ARCADE_PIN_DU
#define ARCADE_PIN_DU  -1   // Dpad Up
#endif

#ifndef ARCADE_PIN_DD
#define ARCADE_PIN_DD  -1   // Dpad Down
#endif

#ifndef ARCADE_PIN_DR
#define ARCADE_PIN_DR  -1   // Dpad Right
#endif

#ifndef ARCADE_PIN_DL
#define ARCADE_PIN_DL  -1   // Dpad Left
#endif

#ifndef ARCADE_PIN_P1
#define ARCADE_PIN_P1  -1
#endif

#ifndef ARCADE_PIN_P2
#define ARCADE_PIN_P2  -1
#endif

#ifndef ARCADE_PIN_P3
#define ARCADE_PIN_P3  -1
#endif

#ifndef ARCADE_PIN_P4
#define ARCADE_PIN_P4  -1
#endif

#ifndef ARCADE_PIN_K1
#define ARCADE_PIN_K1  -1
#endif

#ifndef ARCADE_PIN_K2
#define ARCADE_PIN_K2  -1
#endif

#ifndef ARCADE_PIN_K3
#define ARCADE_PIN_K3  -1
#endif

#ifndef ARCADE_PIN_K4
#define ARCADE_PIN_K4  -1
#endif

#ifndef ARCADE_PIN_S1
#define ARCADE_PIN_S1  -1   // Coin
#endif

#ifndef ARCADE_PIN_S2
#define ARCADE_PIN_S2  -1   // Start
#endif

#ifndef ARCADE_PIN_A1
#define ARCADE_PIN_A1  -1
#endif

#ifndef ARCADE_PIN_A2
#define ARCADE_PIN_A2  -1
#endif

#ifndef ARCADE_PIN_L3
#define ARCADE_PIN_L3  -1
#endif

#ifndef ARCADE_PIN_R3
#define ARCADE_PIN_R3  -1
#endif

#ifndef GET_BIT_MASK
#define GET_BIT_MASK(p) ((p) < 0 ? 0u : (1u << (uint8_t)(p)))
#endif

// Maximum number of Arcade ports
#define ARCADE_MAX_PORTS 1

// ============================================================================
// CONTROLLER STATE
// ============================================================================

// Device types
#define ARCADEPAD_NONE       -1
#define ARCADEPAD_CONTROLLER  0

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
    uint32_t mask_du;
    uint32_t mask_dd;
    uint32_t mask_dl;
    uint32_t mask_dr;

    uint32_t pin_mask;

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

    // D-pad
    bool dpad_up;
    bool dpad_down;
    bool dpad_left;
    bool dpad_right;

    // Debug/internal
    uint32_t last_read;
} arcade_controller_t;

// ============================================================================
// PUBLIC API
// ============================================================================

// Initialize Arcade host driver
// Sets up GPIO pins
void arcade_host_init(void);

// Initialize with custom pin configuration
void arcade_host_init_pins(uint8_t du, uint8_t dd, uint8_t dr, uint8_t dl,
                           uint8_t p1, uint8_t p2, uint8_t p3, uint8_t p4,
                           uint8_t k1, uint8_t k2, uint8_t k3, uint8_t k4,
                           uint8_t s1, uint8_t s2, uint8_t a1, uint8_t a2,
                           uint8_t l3, uint8_t r3);

// Poll Arcade controllers and submit events to router
// Call this regularly from main loop (typically from app's task function)
void arcade_host_task(void);

// Get detected device type for a port
// Returns: -1=none, 0=ARCADE controller
int8_t arcade_host_get_device_type(uint8_t port);

// Check if any Arcade controller is connected
bool arcade_host_is_connected(void);

// ============================================================================
// HOST INTERFACE
// ============================================================================

// Arcade host interface (implements HostInterface pattern)
extern const HostInterface arcade_host_interface;

// Arcade input interface (implements InputInterface pattern for app declaration)
extern const InputInterface arcade_input_interface;

#endif // ARCADE_HOST_H
