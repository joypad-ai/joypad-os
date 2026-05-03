/**
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "ws2812.h"        // shared WS2812_PIN / WS2812_NUM_PIXELS defaults
#include "ws2812.pio.h"
#include "app_config.h"

// Number of NeoPixels (can be overridden in CMakeLists.txt)
// WS2812_PIN / WS2812_NUM_PIXELS are resolved in ws2812.h so the web
// config layer reports the same defaults the driver uses.
#define NUM_PIXELS WS2812_NUM_PIXELS

#ifndef IS_RGBW
  #ifdef ADAFRUIT_MACROPAD_RP2040
    #define IS_RGBW false  // MacroPad uses WS2812B (RGB, not RGBW)
  #else
    #define IS_RGBW true
  #endif
#endif

#include "core/services/codes/codes.h"

static PIO pio;
static uint sm;

static bool neopixel_disabled = false;
static absolute_time_t init_time;
static absolute_time_t current_time;
static absolute_time_t loop_time;
static const int64_t reset_period = 10000;
int dir = 1; // direction
int tic = 0; // ticker

// NeoPixel profile indicator state machine
typedef enum {
    NEOPIXEL_IDLE,         // Normal operation - showing connection status
    NEOPIXEL_BLINK_ON,     // Profile indicator - LED on
    NEOPIXEL_BLINK_OFF,    // Profile indicator - LED off
    NEOPIXEL_BLINK_PAUSE   // Profile indicator - final pause before returning to idle
} neopixel_state_t;

static volatile neopixel_state_t neopixel_state = NEOPIXEL_IDLE;
static volatile uint8_t profile_to_indicate = 0;
static volatile uint8_t blinks_remaining = 0;
static volatile int stored_pattern = 0;  // Store player count for color matching
static absolute_time_t state_change_time;

// Custom per-LED colors (set via neopixel_set_custom_colors)
static uint8_t custom_led_colors[16][3];  // [led_index][R, G, B]
static bool use_custom_colors = false;

// Per-LED pulse mask for breathing animation
static uint16_t led_pulse_mask = 0;

// Per-LED press mask (pressed keys show bright white)
static uint16_t led_press_mask = 0;

// Override color for mode indication (set via neopixel_set_override_color)
static uint8_t override_r = 0, override_g = 0, override_b = 0;
static bool has_override_color = false;

// Timing constants for NeoPixel profile indicator (in microseconds for precision)
// We count OFF blinks, so OFF time is longer and more noticeable
#define BLINK_OFF_TIME_US 200000  // 200ms LED off (this is what we count)
#define BLINK_ON_TIME_US 100000   // 100ms LED on (brief flash between OFF blinks)

static inline void put_pixel(uint32_t pixel_grb) {
    pio_sm_put_blocking(pio, sm, pixel_grb << 8u);
}

static inline uint32_t urgb_u32(uint8_t r, uint8_t g, uint8_t b) {
    return
            ((uint32_t) (r) << 8) |
            ((uint32_t) (g) << 16) |
            (uint32_t) (b);
}

void pattern_snakes(uint len, uint t) {
    for (uint i = 0; i < len; ++i) {
        uint x = (i + (t >> 1)) % 64;
        if (x < 10)
            put_pixel(urgb_u32(0xff, 0, 0));
        else if (x >= 15 && x < 25)
            put_pixel(urgb_u32(0, 0xff, 0));
        else if (x >= 30 && x < 40)
            put_pixel(urgb_u32(0, 0, 0xff));
        else
            put_pixel(0);
    }
}

void pattern_random(uint len, uint t) {
    if (t % 8)
        return;
    for (int i = 0; i < len; ++i)
        put_pixel(rand());
}

void pattern_sparkle(uint len, uint t) {
    if (t % 8)
        return;
    for (int i = 0; i < len; ++i)
        put_pixel(rand() % 16 ? 0 : 0xffffffff);
}

void pattern_greys(uint len, uint t) {
    int max = 100; // let's not draw too much current!
    t %= max;
    for (int i = 0; i < len; ++i) {
        put_pixel(t * 0x10101);
        if (++t >= max) t = 0;
    }
}

void pattern_blues(uint len, uint t) {
    int max = 100; // let's not draw too much current!
    t %= max;
    for (int i = 0; i < len; ++i) {
        put_pixel(t * 0x00001);
        if (++t >= max) t = 0;
    }
}

void pattern_purples(uint len, uint t) {
    int max = 100; // let's not draw too much current!
    t %= max;
    for (int i = 0; i < len; ++i) {
        uint8_t intensity = t; // Adjust the intensity value for a darker effect
        put_pixel(urgb_u32(intensity / 10, 0, intensity / 1)); // Dark purple color (red + blue)
        if (++t >= max) t = 0;
    }
}

void pattern_pinks(uint len, uint t) {
    int max = 100; // let's not draw too much current!
    t %= max;
    for (int i = 0; i < len; ++i) {
        uint8_t intensity = t; // Adjust the intensity value for a darker effect
        put_pixel(urgb_u32(intensity / 2, 0, intensity / 1)); // Dark purple color (red + blue)
        if (++t >= max) t = 0;
    }
}

void pattern_reds(uint len, uint t) {
    int max = 100; // let's not draw too much current!
    t %= max;
    for (int i = 0; i < len; ++i) {
        put_pixel(t * 0x00100);
        if (++t >= max) t = 0;
    }
}

void pattern_greens(uint len, uint t) {
    int max = 100; // let's not draw too much current!
    t %= max;
    for (int i = 0; i < len; ++i) {
        uint8_t intensity = t; // Adjust the intensity value for a darker effect
        put_pixel(urgb_u32(0, intensity / 10, 0));
        if (++t >= max) t = 0;
    }
}

void pattern_blue(uint len, uint t) {
    for (uint i = 0; i < len; ++i) {
        put_pixel(urgb_u32(0, 0, 64)); // blue
    }
}

void pattern_red(uint len, uint t) {
    for (uint i = 0; i < len; ++i) {
        put_pixel(urgb_u32(64, 0, 0)); // red
    }
}

void pattern_orange(uint len, uint t) {
    for (uint i = 0; i < len; ++i) {
        put_pixel(urgb_u32(64, 24, 0)); // orange
    }
}

void pattern_oranges(uint len, uint t) {
    int max = 100;
    t %= max;
    for (int i = 0; i < len; ++i) {
        uint8_t intensity = t;
        put_pixel(urgb_u32(intensity, intensity / 3, 0)); // orange gradient
        if (++t >= max) t = 0;
    }
}

void pattern_green(uint len, uint t) {
    for (uint i = 0; i < len; ++i) {
        put_pixel(urgb_u32(0, 64, 0)); // green
    }
}

void pattern_purple(uint len, uint t) {
    for (uint i = 0; i < len; ++i) {
        put_pixel(urgb_u32(6, 0, 64)); // purple
    }
}

void pattern_pink(uint len, uint t) {
    for (uint i = 0; i < len; ++i) {
        put_pixel(urgb_u32(64, 20, 32)); // pink
    }
}

void pattern_yellow(uint len, uint t) {
    for (uint i = 0; i < len; ++i) {
        put_pixel(urgb_u32(64, 64, 0)); // yellow
    }
}

void pattern_br(uint len, uint t) {
    for (uint i = 0; i < len; ++i) {
        uint x = (i + (t >> 1)) % 64;
        if (x < 10)
            put_pixel(urgb_u32(0xff, 0, 0));
        else if (x >= 15 && x < 25)
            put_pixel(urgb_u32(0, 0, 0xff));
        else if (x >= 30 && x < 40)
            put_pixel(urgb_u32(0xff, 0, 0));
        else
            put_pixel(urgb_u32(0xff, 0, 0));
    }
}

void pattern_brg(uint len, uint t) {
    for (uint i = 0; i < len; ++i) {
        uint x = (i + (t >> 1)) % 64;
        if (x < 10)
            put_pixel(urgb_u32(0, 0xff, 0));
        else if (x >= 15 && x < 25)
            put_pixel(urgb_u32(0, 0, 0xff));
        else if (x >= 30 && x < 40)
            put_pixel(urgb_u32(0xff, 0, 0));
        else
            put_pixel(urgb_u32(0, 0xff, 0));
    }
}

void pattern_brgp(uint len, uint t) {
    for (uint i = 0; i < len; ++i) {
        uint x = (i + (t >> 1)) % 64;
        if (x < 10)
            put_pixel(urgb_u32(0, 0, 0xff)); // blue
        else if (x >= 15 && x < 25)
            put_pixel(urgb_u32(0xff, 0, 0)); // red
        else if (x >= 30 && x < 40)
            put_pixel(urgb_u32(0, 0xff, 0)); // green
        else
            put_pixel(urgb_u32(20, 0, 40)); // purple
    }
}

void pattern_brgpy(uint len, uint t) {
    for (uint i = 0; i < len; ++i) {
        uint x = (i + (t >> 1)) % 64;
        if (x < 10)
            put_pixel(urgb_u32(0, 0, 0xff)); // blue
        else if (x >= 10 && x < 20)
            put_pixel(urgb_u32(0xff, 0, 0)); // red
        else if (x >= 20 && x < 30)
            put_pixel(urgb_u32(0, 0xff, 0)); // green
        else if (x >= 30 && x < 40)
            put_pixel(urgb_u32(20, 0, 40)); // purple
        else
            put_pixel(urgb_u32(0xff, 0xff, 0)); // yellow
    }
}

// Breathing brightness scale (0-255) from phase within cycle
// Smooth ramp up/down with quadratic easing, ~3s cycle
static inline uint8_t breathing_scale(uint t) {
    int phase = t % 300;  // ~3s cycle at 10ms per tic
    // Triangle wave 0→150→0
    int ramp = phase < 150 ? phase : (300 - phase);
    // Quadratic easing: slow at extremes, fast in middle
    // Range: 8 (dim glow) to 255 (full bright)
    return 8 + (uint8_t)((uint32_t)ramp * ramp * 247 / 22500);
}

// Custom colors pattern - uses colors set via neopixel_set_custom_colors()
// Priority: pressed (white) > breathing pulse > solid color
void pattern_custom(uint len, uint t) {
    for (uint i = 0; i < len && i < 16; ++i) {
        if (led_press_mask & (1 << i)) {
            // Pressed: white (capped to limit current draw on USB-powered boards)
            put_pixel(urgb_u32(128, 128, 128));
        } else if (led_pulse_mask & (1 << i)) {
            // Breathing pulse
            uint8_t s = breathing_scale(t);
            put_pixel(urgb_u32(
                (custom_led_colors[i][0] * s) / 255,
                (custom_led_colors[i][1] * s) / 255,
                (custom_led_colors[i][2] * s) / 255));
        } else {
            // Solid
            put_pixel(urgb_u32(custom_led_colors[i][0],
                              custom_led_colors[i][1],
                              custom_led_colors[i][2]));
        }
    }
}

// Set custom per-LED colors from GPIO config
// colors: array of [16][3] RGB values, count: number of LEDs
void neopixel_set_custom_colors(const uint8_t colors[][3], uint8_t count) {
    use_custom_colors = false;
    for (uint8_t i = 0; i < count && i < 16; ++i) {
        custom_led_colors[i][0] = colors[i][0];
        custom_led_colors[i][1] = colors[i][1];
        custom_led_colors[i][2] = colors[i][2];
        // Check if any color is non-zero
        if (colors[i][0] || colors[i][1] || colors[i][2]) {
            use_custom_colors = true;
        }
    }
}

// Check if custom colors are active
bool neopixel_has_custom_colors(void) {
    return use_custom_colors;
}

// Set bitmask of LEDs that pulse with breathing animation
void neopixel_set_pulse_mask(uint16_t mask) {
    led_pulse_mask = mask;
}

// Set bitmask of currently pressed LEDs (shown as bright white)
void neopixel_set_press_mask(uint16_t mask) {
    led_press_mask = mask;
}

// Set override color for mode indication
void neopixel_set_override_color(uint8_t r, uint8_t g, uint8_t b) {
    override_r = r;
    override_g = g;
    override_b = b;
    has_override_color = true;
}

typedef void (*pattern)(uint len, uint t);
const struct {
    pattern pat;
    const char *name;
} pattern_table[] = {
        // Console-specific patterns from led_config.h
        {NEOPIXEL_PATTERN_0, "P0"},      // 0 controllers
        {NEOPIXEL_PATTERN_1, "P1"},      // 1 controller
        {NEOPIXEL_PATTERN_2, "P2"},      // 2 controllers
        {NEOPIXEL_PATTERN_3, "P3"},      // 3 controllers
        {NEOPIXEL_PATTERN_4, "P4"},      // 4 controllers
        {NEOPIXEL_PATTERN_5, "P5"},      // 5 controllers
        {pattern_random,  "Random data"},// fun
        {pattern_sparkle, "Sparkles"},
        {pattern_snakes,  "Snakes!"},
        {pattern_greys,   "Greys"},
        {pattern_br,      "B R"},        // 2 controllers alt
        {pattern_brg,     "B R G"},      // 3 controllers alt
        {pattern_brgp,    "B R G P"},    // 4 controllers alt
        {pattern_brgpy,   "B R G P Y"},  // 5 controllers alt
};

void neopixel_set_pin(int8_t pin) {
    (void)pin; // RP2040: PIO pin set at compile time via WS2812_PIN
}

void neopixel_disable(void) {
    neopixel_disabled = true;
    printf("[ws2812] NeoPixel disabled\n");
}

void neopixel_init()
{
#ifdef CONFIG_NO_NEOPIXEL
    // NeoPixel disabled for this build (e.g., n642dc needs PIO space for joybus)
    return;
#endif

#ifdef WS2812_POWER_PIN
    // Enable NeoPixel power (required on some Adafruit boards)
    gpio_init(WS2812_POWER_PIN);
    gpio_set_dir(WS2812_POWER_PIN, GPIO_OUT);
    gpio_put(WS2812_POWER_PIN, 1);
#endif

    // PIO selection:
    // - CONFIG_DC: Use PIO1 SM3 (share with maple_rx which uses SM0/1/2)
    // - CONFIG_NEOPIXEL_USE_PIO1: Use PIO1 SM3 to leave PIO0 for PIO-USB
    //   (pico-pio-usb hardcodes PIO0 SM 0/1/2). Only safe when nothing else
    //   on the build claims PIO1 (e.g. no CYW43 / no maple).
    // - Others: Use PIO0
#if defined(CONFIG_DC) || defined(CONFIG_NEOPIXEL_USE_PIO1)
    pio = pio1;
    sm = 3;
    pio_sm_claim(pio, sm);
#else
    pio = pio0;
    sm = pio_claim_unused_sm(pio, true);
#endif

    // Load neopixel program and config state machine to run it.
    uint offset = pio_add_program(pio, &ws2812_program);
    ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, IS_RGBW);
    // Initialize all pixels off (app_task sets the correct color)
    for (uint i = 0; i < NUM_PIXELS; ++i) {
        put_pixel(0);
    }
}

// Trigger NeoPixel LED profile indicator blinking (called from console code)
void neopixel_indicate_profile(uint8_t profile_index)
{
    // Only trigger if currently idle
    if (neopixel_state == NEOPIXEL_IDLE) {
        profile_to_indicate = profile_index;
        blinks_remaining = profile_index + 1;  // Profile 0 = 1 OFF blink, etc.
        neopixel_state = NEOPIXEL_BLINK_OFF;   // Start by turning OFF
        state_change_time = get_absolute_time();
    }
}

// Check if NeoPixel profile indicator is currently active
bool neopixel_is_indicating(void)
{
    return neopixel_state != NEOPIXEL_IDLE;
}

void neopixel_task(int pat)
{
#ifdef CONFIG_NO_NEOPIXEL
    return;
#endif
    if (neopixel_disabled) return;

    current_time = get_absolute_time();

    // Handle profile indicator state machine
    if (neopixel_state != NEOPIXEL_IDLE) {
        int64_t time_in_state = absolute_time_diff_us(state_change_time, current_time);

        switch (neopixel_state) {
            case NEOPIXEL_BLINK_OFF:
                // Turn all LEDs off (this is what we count)
                for (uint i = 0; i < NUM_PIXELS; ++i) {
                    put_pixel(urgb_u32(0x00, 0x00, 0x00));
                }
                if (time_in_state >= BLINK_OFF_TIME_US) {
                    blinks_remaining--;
                    if (blinks_remaining > 0) {
                        // More OFF blinks needed, briefly turn ON between them
                        neopixel_state = NEOPIXEL_BLINK_ON;
                    } else {
                        // All OFF blinks done, return to normal solid purple
                        neopixel_state = NEOPIXEL_IDLE;
                        init_time = current_time;  // Reset pattern timing
                    }
                    state_change_time = current_time;
                }
                break;

            case NEOPIXEL_BLINK_ON:
                // Show LED using custom colors or pattern based on stored player count
                if (use_custom_colors) {
                    pattern_custom(NUM_PIXELS, tic);
                } else {
                    pattern_table[stored_pattern].pat(NUM_PIXELS, tic);
                }
                if (time_in_state >= BLINK_ON_TIME_US) {
                    // Back to OFF for the next blink
                    neopixel_state = NEOPIXEL_BLINK_OFF;
                    state_change_time = current_time;
                }
                break;

            case NEOPIXEL_BLINK_PAUSE:
                // Not used anymore, but keep for safety
                neopixel_state = NEOPIXEL_IDLE;
                break;

            default:
                neopixel_state = NEOPIXEL_IDLE;
                break;
        }
    }

    // Don't run normal NeoPixel LED patterns while indicating profile
    if (neopixel_state != NEOPIXEL_IDLE) {
        return;
    }

    // Normal operation - show connection status patterns
    // Store pattern for profile indicator to use
    if (pat > 5) pat = 5;
    if (pat && codes_is_test_mode()) pat = 6;
    stored_pattern = pat;

    // Override color mode: pulse when idle (pat=0), solid when connected (pat>0)
    if (has_override_color) {
        if (absolute_time_diff_us(init_time, current_time) > reset_period) {
            if (pat == 0) {
                // Pulse: triangle wave brightness (tic cycles 0-199)
                int phase = tic % 200;
                int bright = phase < 100 ? (100 + phase) : (100 + (200 - phase));
                // bright ranges 100-200, visible on 3.3V NeoPixels
                for (uint i = 0; i < NUM_PIXELS; ++i) {
                    put_pixel(urgb_u32(
                        (override_r * bright) / 255,
                        (override_g * bright) / 255,
                        (override_b * bright) / 255));
                }
            } else {
                // Solid: full brightness
                for (uint i = 0; i < NUM_PIXELS; ++i) {
                    put_pixel(urgb_u32(override_r, override_g, override_b));
                }
            }
            tic += dir;
            init_time = get_absolute_time();
        }
        return;
    }

    if (absolute_time_diff_us(init_time, current_time) > reset_period) {
        // Use custom colors if set, otherwise use pattern table
        if (use_custom_colors) {
            pattern_custom(NUM_PIXELS, tic);
        } else {
            pattern_table[pat].pat(NUM_PIXELS, tic);
        }

        tic += dir;

        init_time = get_absolute_time();
    }
}
