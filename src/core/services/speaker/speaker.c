// speaker.c - PWM Speaker/Buzzer Driver
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "speaker.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include <stdio.h>

// ============================================================================
// STATE
// ============================================================================

static bool initialized = false;
static uint8_t pwm_pin = 0;
static int8_t enable_pin = -1;
static uint pwm_slice = 0;
static uint pwm_channel = 0;

// ============================================================================
// IMPLEMENTATION
// ============================================================================

void speaker_init(uint8_t speaker_pin, int8_t shutdown_pin)
{
    pwm_pin = speaker_pin;
    enable_pin = shutdown_pin;

    // Set up speaker enable pin if provided
    if (enable_pin >= 0) {
        gpio_init(enable_pin);
        gpio_set_dir(enable_pin, GPIO_OUT);
        gpio_put(enable_pin, 0);  // Start with speaker disabled
    }

    // Set up PWM on speaker pin
    gpio_set_function(pwm_pin, GPIO_FUNC_PWM);
    pwm_slice = pwm_gpio_to_slice_num(pwm_pin);
    pwm_channel = pwm_gpio_to_channel(pwm_pin);

    // Configure PWM for audio range
    // Default to ~1kHz, will be adjusted by tone/rumble functions
    pwm_set_wrap(pwm_slice, 65535);
    pwm_set_chan_level(pwm_slice, pwm_channel, 0);
    pwm_set_enabled(pwm_slice, true);

    initialized = true;
    printf("[speaker] Initialized on GPIO %d (enable: GPIO %d)\n",
           pwm_pin, enable_pin);
}

void speaker_tone(uint16_t frequency, uint8_t volume)
{
    if (!initialized || frequency == 0) {
        speaker_stop();
        return;
    }

    // Enable speaker
    if (enable_pin >= 0) {
        gpio_put(enable_pin, 1);
    }

    // Calculate PWM settings for desired frequency
    // PWM frequency = clock / (wrap + 1)
    uint32_t clock = clock_get_hz(clk_sys);
    uint32_t divider = 1;
    uint32_t wrap = clock / frequency;

    // Use clock divider if wrap would be too large
    while (wrap > 65535 && divider < 256) {
        divider++;
        wrap = clock / (frequency * divider);
    }

    if (wrap > 65535) wrap = 65535;
    if (wrap < 1) wrap = 1;

    // Set PWM frequency
    pwm_set_clkdiv(pwm_slice, (float)divider);
    pwm_set_wrap(pwm_slice, wrap);

    // Set duty cycle based on volume (50% max for square wave)
    uint32_t level = (wrap * volume) / 512;  // volume/255 * wrap/2
    pwm_set_chan_level(pwm_slice, pwm_channel, level);
}

// Volume scaling (0-100, where 100 = full volume)
#define SPEAKER_VOLUME_PERCENT 25

void speaker_set_rumble(uint8_t intensity)
{
    if (!initialized) return;

    if (intensity == 0) {
        speaker_stop();
        return;
    }

    // Map rumble intensity to frequency and volume
    // Lower rumble = lower frequency (more bass-like buzz)
    // Frequency range: 100Hz (low rumble) to 400Hz (high rumble)
    uint16_t frequency = 100 + (intensity * 300 / 255);

    // Volume proportional to intensity, scaled down
    uint8_t volume = (intensity * SPEAKER_VOLUME_PERCENT) / 100;

    speaker_tone(frequency, volume);
}

void speaker_stop(void)
{
    if (!initialized) return;

    // Disable speaker
    if (enable_pin >= 0) {
        gpio_put(enable_pin, 0);
    }

    // Set PWM level to 0
    pwm_set_chan_level(pwm_slice, pwm_channel, 0);
}

bool speaker_is_initialized(void)
{
    return initialized;
}
