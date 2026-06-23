// leds.c - LED Subsystem
//
// Unified LED control for status indication.
// Supports NeoPixel (RGB) and plain GPIO LEDs (BOARD_LED_PIN).

#include "leds.h"
#include "neopixel/ws2812.h"
#include "core/services/players/manager.h"
#include "platform/platform.h"

static int connected_devices = 0;

// ============================================================================
// PLAIN GPIO LED (fallback when no NeoPixel)
// ============================================================================

// A single-color status LED is available either as a plain GPIO
// (BOARD_LED_PIN) or, on Pico W / Pico 2 W, behind the CYW43 chip
// (BOARD_LED_CYW43 — the onboard LED is on WL_GPIO0, not a GPIO).
#if defined(BOARD_LED_PIN) || defined(BOARD_LED_CYW43)
#define BOARD_LED_ENABLED 1
#include "pico/stdlib.h"
#ifdef BOARD_LED_CYW43
#include "pico/cyw43_arch.h"
#ifndef CYW43_WL_GPIO_LED_PIN
#define CYW43_WL_GPIO_LED_PIN 0
#endif
#endif

static bool board_led_inited = false;
static uint32_t board_led_last_toggle = 0;
static bool board_led_state = false;
static uint8_t board_led_blink_count = 0;
static uint32_t board_led_indicate_start = 0;

static void board_led_init(void)
{
#ifdef BOARD_LED_CYW43
    // Bring up the wireless arch just enough to toggle WL_GPIO0; leave
    // board_led_inited false on failure so we never poke an absent chip.
    if (cyw43_arch_init() != 0) return;
#else
    gpio_init(BOARD_LED_PIN);
    gpio_set_dir(BOARD_LED_PIN, GPIO_OUT);
#endif
    board_led_inited = true;
}

static void board_led_set(bool on)
{
    if (!board_led_inited) return;
    if (on == board_led_state) return;  // idempotent — a CYW43 put is an SPI txn
    board_led_state = on;
#ifdef BOARD_LED_CYW43
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, on);
#else
    gpio_put(BOARD_LED_PIN, on);
#endif
}

// Plain LED status patterns:
//   Solid on:            Device(s) connected
//   Slow blink  (500ms): No devices connected (idle)
//   Profile indicator:   Fast blinks (count = profile_index + 1)
static void board_led_task(int count)
{
    if (!board_led_inited) return;
    uint32_t now = platform_time_ms();

    // Profile indicator: fast blinks then pause
    if (board_led_blink_count > 0) {
        uint32_t elapsed = now - board_led_indicate_start;
        // Each blink = 150ms on + 150ms off, then 600ms pause
        uint32_t blink_duration = board_led_blink_count * 300;
        if (elapsed < blink_duration) {
            uint32_t phase = (elapsed / 150) % 2;
            uint32_t blink_num = elapsed / 300;
            if (blink_num < board_led_blink_count) {
                board_led_set(phase == 0);
            } else {
                board_led_set(false);
            }
        } else if (elapsed < blink_duration + 600) {
            board_led_set(false);  // Pause
        } else {
            board_led_blink_count = 0;  // Done
        }
        return;
    }

    if (count > 0) {
        // Connected — solid on
        board_led_set(true);
    } else {
        // Idle — slow blink
        if (now - board_led_last_toggle >= 500) {
            board_led_last_toggle = now;
            board_led_set(!board_led_state);
        }
    }
}
#endif // BOARD_LED_ENABLED

// ============================================================================
// PUBLIC API
// ============================================================================

void leds_init(void)
{
#ifdef BOARD_LED_ENABLED
    board_led_init();
#endif
    neopixel_init();
}

void leds_set_connected_devices(int count)
{
    connected_devices = count;
}

void leds_set_color(uint8_t r, uint8_t g, uint8_t b)
{
    neopixel_set_override_color(r, g, b);
}

void leds_task(void)
{
    int count = playersCount > connected_devices ? playersCount : connected_devices;
#ifdef BOARD_LED_ENABLED
    board_led_task(count);
#endif
    neopixel_task(count);
}

void leds_indicate_profile(uint8_t profile_index)
{
#ifdef BOARD_LED_ENABLED
    board_led_blink_count = profile_index + 1;
    board_led_indicate_start = platform_time_ms();
#endif
    neopixel_indicate_profile(profile_index);
}

bool leds_is_indicating(void)
{
#ifdef BOARD_LED_ENABLED
    if (board_led_blink_count > 0) return true;
#endif
    return neopixel_is_indicating();
}
