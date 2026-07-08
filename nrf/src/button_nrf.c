// button_nrf.c - Button driver for nRF52840 boards
//
// Implements button.h using Zephyr GPIO. Active low with internal pull-up.
//
// XIAO nRF52840:   D1 = P0.03 on gpio0
// Feather nRF52840: User switch = P1.02 on gpio1

#include "core/services/button/button.h"
#include "platform/platform.h"
#include <zephyr/drivers/gpio.h>
#include <stdio.h>

#ifdef BOARD_FEATHER_NRF52840
#define BUTTON_PORT_LABEL DT_NODELABEL(gpio1)
#define BUTTON_PIN 2   // P1.02 (User switch)
#define BUTTON_PIN_STR "P1.02"
#else
#define BUTTON_PORT_LABEL DT_NODELABEL(gpio0)
#define BUTTON_PIN 3   // P0.03 (D1)
#define BUTTON_PIN_STR "P0.03"
#endif

static const struct device *button_port;

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
    if (!button_port) return false;

    bool raw = !gpio_pin_get(button_port, BUTTON_PIN);  // Active low
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
#ifdef DISABLE_USER_BUTTON
    // No dedicated user button on this board, and its default pin collides
    // with a pad/gamepad GPIO (e.g. XIAO D1/P0.03). Leave button_port NULL so
    // button_task() never fires events — frees the pin for pad input.
    button_port = NULL;
    printf("[button] User button disabled (pin reserved for pad input)\n");
    return;
#else
    button_port = DEVICE_DT_GET(BUTTON_PORT_LABEL);
    if (!device_is_ready(button_port)) {
        printf("[button] GPIO port not ready\n");
        button_port = NULL;
        return;
    }

    gpio_pin_configure(button_port, BUTTON_PIN, GPIO_INPUT | GPIO_PULL_UP);

    state = STATE_IDLE;
    last_raw_state = false;
    last_change_ms = platform_time_ms();
    hold_event_fired = false;
    click_count = 0;

    printf("[button] Initialized on %s\n", BUTTON_PIN_STR);
#endif // DISABLE_USER_BUTTON
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
