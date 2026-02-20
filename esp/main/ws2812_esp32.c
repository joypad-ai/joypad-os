// ws2812_esp32.c - NeoPixel stub for ESP32
//
// Stub implementation of the NeoPixel API for ESP32 builds.
// Can be replaced with ESP-IDF RMT driver later.

#include "core/services/leds/neopixel/ws2812.h"
#include <stdio.h>

void neopixel_init(void)
{
    printf("[neopixel] ESP32 stub initialized (no LED)\n");
}

void neopixel_task(int pat)
{
    (void)pat;
}

void neopixel_indicate_profile(uint8_t profile_index)
{
    (void)profile_index;
}

bool neopixel_is_indicating(void)
{
    return false;
}

void neopixel_set_custom_colors(const uint8_t colors[][3], uint8_t count)
{
    (void)colors;
    (void)count;
}

bool neopixel_has_custom_colors(void)
{
    return false;
}

void neopixel_set_override_color(uint8_t r, uint8_t g, uint8_t b)
{
    (void)r; (void)g; (void)b;
}
