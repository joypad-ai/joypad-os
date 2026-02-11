// gpio_device.h

#ifndef GPIO_DEVICE_H
#define GPIO_DEVICE_H

#include <stdint.h>

#include "tusb.h"
#include "core/buttons.h"
#include "core/uart.h"

// Define constants
#undef GPIO_MAX_PLAYERS
#define GPIO_MAX_PLAYERS 2 // NEOGEO DB15 2 player

// ============================================================================
// HELPER MACROS FOR CONFIG DEFINITIONS
// ============================================================================

// GPIO
#ifndef GPIO_MASK 
#define GPIO_MASK(pin) ((pin < 0) ? 0u : (1u << (pin)))
#endif

#ifndef GPIO_DISABLED 
#define GPIO_DISABLED (-1)
#endif

// Initialize all pins to disabled
#define PORT_CONFIG_INIT { \
    .pin_du = GPIO_DISABLED, \
    .pin_dd = GPIO_DISABLED, \
    .pin_dl = GPIO_DISABLED, \
    .pin_dr = GPIO_DISABLED, \
    .pin_b1 = GPIO_DISABLED, \
    .pin_b2 = GPIO_DISABLED, \
    .pin_b3 = GPIO_DISABLED, \
    .pin_b4 = GPIO_DISABLED, \
    .pin_l1 = GPIO_DISABLED, \
    .pin_r1 = GPIO_DISABLED, \
    .pin_l2 = GPIO_DISABLED, \
    .pin_r2 = GPIO_DISABLED, \
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
// PIN ADDRESSING
// ============================================================================

// Pin values: 0-29 = direct GPIO
typedef struct {
    bool active_high;           // true = pressed when high, false = pressed when low

    // GPIO Masked Pins
    uint32_t mask_du;
    uint32_t mask_dd;
    uint32_t mask_dl;
    uint32_t mask_dr;
    uint32_t mask_b1;
    uint32_t mask_b2;
    uint32_t mask_b3;
    uint32_t mask_b4;
    uint32_t mask_l1;
    uint32_t mask_r1;
    uint32_t mask_l2;
    uint32_t mask_r2;
    uint32_t mask_s1;
    uint32_t mask_s2;
    uint32_t mask_a1;
    uint32_t mask_a2;
    uint32_t mask_l3;
    uint32_t mask_r3;
    uint32_t mask_l4;
    uint32_t mask_r4;

    uint32_t gpio_mask;

    // Debug/internal
    uint32_t last_read;
} gpio_device_port_t;

typedef struct {
    // GPIO Pins
    // D-pad
    uint8_t pin_du;
    uint8_t pin_dd;
    uint8_t pin_dl;
    uint8_t pin_dr;

    // Action Buttons
    uint8_t pin_b1;
    uint8_t pin_b2;
    uint8_t pin_b3;
    uint8_t pin_b4;
    uint8_t pin_l1;
    uint8_t pin_r1;
    uint8_t pin_l2;
    uint8_t pin_r2;

    // Meta Buttons

    uint8_t pin_s1;
    uint8_t pin_s2;
    uint8_t pin_a1;
    uint8_t pin_a2;

    // Extra Buttons
    uint8_t pin_l3;
    uint8_t pin_r3;
    uint8_t pin_l4;
    uint8_t pin_r4;
} gpio_device_config_t;

// Function declarations
void gpio_device_init(void);
void gpio_devide_task(void);
void __not_in_flash_func(core1_task)(void);
void gpio_device_init_pins(gpio_device_config_t* config, bool active_high);

#endif // GPIO_DEVICE_H
