// speaker.h - PWM Speaker/Buzzer Driver
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Simple PWM-based speaker driver for haptic feedback via buzzer.
// Used on MacroPad RP2040 (speaker on GPIO 16, shutdown on GPIO 14).

#ifndef SPEAKER_H
#define SPEAKER_H

#include <stdint.h>
#include <stdbool.h>

// Initialize speaker with GPIO pins
// speaker_pin: PWM output pin (e.g., GPIO 16)
// shutdown_pin: Speaker enable pin (e.g., GPIO 14), or -1 if not used
void speaker_init(uint8_t speaker_pin, int8_t shutdown_pin);

// Set speaker buzz based on rumble intensity (0-255)
// Higher intensity = higher frequency and volume
void speaker_set_rumble(uint8_t intensity);

// Play a tone at specified frequency (Hz) and volume (0-255)
void speaker_tone(uint16_t frequency, uint8_t volume);

// Stop speaker output
void speaker_stop(void);

// Check if speaker is initialized
bool speaker_is_initialized(void);

#endif // SPEAKER_H
