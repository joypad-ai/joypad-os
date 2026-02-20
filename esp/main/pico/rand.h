// pico/rand.h shim for ESP32
// Provides get_rand_32() using ESP-IDF's hardware RNG.

#ifndef PICO_RAND_H_SHIM
#define PICO_RAND_H_SHIM

#include "esp_random.h"

static inline uint32_t get_rand_32(void)
{
    return esp_random();
}

#endif
