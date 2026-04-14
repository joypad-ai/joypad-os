// platform_gpio.h - Platform-agnostic GPIO and ADC interface
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Abstracts GPIO digital input and ADC analog reading across platforms.
// Used by pad_input.c for custom controller button/stick reading.

#ifndef PLATFORM_GPIO_H
#define PLATFORM_GPIO_H

#include <stdint.h>
#include <stdbool.h>

// Initialize a GPIO pin as digital input with pull-up or pull-down
void platform_gpio_init_input(uint8_t pin, bool pull_up);

// Read digital state of a GPIO pin (true = high)
bool platform_gpio_get(uint8_t pin);

// Initialize ADC subsystem (call once before any ADC reads)
void platform_adc_init(void);

// Initialize a specific ADC channel pin (channel 0-3 → GPIO 26-29 on RP2040)
void platform_adc_init_channel(uint8_t channel);

// Read ADC channel, returns 12-bit value (0-4095)
uint16_t platform_adc_read(uint8_t channel);

#endif // PLATFORM_GPIO_H
