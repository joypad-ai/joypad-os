// platform_gpio_rp2040.c - RP2040 GPIO/ADC implementation
// SPDX-License-Identifier: Apache-2.0

#include "platform/platform_gpio.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"

// NUM_BANK0_GPIOS and ADC_BASE_PIN come from the SDK's per-chip
// hardware/platform_defs.h: 30/26 on RP2040 and RP2350A, 48/40 on RP2350B.
// Reading them here keeps the pin menus right if a target changes chips.
uint8_t platform_gpio_pin_count(void) {
    return (uint8_t)NUM_BANK0_GPIOS;
}

bool platform_gpio_pin_usable(uint8_t pin) {
    // Every bank 0 GPIO works as a digital input. Pins consumed by a specific
    // board's peripherals are a build-time concern (board headers), not
    // something the chip can report at runtime.
    return pin < NUM_BANK0_GPIOS;
}

uint8_t platform_adc_channel_count(void) {
    return 4;  // pad_read_adc() centres anything above channel 3
}

int8_t platform_adc_channel_gpio(uint8_t channel) {
    if (channel >= platform_adc_channel_count()) return -1;
    return (int8_t)(ADC_BASE_PIN + channel);
}

void platform_gpio_init_input(uint8_t pin, bool pull_up) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_IN);
    if (pull_up) {
        gpio_pull_up(pin);
    } else {
        gpio_pull_down(pin);
    }
}

bool platform_gpio_get(uint8_t pin) {
    return gpio_get(pin);
}

void platform_gpio_init_output(uint8_t pin) {
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, 0);
}

void platform_gpio_put(uint8_t pin, bool on) {
    gpio_put(pin, on);
}

void platform_adc_init(void) {
    adc_init();
}

void platform_adc_init_channel(uint8_t channel) {
    if (channel <= 3) {
        // ADC_BASE_PIN, not a literal 26: RP2350B puts ADC0 on GPIO 40. No
        // target compiles pad input for that chip today, so this is latent.
        adc_gpio_init(ADC_BASE_PIN + channel);
    }
}

uint16_t platform_adc_read(uint8_t channel) {
    if (channel > 3) return 2048;  // center
    adc_select_input(channel);
    return adc_read();
}
