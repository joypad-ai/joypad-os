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

// Number of pin ids in this platform's GPIO numbering space.
// RP2040/RP2350A: 30. RP2350B: 48. nRF52840: 48 (P0.00-P0.31 = 0-31,
// P1.00-P1.15 = 32-47). ESP32: SOC_GPIO_PIN_COUNT.
// Not every id in the range is usable — see platform_gpio_pin_usable().
uint8_t platform_gpio_pin_count(void);

// True if `pin` can be used as a general-purpose digital input on this board.
// A pin can be inside the numbering space and still be unusable: the nRF52840
// ties P0.00/P0.01 to the LFXO crystal, and ESP32 strapping/flash pins are
// reserved. PAD.CONFIG.PINS reports the result so the config tool only offers
// pins the board actually has.
bool platform_gpio_pin_usable(uint8_t pin);

// Initialize a GPIO pin as digital input with pull-up or pull-down
void platform_gpio_init_input(uint8_t pin, bool pull_up);

// Read digital state of a GPIO pin (true = high)
bool platform_gpio_get(uint8_t pin);

// Initialize a GPIO pin as digital output, drive low
void platform_gpio_init_output(uint8_t pin);

// Drive output pin (true = high, false = low)
void platform_gpio_put(uint8_t pin, bool on);

// Number of analog channels selectable as a stick or trigger source.
// Capped by pad_read_adc() in pad_input.c, which returns a centred axis for
// any channel above 3 — so this is 4 everywhere today even on parts with more
// SAADC/ADC inputs. Raise both together or the extra channels read as centred.
uint8_t platform_adc_channel_count(void);

// GPIO backing an ADC channel, or -1 when the platform does not number its
// analog inputs as GPIOs. RP2040/RP2350A: 26-29. RP2350B: 40-43. nRF52840 and
// ESP32 map analog inputs through devicetree/SoC tables rather than a fixed
// GPIO offset, so they return -1 and the config tool omits the GPIO hint.
int8_t platform_adc_channel_gpio(uint8_t channel);

// Initialize ADC subsystem (call once before any ADC reads)
void platform_adc_init(void);

// Initialize a specific ADC channel pin (channel 0-3 → GPIO 26-29 on RP2040)
void platform_adc_init_channel(uint8_t channel);

// Read ADC channel, returns 12-bit value (0-4095)
uint16_t platform_adc_read(uint8_t channel);

#endif // PLATFORM_GPIO_H
