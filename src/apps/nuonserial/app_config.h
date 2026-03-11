// Nuon Serial Adapter LED Configuration
// Minimal - just needs defaults for ws2812.c

#ifndef CONSOLE_LED_CONFIG_H
#define CONSOLE_LED_CONFIG_H

// Default LED color - Orange (serial/config mode indicator)
#define LED_P1_R 64
#define LED_P1_G 32
#define LED_P1_B 0
#define LED_P1_PATTERN 0

#define LED_DEFAULT_R 64
#define LED_DEFAULT_G 32
#define LED_DEFAULT_B 0
#define LED_DEFAULT_PATTERN 0

// NeoPixel pattern - solid orange
#define NEOPIXEL_PATTERN_0 pattern_orange
#define NEOPIXEL_PATTERN_1 pattern_orange
#define NEOPIXEL_PATTERN_2 pattern_orange
#define NEOPIXEL_PATTERN_3 pattern_orange
#define NEOPIXEL_PATTERN_4 pattern_orange
#define NEOPIXEL_PATTERN_5 pattern_orange

#endif
