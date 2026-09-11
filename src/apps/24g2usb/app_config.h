/*
 * 24G2USB LED Configuration
 * Neopixel (WS2812) board LED patterns, indexed by connected-controller count
 */

#ifndef CONSOLE_LED_CONFIG_H
#define CONSOLE_LED_CONFIG_H

// ws2812.c's pattern_table[] unconditionally references NEOPIXEL_PATTERN_0
// through _5, so all six must stay defined even though this receiver's
// single-controller cap (see MAX_PLAYER_SLOTS in app.h) means _2.._5 can
// never actually be selected -- removing them breaks the build.
#define NEOPIXEL_PATTERN_0 pattern_purples
#define NEOPIXEL_PATTERN_1 pattern_purple
#define NEOPIXEL_PATTERN_2 pattern_br
#define NEOPIXEL_PATTERN_3 pattern_brg
#define NEOPIXEL_PATTERN_4 pattern_brgp
#define NEOPIXEL_PATTERN_5 pattern_brgpy

#endif // CONSOLE_LED_CONFIG_H
