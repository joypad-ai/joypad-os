// leds.c - LED Subsystem
//
// Unified LED control for status indication.

#include "leds.h"
#include "neopixel/ws2812.h"
#include "core/services/players/manager.h"

void leds_init(void)
{
    neopixel_init();
}

void leds_task(void)
{
    neopixel_task(playersCount);
}

void leds_indicate_profile(uint8_t profile_index)
{
    neopixel_indicate_profile(profile_index);
}

bool leds_is_indicating(void)
{
    return neopixel_is_indicating();
}
