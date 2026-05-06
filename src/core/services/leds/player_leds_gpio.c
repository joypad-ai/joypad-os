// player_leds_gpio.c - 4-LED player indicator on raw GPIO pins
//
// See player_leds_gpio.h for the pin/bit mapping. Reads the 4-bit
// PLAYER_LEDS[] bitmap from feedback_state.led.pattern and writes each
// of the four configured GPIOs accordingly. Active-high.

#include "player_leds_gpio.h"
#include "platform/platform_gpio.h"
#include "core/services/players/feedback.h"

#include <stdbool.h>

static uint8_t pins[4] = {0, 0, 0, 0};
static bool initialized = false;

void player_leds_gpio_init(uint8_t pin1, uint8_t pin2,
                           uint8_t pin3, uint8_t pin4)
{
    pins[0] = pin1;
    pins[1] = pin2;
    pins[2] = pin3;
    pins[3] = pin4;

    for (int i = 0; i < 4; i++) {
        platform_gpio_init_output(pins[i]);
    }
    initialized = true;
}

void player_leds_gpio_task(void)
{
    if (!initialized) return;

    // Slot 0 only — for multi-player adapters that need this to follow
    // the active player, swap in active_player here. See plan doc.
    feedback_state_t* state = feedback_get_state(0);
    uint8_t pattern = state ? state->led.pattern : 0;

    for (int i = 0; i < 4; i++) {
        platform_gpio_put(pins[i], (pattern >> i) & 1);
    }
}
