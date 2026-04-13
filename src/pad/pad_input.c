// pad_input.c - Pad Input Interface Implementation
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Reads buttons and analog sticks wired to GPIO pins or I2C expanders.
// Each registered config creates a controller input source.

#include "pad_input.h"
#include "core/buttons.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// I2C I/O EXPANDER REGISTERS (PCA9555/TCA9555 compatible)
// ============================================================================

#define I2C_IO_REG_INPUT     0x00  // Input port register
#define I2C_IO_REG_OUTPUT    0x02  // Output port register
#define I2C_IO_REG_POLARITY  0x04  // Polarity inversion register
#define I2C_IO_REG_CONFIG    0x06  // Configuration register
#define I2C_IO_REG_PULLUP    0x46  // Pull-up resistor register (TCA9555 only)

#define I2C_FREQ 400000  // 400kHz

// ============================================================================
// INTERNAL STATE
// ============================================================================

// Registered device configurations
static const pad_device_config_t* pad_devices[PAD_MAX_DEVICES];
static uint8_t pad_device_count = 0;

// Current input state per device
static input_event_t pad_events[PAD_MAX_DEVICES];

// Debounce state (simple: require 2 consecutive reads)
static uint32_t pad_prev_buttons[PAD_MAX_DEVICES];

// D-pad mode (software, set via S1+S2+direction hotkeys)
typedef enum {
    DPAD_MODE_DPAD,        // D-pad → d-pad buttons (default)
    DPAD_MODE_LEFT_STICK,  // D-pad → left analog stick
    DPAD_MODE_RIGHT_STICK, // D-pad → right analog stick
} dpad_mode_t;

static dpad_mode_t dpad_mode = DPAD_MODE_DPAD;

// S1+S2 combo state
static bool prev_s1s2_held = false;
static bool combo_used = false;

// ADC initialized flag
static bool adc_initialized = false;

// I2C initialized flag
static bool i2c_initialized = false;

// I2C expander cached state (16 bits per expander)
static uint16_t i2c_expander_cache[2] = {0, 0};

// ============================================================================
// I2C HELPERS
// ============================================================================

// Initialize I2C bus for expanders
static void i2c_expander_init(int8_t sda_pin, int8_t scl_pin) {
    if (i2c_initialized) return;
    if (sda_pin < 0 || scl_pin < 0) return;

    printf("[pad] Initializing I2C on SDA=%d, SCL=%d\n", sda_pin, scl_pin);

    i2c_init(i2c1, I2C_FREQ);
    gpio_set_function(sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(sda_pin);
    gpio_pull_up(scl_pin);

    // Configure expanders: set polarity inversion and enable pull-ups
    uint8_t polarity_data[] = {I2C_IO_REG_POLARITY, 0xFF, 0xFF};
    uint8_t pullup_data[] = {I2C_IO_REG_PULLUP, 0xFF, 0xFF};

    // Try to configure expander 0
    if (i2c_write_blocking(i2c1, PAD_I2C_EXPANDER_ADDR_0, polarity_data, 3, false) >= 0) {
        i2c_write_blocking(i2c1, PAD_I2C_EXPANDER_ADDR_0, pullup_data, 3, false);
        printf("[pad] I2C expander 0 (0x%02X) configured\n", PAD_I2C_EXPANDER_ADDR_0);
    }

    // Try to configure expander 1
    if (i2c_write_blocking(i2c1, PAD_I2C_EXPANDER_ADDR_1, polarity_data, 3, false) >= 0) {
        i2c_write_blocking(i2c1, PAD_I2C_EXPANDER_ADDR_1, pullup_data, 3, false);
        printf("[pad] I2C expander 1 (0x%02X) configured\n", PAD_I2C_EXPANDER_ADDR_1);
    }

    i2c_initialized = true;
}

// Read 16 bits from I2C expander
static uint16_t i2c_expander_read(uint8_t addr) {
    uint8_t reg = I2C_IO_REG_INPUT;
    uint8_t buf[2] = {0, 0};

    i2c_write_blocking(i2c1, addr, &reg, 1, true);
    i2c_read_blocking(i2c1, addr, buf, 2, false);

    return (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
}

// Update I2C expander cache (call once per poll cycle)
static void i2c_expander_update_cache(void) {
    if (!i2c_initialized) return;

    i2c_expander_cache[0] = i2c_expander_read(PAD_I2C_EXPANDER_ADDR_0);
    i2c_expander_cache[1] = i2c_expander_read(PAD_I2C_EXPANDER_ADDR_1);
}

// ============================================================================
// GPIO HELPERS
// ============================================================================

// Initialize a single GPIO pin as input with appropriate pull
static void pad_init_button_pin(int16_t pin, bool active_high) {
    // Only initialize direct GPIO pins (0-29)
    if (pin < 0 || pin > 29) return;

    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);

    // Pull opposite to active state
    if (active_high) {
        gpio_pull_down(pin);  // Active high: pull down, button connects to VCC
    } else {
        gpio_pull_up(pin);    // Active low: pull up, button connects to GND
    }
}

// Read a button pin and return true if pressed
// Supports direct GPIO (0-29) and I2C expanders (100-115, 200-215)
static bool pad_read_button(int16_t pin, bool active_high) {
    if (pin < 0) return false;

    bool state;

    if (pin < 100) {
        // Direct GPIO
        state = gpio_get(pin);
    } else if (pin < 200) {
        // I2C expander 0 (pins 100-115)
        uint8_t bit = pin - PAD_I2C_EXPANDER_0_BASE;
        if (bit > 15) return false;
        state = (i2c_expander_cache[0] >> bit) & 1;
    } else {
        // I2C expander 1 (pins 200-215)
        uint8_t bit = pin - PAD_I2C_EXPANDER_1_BASE;
        if (bit > 15) return false;
        state = (i2c_expander_cache[1] >> bit) & 1;
    }

    return active_high ? state : !state;
}

// Read ADC channel and return 0-255 value with proper scaling
// Most analog joysticks don't use full 0-3.3V range
// ADC range: 0-4095 (12-bit), adjust min/max based on actual joystick
#define ADC_STICK_MIN  1100  // Minimum ADC value at full deflection
#define ADC_STICK_MAX  3000  // Maximum ADC value at full deflection
#define ADC_STICK_CENTER 2048

static uint8_t pad_read_adc(int8_t channel, bool invert) {
    if (channel < 0 || channel > 3) return 128;  // Centered

    adc_select_input(channel);
    uint16_t raw = adc_read();  // 12-bit: 0-4095

    // Scale from joystick's actual range to full 0-255
    // Clamp to expected range first
    if (raw < ADC_STICK_MIN) raw = ADC_STICK_MIN;
    if (raw > ADC_STICK_MAX) raw = ADC_STICK_MAX;

    // Scale to 0-255
    uint32_t scaled = ((uint32_t)(raw - ADC_STICK_MIN) * 255) / (ADC_STICK_MAX - ADC_STICK_MIN);
    uint8_t value = (uint8_t)scaled;

    if (invert) {
        value = 255 - value;
    }

    return value;
}

// Apply deadzone to analog value (centered at 128)
static uint8_t apply_deadzone(uint8_t value, uint8_t deadzone) {
    int16_t centered = (int16_t)value - 128;

    if (centered > -deadzone && centered < deadzone) {
        return 128;  // In deadzone, return center
    }

    return value;
}

// Check if config uses I2C expanders
static bool config_uses_i2c(const pad_device_config_t* config) {
    return (config->dpad_up >= 100 || config->dpad_down >= 100 ||
            config->dpad_left >= 100 || config->dpad_right >= 100 ||
            config->b1 >= 100 || config->b2 >= 100 ||
            config->b3 >= 100 || config->b4 >= 100 ||
            config->l1 >= 100 || config->r1 >= 100 ||
            config->l2 >= 100 || config->r2 >= 100 ||
            config->s1 >= 100 || config->s2 >= 100 ||
            config->l3 >= 100 || config->r3 >= 100 ||
            config->a1 >= 100 || config->a2 >= 100 ||
            config->l4 >= 100 || config->r4 >= 100);
}

// Initialize GPIO pins for a device config
static void pad_init_device_pins(const pad_device_config_t* config) {
    if (!config) return;

    bool ah = config->active_high;

    // Initialize I2C if this config uses expanders
    if (config_uses_i2c(config)) {
        i2c_expander_init(config->i2c_sda, config->i2c_scl);
    }

    // Initialize direct GPIO button pins (I2C pins are handled by expander init)
    pad_init_button_pin(config->dpad_up, ah);
    pad_init_button_pin(config->dpad_down, ah);
    pad_init_button_pin(config->dpad_left, ah);
    pad_init_button_pin(config->dpad_right, ah);

    pad_init_button_pin(config->b1, ah);
    pad_init_button_pin(config->b2, ah);
    pad_init_button_pin(config->b3, ah);
    pad_init_button_pin(config->b4, ah);

    pad_init_button_pin(config->l1, ah);
    pad_init_button_pin(config->r1, ah);
    pad_init_button_pin(config->l2, ah);
    pad_init_button_pin(config->r2, ah);

    pad_init_button_pin(config->s1, ah);
    pad_init_button_pin(config->s2, ah);
    pad_init_button_pin(config->l3, ah);
    pad_init_button_pin(config->r3, ah);
    pad_init_button_pin(config->a1, ah);
    pad_init_button_pin(config->a2, ah);
    pad_init_button_pin(config->l4, ah);
    pad_init_button_pin(config->r4, ah);

    // Initialize toggle switch pin
    if (config->dpad_toggle >= 0 && config->dpad_toggle <= 29) {
        gpio_init(config->dpad_toggle);
        gpio_set_dir(config->dpad_toggle, GPIO_IN);
        if (config->dpad_toggle_invert) {
            gpio_pull_up(config->dpad_toggle);    // Inverted: switch connects to GND
        } else {
            gpio_pull_down(config->dpad_toggle);  // Normal: switch connects to VCC
        }
        printf("[pad] D-pad toggle switch on GPIO %d%s\n", config->dpad_toggle,
               config->dpad_toggle_invert ? " (inverted)" : "");
    }

    // Initialize ADC if any analog inputs are used
    bool has_analog = (config->adc_lx >= 0 || config->adc_ly >= 0 ||
                       config->adc_rx >= 0 || config->adc_ry >= 0);

    if (has_analog && !adc_initialized) {
        adc_init();
        adc_initialized = true;
    }

    // Initialize ADC pins (GPIO 26-29 are ADC0-3)
    if (config->adc_lx >= 0 && config->adc_lx <= 3) {
        adc_gpio_init(26 + config->adc_lx);
    }
    if (config->adc_ly >= 0 && config->adc_ly <= 3) {
        adc_gpio_init(26 + config->adc_ly);
    }
    if (config->adc_rx >= 0 && config->adc_rx <= 3) {
        adc_gpio_init(26 + config->adc_rx);
    }
    if (config->adc_ry >= 0 && config->adc_ry <= 3) {
        adc_gpio_init(26 + config->adc_ry);
    }

    printf("[pad] Initialized device: %s (active_%s%s)\n",
           config->name, ah ? "high" : "low",
           config_uses_i2c(config) ? ", I2C" : "");
}

// Poll a single device and update its input event
static void pad_poll_device(uint8_t device_index) {
    if (device_index >= pad_device_count) return;

    const pad_device_config_t* config = pad_devices[device_index];
    input_event_t* event = &pad_events[device_index];
    bool ah = config->active_high;

    // Read buttons into bitmap
    uint32_t buttons = 0;

    // Always read D-pad as digital buttons first
    if (pad_read_button(config->dpad_up, ah))    buttons |= JP_BUTTON_DU;
    if (pad_read_button(config->dpad_down, ah))  buttons |= JP_BUTTON_DD;
    if (pad_read_button(config->dpad_left, ah))  buttons |= JP_BUTTON_DL;
    if (pad_read_button(config->dpad_right, ah)) buttons |= JP_BUTTON_DR;

    // Face buttons
    if (pad_read_button(config->b1, ah)) buttons |= JP_BUTTON_B1;
    if (pad_read_button(config->b2, ah)) buttons |= JP_BUTTON_B2;
    if (pad_read_button(config->b3, ah)) buttons |= JP_BUTTON_B3;
    if (pad_read_button(config->b4, ah)) buttons |= JP_BUTTON_B4;

    // Shoulders/triggers
    if (pad_read_button(config->l1, ah)) buttons |= JP_BUTTON_L1;
    if (pad_read_button(config->r1, ah)) buttons |= JP_BUTTON_R1;
    if (pad_read_button(config->l2, ah)) buttons |= JP_BUTTON_L2;
    if (pad_read_button(config->r2, ah)) buttons |= JP_BUTTON_R2;

    // Meta buttons
    if (pad_read_button(config->s1, ah)) buttons |= JP_BUTTON_S1;
    if (pad_read_button(config->s2, ah)) buttons |= JP_BUTTON_S2;
    if (pad_read_button(config->l3, ah)) buttons |= JP_BUTTON_L3;
    if (pad_read_button(config->r3, ah)) buttons |= JP_BUTTON_R3;
    if (pad_read_button(config->a1, ah)) buttons |= JP_BUTTON_A1;
    if (pad_read_button(config->a2, ah)) buttons |= JP_BUTTON_A2;

    // Extra buttons (L4/R4 mapped to L2/R2 digital for now)
    // TODO: Add proper L4/R4 button defines if needed

    // Simple debounce: only update if same as previous read
    // (This filters out single-sample glitches)
    if (buttons == pad_prev_buttons[device_index]) {
        event->buttons = buttons;
    }
    pad_prev_buttons[device_index] = buttons;

    // =================================================================
    // S1+S2 combo detection (Home button + d-pad mode switching)
    // S1+S2 alone: inject A1 (Home) continuously
    // S1+S2 + Down:  d-pad mode (default)
    // S1+S2 + Left:  d-pad → left analog stick
    // S1+S2 + Right: d-pad → right analog stick
    // =================================================================
    bool s1s2_held = (event->buttons & (JP_BUTTON_S1 | JP_BUTTON_S2)) == (JP_BUTTON_S1 | JP_BUTTON_S2);

    if (s1s2_held) {
        if (!prev_s1s2_held) {
            combo_used = false;
        }

        uint32_t dpad_bits = event->buttons & (JP_BUTTON_DU | JP_BUTTON_DD | JP_BUTTON_DL | JP_BUTTON_DR);
        uint32_t other_bits = event->buttons & ~(JP_BUTTON_S1 | JP_BUTTON_S2 | JP_BUTTON_DU | JP_BUTTON_DD | JP_BUTTON_DL | JP_BUTTON_DR);

        // Check for d-pad mode switch (only fire once per hold)
        if (!combo_used && dpad_bits) {
            if (dpad_bits == JP_BUTTON_DD) {
                dpad_mode = DPAD_MODE_DPAD;
                printf("[pad] D-pad mode: D-PAD (default)\n");
                combo_used = true;
            } else if (dpad_bits == JP_BUTTON_DL) {
                dpad_mode = DPAD_MODE_LEFT_STICK;
                printf("[pad] D-pad mode: LEFT STICK\n");
                combo_used = true;
            } else if (dpad_bits == JP_BUTTON_DR) {
                dpad_mode = DPAD_MODE_RIGHT_STICK;
                printf("[pad] D-pad mode: RIGHT STICK\n");
                combo_used = true;
            }
        }

        prev_s1s2_held = true;

        if (!combo_used && !other_bits) {
            // S1+S2 alone → inject Home
            event->buttons = JP_BUTTON_A1;
        } else {
            // Direction combo or other buttons — suppress all output
            event->buttons = 0;
        }
    } else if (prev_s1s2_held) {
        prev_s1s2_held = false;
    }

    // =================================================================
    // Apply d-pad mode (remap d-pad to analog stick if needed)
    // Physical toggle switch overrides software mode to right stick
    // =================================================================
    dpad_mode_t effective_mode = dpad_mode;

    // Physical toggle switch: when HIGH, remap d-pad to analog stick
    // If config has a left analog stick → right stick (V2: left stick is physical)
    // If config has no analog sticks   → left stick  (V1: d-pad doubles as stick)
    if (config->dpad_toggle >= 0 && config->dpad_toggle <= 29) {
        bool toggle_state = gpio_get(config->dpad_toggle);
        if (config->dpad_toggle_invert) toggle_state = !toggle_state;
        if (toggle_state) {
            bool has_left_stick = (config->adc_lx >= 0 || config->adc_ly >= 0);
            effective_mode = has_left_stick ? DPAD_MODE_RIGHT_STICK : DPAD_MODE_LEFT_STICK;
        }
    }

    static dpad_mode_t prev_effective_mode = DPAD_MODE_DPAD;
    if (effective_mode != DPAD_MODE_DPAD) {
        uint32_t dpad_bits = event->buttons & (JP_BUTTON_DU | JP_BUTTON_DD | JP_BUTTON_DL | JP_BUTTON_DR);
        event->buttons &= ~(JP_BUTTON_DU | JP_BUTTON_DD | JP_BUTTON_DL | JP_BUTTON_DR);

        uint8_t ax = 128, ay = 128;
        if (dpad_bits & JP_BUTTON_DL) ax = 0;
        else if (dpad_bits & JP_BUTTON_DR) ax = 255;
        if (dpad_bits & JP_BUTTON_DU) ay = 0;
        else if (dpad_bits & JP_BUTTON_DD) ay = 255;

        if (effective_mode == DPAD_MODE_LEFT_STICK) {
            event->analog[ANALOG_LX] = ax;
            event->analog[ANALOG_LY] = ay;
        } else {
            event->analog[ANALOG_RX] = ax;
            event->analog[ANALOG_RY] = ay;
        }
    } else if (prev_effective_mode != DPAD_MODE_DPAD) {
        // Switched back to d-pad mode — reset analog axes to center
        if (prev_effective_mode == DPAD_MODE_LEFT_STICK) {
            event->analog[ANALOG_LX] = 128;
            event->analog[ANALOG_LY] = 128;
        } else {
            event->analog[ANALOG_RX] = 128;
            event->analog[ANALOG_RY] = 128;
        }
    }
    prev_effective_mode = effective_mode;

    // Read analog sticks (ADC overrides dpad-to-stick if both present)
    uint8_t dz = config->deadzone;

    if (config->adc_lx >= 0) {
        event->analog[ANALOG_LX] = apply_deadzone(
            pad_read_adc(config->adc_lx, config->invert_lx), dz);
    }
    if (config->adc_ly >= 0) {
        event->analog[ANALOG_LY] = apply_deadzone(
            pad_read_adc(config->adc_ly, config->invert_ly), dz);
    }
    if (config->adc_rx >= 0) {
        event->analog[ANALOG_RX] = apply_deadzone(
            pad_read_adc(config->adc_rx, config->invert_rx), dz);
    }
    if (config->adc_ry >= 0) {
        event->analog[ANALOG_RY] = apply_deadzone(
            pad_read_adc(config->adc_ry, config->invert_ry), dz);
    }
}

// ============================================================================
// PUBLIC API
// ============================================================================

int pad_input_add_device(const pad_device_config_t* config) {
    if (!config || pad_device_count >= PAD_MAX_DEVICES) {
        return -1;
    }

    uint8_t index = pad_device_count;
    pad_devices[index] = config;

    // Initialize input event for this device
    init_input_event(&pad_events[index]);
    pad_events[index].dev_addr = 0xF0 + index;  // Virtual address for pad devices
    pad_events[index].instance = index;
    pad_events[index].type = INPUT_TYPE_GAMEPAD;

    pad_prev_buttons[index] = 0;

    pad_device_count++;

    return index;
}

void pad_input_clear_devices(void) {
    pad_device_count = 0;
    memset(pad_devices, 0, sizeof(pad_devices));
}

uint8_t pad_input_get_device_count(void) {
    return pad_device_count;
}

const input_event_t* pad_input_get_event(uint8_t device_index) {
    if (device_index >= pad_device_count) return NULL;
    return &pad_events[device_index];
}

const pad_device_config_t* pad_input_get_config(uint8_t device_index) {
    if (device_index >= pad_device_count) return NULL;
    return pad_devices[device_index];
}

// ============================================================================
// INPUT INTERFACE IMPLEMENTATION
// ============================================================================

static void pad_input_init(void) {
    printf("[pad] Initializing pad input interface\n");

    // Initialize pins for all registered devices
    for (uint8_t i = 0; i < pad_device_count; i++) {
        pad_init_device_pins(pad_devices[i]);
    }

    printf("[pad] Initialized %d pad device(s)\n", pad_device_count);
}

static void pad_input_task(void) {
    // Update I2C expander cache once per cycle (more efficient than per-button)
    if (i2c_initialized) {
        i2c_expander_update_cache();
    }

    // Poll all registered devices
    for (uint8_t i = 0; i < pad_device_count; i++) {
        pad_poll_device(i);

        // Submit to router
        router_submit_input(&pad_events[i]);
    }
}

static bool pad_input_is_connected(void) {
    // Pad devices are always "connected" if configured
    return pad_device_count > 0;
}

static uint8_t pad_input_device_count(void) {
    return pad_device_count;
}

// Export interface
const InputInterface pad_input_interface = {
    .name = "Pad",
    .source = INPUT_SOURCE_GPIO,
    .init = pad_input_init,
    .task = pad_input_task,
    .is_connected = pad_input_is_connected,
    .get_device_count = pad_input_device_count,
};
