// leds.h - LED Subsystem
//
// Unified LED control for status indication.
// Currently wraps NeoPixel, but can expand to other LED types.

#ifndef LEDS_H
#define LEDS_H

#include <stdint.h>
#include <stdbool.h>

// Initialize LED subsystem
void leds_init(void);

// Update LED state (call from main loop)
void leds_task(void);

// Trigger profile indicator pattern
void leds_indicate_profile(uint8_t profile_index);

// Check if profile indicator is currently active
bool leds_is_indicating(void);

#endif // LEDS_H
