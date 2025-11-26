// hotkey.h - Hotkey detection service
//
// Detects button sequences (e.g., Konami code) and manages test mode state.

#ifndef HOTKEY_H
#define HOTKEY_H

#include <stdint.h>
#include <stdbool.h>

// Hotkey sequence length
#define HOTKEY_LENGTH 10

// Test mode API
bool hotkey_is_test_mode(void);
void hotkey_reset_test_mode(void);
uint8_t hotkey_get_test_counter(void);

// Called from console update_output() to detect hotkey sequences
void codes_task(void);

#endif // HOTKEY_H
