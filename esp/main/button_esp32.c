// button_esp32.c - GPIO button for ESP32
//
// Implements the button.h API using ESP-IDF GPIO.
// Same state machine as RP2040 button.c but using platform HAL.

#include "core/services/button/button.h"
#include "platform/platform.h"
#include "driver/gpio.h"
#include <stdio.h>

// Button GPIO pin (can be overridden via Kconfig/compile definition)
#ifndef BUTTON_USER_GPIO
#define BUTTON_USER_GPIO 0  // BOOT button on most ESP32-S3 dev boards
#endif

// ============================================================================
// STATE
// ============================================================================

typedef enum {
    STATE_IDLE,
    STATE_PRESSED,
    STATE_WAIT_DOUBLE,
    STATE_WAIT_TRIPLE,
    STATE_HELD,
} button_state_t;

static button_state_t state = STATE_IDLE;
static uint32_t press_time_ms = 0;
static uint32_t release_time_ms = 0;
static bool last_raw_state = false;
static uint32_t last_change_ms = 0;
static button_callback_t event_callback = NULL;
static bool hold_event_fired = false;
static uint8_t click_count = 0;

// ============================================================================
// INTERNAL HELPERS
// ============================================================================

static bool read_button_debounced(void)
{
    bool raw = !gpio_get_level(BUTTON_USER_GPIO);  // Active low
    uint32_t now = platform_time_ms();

    if (raw != last_raw_state) {
        if (now - last_change_ms >= BUTTON_DEBOUNCE_MS) {
            last_raw_state = raw;
            last_change_ms = now;
        }
    }

    return last_raw_state;
}

static uint32_t elapsed_ms_since(uint32_t since)
{
    return platform_time_ms() - since;
}

static button_event_t fire_event(button_event_t event)
{
    if (event != BUTTON_EVENT_NONE) {
        const char* event_names[] = {
            "NONE", "CLICK", "DOUBLE_CLICK", "TRIPLE_CLICK", "HOLD", "RELEASE"
        };
        printf("[button] Event: %s\n", event_names[event]);
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
    printf("[button] Initializing on GPIO %d\n", BUTTON_USER_GPIO);

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_USER_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    state = STATE_IDLE;
    last_raw_state = false;
    last_change_ms = platform_time_ms();
    hold_event_fired = false;
    click_count = 0;

    printf("[button] Initialized\n");
}

button_event_t button_task(void)
{
    bool pressed = read_button_debounced();
    button_event_t event = BUTTON_EVENT_NONE;

    switch (state) {
        case STATE_IDLE:
            if (pressed) {
                press_time_ms = platform_time_ms();
                hold_event_fired = false;
                click_count = 0;
                state = STATE_PRESSED;
            }
            break;

        case STATE_PRESSED:
            if (!pressed) {
                uint32_t held = elapsed_ms_since(press_time_ms);
                release_time_ms = platform_time_ms();

                if (held < BUTTON_CLICK_MAX_MS) {
                    click_count++;
                    if (click_count >= 3) {
                        event = fire_event(BUTTON_EVENT_TRIPLE_CLICK);
                        click_count = 0;
                        state = STATE_IDLE;
                    } else if (click_count == 2) {
                        state = STATE_WAIT_TRIPLE;
                    } else {
                        state = STATE_WAIT_DOUBLE;
                    }
                } else {
                    click_count = 0;
                    state = STATE_IDLE;
                    if (hold_event_fired) {
                        event = fire_event(BUTTON_EVENT_RELEASE);
                    }
                }
            } else {
                uint32_t held = elapsed_ms_since(press_time_ms);
                if (held >= BUTTON_HOLD_MS && !hold_event_fired) {
                    hold_event_fired = true;
                    click_count = 0;
                    state = STATE_HELD;
                    event = fire_event(BUTTON_EVENT_HOLD);
                }
            }
            break;

        case STATE_WAIT_DOUBLE:
            if (pressed) {
                press_time_ms = platform_time_ms();
                hold_event_fired = false;
                state = STATE_PRESSED;
            } else {
                if (elapsed_ms_since(release_time_ms) >= BUTTON_DOUBLE_CLICK_MS) {
                    event = fire_event(BUTTON_EVENT_CLICK);
                    click_count = 0;
                    state = STATE_IDLE;
                }
            }
            break;

        case STATE_WAIT_TRIPLE:
            if (pressed) {
                press_time_ms = platform_time_ms();
                hold_event_fired = false;
                state = STATE_PRESSED;
            } else {
                if (elapsed_ms_since(release_time_ms) >= BUTTON_DOUBLE_CLICK_MS) {
                    event = fire_event(BUTTON_EVENT_DOUBLE_CLICK);
                    click_count = 0;
                    state = STATE_IDLE;
                }
            }
            break;

        case STATE_HELD:
            if (!pressed) {
                event = fire_event(BUTTON_EVENT_RELEASE);
                click_count = 0;
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
        return elapsed_ms_since(press_time_ms);
    }
    return 0;
}
