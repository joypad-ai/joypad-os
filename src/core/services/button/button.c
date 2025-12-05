// button.c - User button input service
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "button.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <stdio.h>

#ifdef USE_BOOTSEL_BUTTON
#include "hardware/sync.h"
#include "hardware/structs/ioqspi.h"
#include "hardware/structs/sio.h"
#endif

// ============================================================================
// STATE
// ============================================================================

// Button state machine states
typedef enum {
    STATE_IDLE,             // Waiting for press
    STATE_PRESSED,          // Button is pressed, timing for click vs hold
    STATE_WAIT_DOUBLE,      // Released after click, waiting for possible second click
    STATE_HELD,             // Hold threshold reached, waiting for release
} button_state_t;

static button_state_t state = STATE_IDLE;
static absolute_time_t press_time;      // When button was pressed
static absolute_time_t release_time;    // When button was released
static bool last_raw_state = false;     // Last debounced state
static absolute_time_t last_change_time; // For debouncing
static button_callback_t event_callback = NULL;
static bool hold_event_fired = false;   // Track if hold event was already fired
static bool double_click_fired = false; // Track if double-click was just fired

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

#ifdef USE_BOOTSEL_BUTTON
// Read BOOTSEL button state (on QSPI CS pin)
// Based on pico-examples/picoboard/button/button.c
static bool __no_inline_not_in_flash_func(get_bootsel_button)(void)
{
    const uint CS_PIN_INDEX = 1;

    // Disable interrupts - we can't access flash during this
    uint32_t flags = save_and_disable_interrupts();

    // Set chip select to Hi-Z (float)
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_LOW << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    // Wait for pin to stabilize
    for (volatile int i = 0; i < 1000; ++i);

    // Read pin state (button pulls low when pressed)
    bool button_state = !(sio_hw->gpio_hi_in & (1u << CS_PIN_INDEX));

    // Restore chip select to normal operation
    hw_write_masked(&ioqspi_hw->io[CS_PIN_INDEX].ctrl,
                    GPIO_OVERRIDE_NORMAL << IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_LSB,
                    IO_QSPI_GPIO_QSPI_SS_CTRL_OEOVER_BITS);

    restore_interrupts(flags);

    return button_state;
}
#endif

// Read debounced button state (active low - pressed = GPIO low)
static bool read_button_debounced(void)
{
#ifdef USE_BOOTSEL_BUTTON
    bool raw = get_bootsel_button();
#else
    bool raw = !gpio_get(BUTTON_USER_GPIO);  // Active low
#endif
    absolute_time_t now = get_absolute_time();

    if (raw != last_raw_state) {
        int64_t elapsed = absolute_time_diff_us(last_change_time, now) / 1000;
        if (elapsed >= BUTTON_DEBOUNCE_MS) {
            last_raw_state = raw;
            last_change_time = now;
        }
    }

    return last_raw_state;
}

// Get elapsed time since a timestamp in milliseconds
static uint32_t elapsed_ms(absolute_time_t since)
{
    return (uint32_t)(absolute_time_diff_us(since, get_absolute_time()) / 1000);
}

// Fire an event (call callback and return event)
static button_event_t fire_event(button_event_t event)
{
    if (event != BUTTON_EVENT_NONE) {
        // Log the event
        const char* event_names[] = {
            "NONE", "CLICK", "DOUBLE_CLICK", "HOLD", "RELEASE"
        };
        printf("[button] Event: %s\n", event_names[event]);

        // Call callback if registered
        if (event_callback) {
            event_callback(event);
        }
    }
    return event;
}

// ============================================================================
// PUBLIC API
// ============================================================================

void button_init(void)
{
#ifdef USE_BOOTSEL_BUTTON
    printf("[button] Initializing BOOTSEL button\n");
    // No GPIO init needed - BOOTSEL is on QSPI CS pin
#else
    printf("[button] Initializing on GPIO %d\n", BUTTON_USER_GPIO);

    // Configure GPIO as input with pull-up (button connects to GND)
    gpio_init(BUTTON_USER_GPIO);
    gpio_set_dir(BUTTON_USER_GPIO, GPIO_IN);
    gpio_pull_up(BUTTON_USER_GPIO);
#endif

    // Initialize state
    state = STATE_IDLE;
    last_raw_state = false;
    last_change_time = get_absolute_time();
    hold_event_fired = false;
    double_click_fired = false;

    printf("[button] Initialized\n");
}

button_event_t button_task(void)
{
    bool pressed = read_button_debounced();
    button_event_t event = BUTTON_EVENT_NONE;

    switch (state) {
        case STATE_IDLE:
            if (pressed) {
                // Button just pressed
                press_time = get_absolute_time();
                hold_event_fired = false;
                double_click_fired = false;
                state = STATE_PRESSED;
            }
            break;

        case STATE_PRESSED:
            if (!pressed) {
                // Button released
                uint32_t held = elapsed_ms(press_time);
                release_time = get_absolute_time();

                if (double_click_fired) {
                    // Just completed a double-click, go back to idle
                    // (don't wait for another click)
                    state = STATE_IDLE;
                } else if (held < BUTTON_CLICK_MAX_MS) {
                    // Short press - could be click or start of double-click
                    state = STATE_WAIT_DOUBLE;
                } else {
                    // Was a hold, now released
                    state = STATE_IDLE;
                    if (hold_event_fired) {
                        event = fire_event(BUTTON_EVENT_RELEASE);
                    }
                }
            } else {
                // Still pressed - check for hold
                uint32_t held = elapsed_ms(press_time);
                if (held >= BUTTON_HOLD_MS && !hold_event_fired) {
                    hold_event_fired = true;
                    state = STATE_HELD;
                    event = fire_event(BUTTON_EVENT_HOLD);
                }
            }
            break;

        case STATE_WAIT_DOUBLE:
            if (pressed) {
                // Second press - it's a double click!
                press_time = get_absolute_time();
                hold_event_fired = false;
                double_click_fired = true;  // Mark that double-click was fired
                event = fire_event(BUTTON_EVENT_DOUBLE_CLICK);
                state = STATE_PRESSED;  // Track this press too (could become hold)
            } else {
                // Still waiting for second press
                uint32_t since_release = elapsed_ms(release_time);
                if (since_release >= BUTTON_DOUBLE_CLICK_MS) {
                    // Timeout - it was a single click
                    event = fire_event(BUTTON_EVENT_CLICK);
                    state = STATE_IDLE;
                }
            }
            break;

        case STATE_HELD:
            if (!pressed) {
                // Released after hold
                event = fire_event(BUTTON_EVENT_RELEASE);
                state = STATE_IDLE;
            }
            break;
    }

    return event;
}

void button_set_callback(button_callback_t callback)
{
    event_callback = callback;
}

bool button_is_pressed(void)
{
    return read_button_debounced();
}

uint32_t button_held_ms(void)
{
    if (state == STATE_PRESSED || state == STATE_HELD) {
        return elapsed_ms(press_time);
    }
    return 0;
}
