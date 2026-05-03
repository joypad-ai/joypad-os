// ws2812.h - NeoPixel LED Control
//
// Controls WS2812 RGB LED for status indication.

#ifndef WS2812_H
#define WS2812_H

#include <stdint.h>
#include <stdbool.h>

// Pull in the pico-sdk board header so ADAFRUIT_FEATHER_RP2040_USB_HOST,
// PICO_DEFAULT_WS2812_PIN, etc. are defined before we test for them
// below. Without this, including ws2812.h before any pico-sdk header
// would silently fall through to "no NeoPixel pin known".
#ifdef PICO_BUILD
#include "pico.h"
#endif

// ============================================================================
// Compile-time pin defaults
// ============================================================================
// Resolved in this header (not in ws2812.c) so other modules — most
// importantly the web/CDC config layer — report the same defaults the
// driver actually uses. Previously the macros only existed inside
// ws2812.c, which left cdc_commands.c falling back to -1 and the web
// UI showing NeoPixel "disabled" on every board with onboard pixels.

#ifndef WS2812_PIN
  #ifdef ADAFRUIT_FEATHER_RP2040_USB_HOST
    #define WS2812_PIN 21
    #define WS2812_POWER_PIN 20
  #elif defined(ADAFRUIT_MACROPAD_RP2040)
    #define WS2812_PIN 19
  #elif defined(PICO_DEFAULT_WS2812_PIN)
    #define WS2812_PIN PICO_DEFAULT_WS2812_PIN
  #else
    // No NeoPixel pin known for this board — driver auto-disables.
    #ifndef CONFIG_NO_NEOPIXEL
      #define CONFIG_NO_NEOPIXEL 1
    #endif
    #define WS2812_PIN 0  // unused, neopixel_init returns early
  #endif
#endif

#ifndef WS2812_NUM_PIXELS
#define WS2812_NUM_PIXELS 1
#endif

// Set NeoPixel data pin override (call before neopixel_init, -1 = use default)
void neopixel_set_pin(int8_t pin);

// Disable NeoPixel (stops all output, neopixel_task becomes no-op)
void neopixel_disable(void);

// Initialize NeoPixel LED
void neopixel_init(void);

// Update NeoPixel LED pattern based on player count
// pat: number of connected players (0 = no players, shows idle pattern)
void neopixel_task(int pat);

// Trigger profile indicator blink pattern
// profile_index: 0-3 (blinks profile_index + 1 times)
void neopixel_indicate_profile(uint8_t profile_index);

// Check if profile indicator is currently active
bool neopixel_is_indicating(void);

// Set custom per-LED colors from GPIO config
// colors: array of [n][3] RGB values, count: number of LEDs
void neopixel_set_custom_colors(const uint8_t colors[][3], uint8_t count);

// Check if custom colors are active
bool neopixel_has_custom_colors(void);

// Set bitmask of LEDs that pulse with breathing animation
// bit N set = LED N pulses, 0 = all solid
void neopixel_set_pulse_mask(uint16_t mask);

// Set bitmask of LEDs currently pressed (shown as bright white)
void neopixel_set_press_mask(uint16_t mask);

// Set override color for mode indication (USB output apps)
// When set, overrides pattern table: pulses when pat=0, solid when pat>0
void neopixel_set_override_color(uint8_t r, uint8_t g, uint8_t b);

#endif // WS2812_H
