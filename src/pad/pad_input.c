// pad_input.c - Pad Input Interface Implementation
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Reads buttons and analog sticks wired to GPIO pins or I2C expanders.
// Each registered config creates a controller input source.

#include "pad_input.h"
#include "pad_config_flash.h"
#include "core/buttons.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "platform/platform_gpio.h"
#include <stdio.h>
#include <string.h>

// I2C expander support (RP2040-only — uses pico-sdk I2C directly)
#if !defined(PLATFORM_ESP32) && !defined(PLATFORM_NRF)
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#define HAS_I2C_EXPANDER 1
#endif

#ifdef SENSOR_JOYWING
#include "drivers/joywing/joywing_input.h"
#endif

// ============================================================================
// I2C I/O EXPANDER REGISTERS (PCA9555/TCA9555 compatible)
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

#ifdef HAS_I2C_EXPANDER
// I2C I/O expander registers and state
#define I2C_IO_REG_INPUT     0x00
#define I2C_IO_REG_OUTPUT    0x02
#define I2C_IO_REG_POLARITY  0x04
#define I2C_IO_REG_CONFIG    0x06
#define I2C_IO_REG_PULLUP    0x46
#define I2C_FREQ 400000

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
#endif // HAS_I2C_EXPANDER

// ============================================================================
// GPIO HELPERS
// ============================================================================

// Initialize a single GPIO pin as input with appropriate pull
static void pad_init_button_pin(int16_t pin, bool active_high) {
    // Only initialize direct GPIO pins (0-29 on RP2040, 0-48 on ESP32)
    if (pin < 0 || pin > 48) return;
    // I2C expander pins (100+) are not direct GPIO
    if (pin >= 100) return;

    // Pull opposite to active state
    platform_gpio_init_input(pin, !active_high);
}

// Read a button pin and return true if pressed
// Supports direct GPIO (0-29) and I2C expanders (100-115, 200-215)
static bool pad_read_button(int16_t pin, bool active_high) {
    if (pin < 0) return false;

    bool state;

    if (pin < 100) {
        // Direct GPIO
        state = platform_gpio_get(pin);
    }
#ifdef HAS_I2C_EXPANDER
    else if (pin < 200) {
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
#else
    else { return false; }
#endif

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

    uint16_t raw = platform_adc_read(channel);  // 12-bit: 0-4095

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

#ifdef HAS_I2C_EXPANDER
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
#endif // HAS_I2C_EXPANDER

// Initialize GPIO pins for a device config
static void pad_init_device_pins(const pad_device_config_t* config) {
    if (!config) return;

    bool ah = config->active_high;

    // Initialize I2C if this config uses expanders
#ifdef HAS_I2C_EXPANDER
    if (config_uses_i2c(config)) {
        i2c_expander_init(config->i2c_sda, config->i2c_scl);
    }
#endif

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

    // Initialize toggle switch pins
    for (int t = 0; t < 2; t++) {
        int16_t pin = config->toggle[t].pin;
        if (pin >= 0 && pin <= 48) {
            platform_gpio_init_input(pin, config->toggle[t].invert);
            printf("[pad] Toggle %d on GPIO %d (func=%d%s)\n",
                   t, pin, config->toggle[t].function,
                   config->toggle[t].invert ? ", inverted" : "");
        }
    }

    // Initialize ADC if any analog inputs are used
    bool has_analog = (config->adc_lx >= 0 || config->adc_ly >= 0 ||
                       config->adc_rx >= 0 || config->adc_ry >= 0 ||
                       config->adc_lt >= 0 || config->adc_rt >= 0);

    if (has_analog && !adc_initialized) {
        platform_adc_init();
        adc_initialized = true;
    }

    // Initialize ADC channels
    if (config->adc_lx >= 0 && config->adc_lx <= 3) platform_adc_init_channel(config->adc_lx);
    if (config->adc_ly >= 0 && config->adc_ly <= 3) platform_adc_init_channel(config->adc_ly);
    if (config->adc_rx >= 0 && config->adc_rx <= 3) platform_adc_init_channel(config->adc_rx);
    if (config->adc_ry >= 0 && config->adc_ry <= 3) platform_adc_init_channel(config->adc_ry);
    if (config->adc_lt >= 0 && config->adc_lt <= 3) platform_adc_init_channel(config->adc_lt);
    if (config->adc_rt >= 0 && config->adc_rt <= 3) platform_adc_init_channel(config->adc_rt);

    printf("[pad] Initialized device: %s (active_%s%s)\n",
           config->name, ah ? "high" : "low",
#ifdef HAS_I2C_EXPANDER
           config_uses_i2c(config) ? ", I2C" :
#endif
           "");
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
    if (pad_read_button(config->a3, ah)) buttons |= JP_BUTTON_A3;
    if (pad_read_button(config->a4, ah)) buttons |= JP_BUTTON_A4;

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
    // Physical toggle switches can override the software d-pad mode
    // =================================================================
    dpad_mode_t effective_mode = dpad_mode;

    // Check toggle switches for d-pad remap functions
    for (int t = 0; t < 2; t++) {
        int16_t pin = config->toggle[t].pin;
        if (pin < 0 || pin > 48) continue;
        if (config->toggle[t].function == 0) continue;  // No function assigned

        bool toggle_state = platform_gpio_get(pin);
        if (config->toggle[t].invert) toggle_state = !toggle_state;
        if (!toggle_state) continue;  // Toggle not active

        switch (config->toggle[t].function) {
            case PAD_TOGGLE_FUNC_DPAD_LSTICK:
                effective_mode = DPAD_MODE_LEFT_STICK;
                break;
            case PAD_TOGGLE_FUNC_DPAD_RSTICK:
                effective_mode = DPAD_MODE_RIGHT_STICK;
                break;
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

    // Read analog triggers (no deadzone — 0=released, 255=full)
    if (config->adc_lt >= 0) {
        event->analog[ANALOG_L2] = pad_read_adc(config->adc_lt, false);
    }
    if (config->adc_rt >= 0) {
        event->analog[ANALOG_R2] = pad_read_adc(config->adc_rt, false);
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
    pad_events[index].transport = INPUT_TRANSPORT_GPIO;

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
#ifdef HAS_I2C_EXPANDER
    if (i2c_initialized) {
        i2c_expander_update_cache();
    }
#endif

    // Poll all registered devices
    for (uint8_t i = 0; i < pad_device_count; i++) {
        pad_poll_device(i);

#ifdef SENSOR_JOYWING
        // Merge JoyWing data into pad event (combined custom controller)
        const input_event_t* jw = joywing_get_event();
        if (jw) {
            pad_events[i].buttons |= jw->buttons;
            // JoyWing analog overwrites pad analog (JoyWing has priority for sticks)
            if (jw->analog[ANALOG_LX] != 128 || jw->analog[ANALOG_LY] != 128) {
                pad_events[i].analog[ANALOG_LX] = jw->analog[ANALOG_LX];
                pad_events[i].analog[ANALOG_LY] = jw->analog[ANALOG_LY];
            }
            if (jw->analog[ANALOG_RX] != 128 || jw->analog[ANALOG_RY] != 128) {
                pad_events[i].analog[ANALOG_RX] = jw->analog[ANALOG_RX];
                pad_events[i].analog[ANALOG_RY] = jw->analog[ANALOG_RY];
            }
        }
#endif

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
