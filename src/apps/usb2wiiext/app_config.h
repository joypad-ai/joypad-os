/*
 * Wii Extension Console LED Configuration
 * Defines player LED colors and patterns for controllers.
 *
 * Required even though the primary board (KB2040) has no status LED:
 * sinput_host.c, sony_ds4.c and sony_ds5.c all #include "app_config.h" to
 * colour the *controller's* own lightbar/player LEDs, and sinput_host.c
 * reads P1 through P7. bt2wiiext's manifest only defines P1..P5 and gets
 * away with it because it links BTHID sources rather than USB_HOST_SOURCES;
 * this app links the full host set, so the range has to be complete.
 */

#ifndef CONSOLE_LED_CONFIG_H
#define CONSOLE_LED_CONFIG_H

// Player 1 - White (Wii console / Wiimote colour)
#define LED_P1_R 48
#define LED_P1_G 48
#define LED_P1_B 48
#define LED_P1_PATTERN 0b00100

// Player 2 - Blue
#define LED_P2_R 0
#define LED_P2_G 0
#define LED_P2_B 64
#define LED_P2_PATTERN 0b01010

// Player 3 - Red
#define LED_P3_R 64
#define LED_P3_G 0
#define LED_P3_B 0
#define LED_P3_PATTERN 0b10101

// Player 4 - Green
#define LED_P4_R 0
#define LED_P4_G 64
#define LED_P4_B 0
#define LED_P4_PATTERN 0b11011

// Player 5 - Yellow
#define LED_P5_R 64
#define LED_P5_G 64
#define LED_P5_B 0
#define LED_P5_PATTERN 0b11111

// Player 6 - Cyan
#define LED_P6_R 0
#define LED_P6_G 64
#define LED_P6_B 64
#define LED_P6_PATTERN 0b00011

// Player 7 - Purple
#define LED_P7_R 32
#define LED_P7_G 0
#define LED_P7_B 64
#define LED_P7_PATTERN 0b00110

// Default/Unassigned - White
#define LED_DEFAULT_R 32
#define LED_DEFAULT_G 32
#define LED_DEFAULT_B 32
#define LED_DEFAULT_PATTERN 0

// Neopixel (WS2812) board LED patterns by player count.
// Only reached on boards that have a WS2812; the KB2040 target sets
// CONFIG_NO_NEOPIXEL and the plain Pico has no board pin, so ws2812.h
// auto-defines it there.
#define NEOPIXEL_PATTERN_0 pattern_purples
#define NEOPIXEL_PATTERN_1 pattern_blue
#define NEOPIXEL_PATTERN_2 pattern_green
#define NEOPIXEL_PATTERN_3 pattern_red
#define NEOPIXEL_PATTERN_4 pattern_pink
#define NEOPIXEL_PATTERN_5 pattern_yellow

#endif // CONSOLE_LED_CONFIG_H
