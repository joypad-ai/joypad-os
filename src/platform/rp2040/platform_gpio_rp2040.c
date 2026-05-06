// platform_gpio_rp2040.c - RP2040 GPIO/ADC implementation
// SPDX-License-Identifier: Apache-2.0

#include "platform/platform_gpio.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"

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
        adc_gpio_init(26 + channel);
    }
}

uint16_t platform_adc_read(uint8_t channel) {
    if (channel > 3) return 2048;  // center
    adc_select_input(channel);
    return adc_read();
}
