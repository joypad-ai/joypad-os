// ws2812.h - NeoPixel LED Control
//
// Controls WS2812 RGB LED for status indication.

#ifndef WS2812_H
#define WS2812_H

#include <stdint.h>
#include <stdbool.h>

// Initialize NeoPixel LED
void neopixel_init(void);

// Update NeoPixel LED pattern based on player count
// pat: number of connected players (0 = no players, shows idle pattern)
void neopixel_task(int pat);

// Trigger profile indicator blink pattern
// profile_index: 0-3 (blinks profile_index + 1 times)
void neopixel_indicate_profile(uint8_t profile_index);

// Check if profile indicator is currently active
bool neopixel_is_indicating(void);

#endif // WS2812_H
