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

// Player LED colors/patterns. The USB output modes (dualsense_mode.c etc.)
// reference LED_P*_PATTERN unconditionally, so these must be defined even
// though this receiver caps at one controller. Palette matches usb2usb.
#define LED_P1_R 0
#define LED_P1_G 40
#define LED_P1_B 40
#define LED_P1_PATTERN 0b00100
#define LED_P2_R 0
#define LED_P2_G 0
#define LED_P2_B 64
#define LED_P2_PATTERN 0b01010
#define LED_P3_R 64
#define LED_P3_G 0
#define LED_P3_B 0
#define LED_P3_PATTERN 0b10101
#define LED_P4_R 0
#define LED_P4_G 64
#define LED_P4_B 0
#define LED_P4_PATTERN 0b11011
#define LED_P5_R 64
#define LED_P5_G 64
#define LED_P5_B 0
#define LED_P5_PATTERN 0b11111
#define LED_P6_R 0
#define LED_P6_G 64
#define LED_P6_B 64
#define LED_P6_PATTERN 0b00011
#define LED_P7_R 64
#define LED_P7_G 32
#define LED_P7_B 0
#define LED_P7_PATTERN 0b00110
#define LED_DEFAULT_R 32
#define LED_DEFAULT_G 32
#define LED_DEFAULT_B 32
#define LED_DEFAULT_PATTERN 0

#endif // CONSOLE_LED_CONFIG_H
