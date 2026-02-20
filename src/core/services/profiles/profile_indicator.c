// profile_indicator.c - Profile switching feedback management
//
// Temporarily alters a player's feedback state (rumble/LED) to indicate
// profile switches. The actual feedback is applied by core/feedback.c
// which device drivers read from.

#include "profile_indicator.h"
#include "core/services/players/feedback.h"
#include "core/services/players/manager.h"
#include "platform/platform.h"
#include <stdint.h>
#include <stdbool.h>

// Profile indicator state
static volatile uint8_t profile_to_indicate = 0;
static volatile uint8_t indicating_player = 0;

// Rumble pattern state
static volatile uint8_t rumble_blinks_remaining = 0;
static volatile bool rumble_is_on = false;
static uint32_t rumble_state_change_us;

// LED pattern state
static volatile uint8_t led_blinks_remaining = 0;
static volatile bool led_is_on = false;
static uint32_t led_state_change_us;

// Timing constants
#define RUMBLE_ON_TIME_US  100000   // 100ms rumble on
#define RUMBLE_OFF_TIME_US 200000   // 200ms rumble off
#define LED_ON_TIME_US     100000   // 100ms LED on
#define LED_OFF_TIME_US    200000   // 200ms LED off

void profile_indicator_init(void)
{
    profile_to_indicate = 0;
    indicating_player = 0;
    rumble_blinks_remaining = 0;
    rumble_is_on = false;
    led_blinks_remaining = 0;
    led_is_on = false;
}

void profile_indicator_trigger_player(uint8_t player_index, uint8_t profile_index, uint8_t player_count)
{
    (void)player_count;  // Not needed - we use feedback state directly

    // Only trigger if not already active
    if (rumble_blinks_remaining > 0 || led_blinks_remaining > 0) {
        return;
    }

    profile_to_indicate = profile_index;
    indicating_player = player_index;

    // Number of blinks = profile_index + 1 (profile 0 = 1 blink, etc.)
    uint8_t blink_count = profile_index + 1;

    // Start rumble pattern (use internal setter to bypass indicator check)
    rumble_blinks_remaining = blink_count;
    rumble_is_on = true;
    rumble_state_change_us = platform_time_us();
    feedback_set_rumble_internal(player_index, 255, 255);

    // Start LED pattern (use internal setter to bypass indicator check)
    led_blinks_remaining = blink_count;
    led_is_on = true;
    led_state_change_us = platform_time_us();
    feedback_set_led_player_internal(player_index, profile_index + 1);
}

void profile_indicator_trigger(uint8_t profile_index, uint8_t player_count)
{
    profile_indicator_trigger_player(0, profile_index, player_count);
}

uint8_t profile_indicator_get_rumble(void)
{
    // Legacy - just return current state for backwards compat
    return rumble_is_on ? 255 : 0;
}

uint8_t profile_indicator_get_player_led(uint8_t player_count)
{
    // If indicator is active, return blinking pattern
    if (led_blinks_remaining > 0) {
        if (led_is_on) {
            return PLAYER_LEDS[profile_to_indicate + 1];
        } else {
            return PLAYER_LEDS[0];
        }
    }
    return PLAYER_LEDS[player_count];
}

bool profile_indicator_is_active(void)
{
    return (rumble_blinks_remaining > 0 || led_blinks_remaining > 0);
}

bool profile_indicator_is_active_for_player(uint8_t player_index)
{
    return profile_indicator_is_active() && (indicating_player == player_index);
}

void profile_indicator_cancel_rumble(void)
{
    rumble_blinks_remaining = 0;
    rumble_is_on = false;
}

// Flag to completely disable profile indicator rumble (for DC output)
static bool rumble_disabled = false;

void profile_indicator_disable_rumble(void)
{
    rumble_disabled = true;
    rumble_blinks_remaining = 0;
    rumble_is_on = false;
}

int8_t profile_indicator_get_display_player_index(int8_t actual_player_index)
{
    if (led_blinks_remaining > 0) {
        return led_is_on ? profile_to_indicate : -1;
    }
    return actual_player_index;
}

void profile_indicator_task(void)
{
    uint32_t now = platform_time_us();

    // Handle rumble state machine (use internal setters to bypass indicator check)
    if (rumble_blinks_remaining > 0 && !rumble_disabled) {
        uint32_t elapsed = now - rumble_state_change_us;

        if (rumble_is_on && elapsed >= RUMBLE_ON_TIME_US) {
            // Turn off rumble
            rumble_is_on = false;
            rumble_blinks_remaining--;
            rumble_state_change_us = now;
            feedback_set_rumble_internal(indicating_player, 0, 0);
        }
        else if (!rumble_is_on && elapsed >= RUMBLE_OFF_TIME_US && rumble_blinks_remaining > 0) {
            // Turn on rumble for next pulse
            rumble_is_on = true;
            rumble_state_change_us = now;
            feedback_set_rumble_internal(indicating_player, 255, 255);
        }
    }

    // Handle LED state machine (use internal setters to bypass indicator check)
    if (led_blinks_remaining > 0) {
        uint32_t elapsed = now - led_state_change_us;

        if (led_is_on && elapsed >= LED_ON_TIME_US) {
            // Turn off LED
            led_is_on = false;
            led_blinks_remaining--;
            led_state_change_us = now;
            // Set LED to "off" state (could be all off or dim)
            feedback_set_led_rgb_internal(indicating_player, 0, 0, 0);
        }
        else if (!led_is_on && elapsed >= LED_OFF_TIME_US && led_blinks_remaining > 0) {
            // Turn on LED for next blink
            led_is_on = true;
            led_state_change_us = now;
            feedback_set_led_player_internal(indicating_player, profile_to_indicate + 1);
        }
    }
}
