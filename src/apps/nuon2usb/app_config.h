/*
 * Nuon Console LED Configuration
 * Defines player LED colors and patterns for controllers
 */

#ifndef CONSOLE_LED_CONFIG_H
#define CONSOLE_LED_CONFIG_H

// Player 1 - Red
#define LED_P1_R 64
#define LED_P1_G 0
#define LED_P1_B 0
#define LED_P1_PATTERN 0b00100

// Player 2 - Blue
#define LED_P2_R 0
#define LED_P2_G 0
#define LED_P2_B 64
#define LED_P2_PATTERN 0b01010

// Player 3 - Green
#define LED_P3_R 0
#define LED_P3_G 64
#define LED_P3_B 0
#define LED_P3_PATTERN 0b10101

// Player 4 - Purple
#define LED_P4_R 20
#define LED_P4_G 0
#define LED_P4_B 40
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

// Player 7 - Orange
#define LED_P7_R 64
#define LED_P7_G 32
#define LED_P7_B 0
#define LED_P7_PATTERN 0b00110

// Default/Unassigned - White
#define LED_DEFAULT_R 32
#define LED_DEFAULT_G 32
#define LED_DEFAULT_B 32
#define LED_DEFAULT_PATTERN 0

// Neopixel (WS2812) board LED patterns by player count
#define NEOPIXEL_PATTERN_0 pattern_reds
#define NEOPIXEL_PATTERN_1 pattern_red
#define NEOPIXEL_PATTERN_2 pattern_red
#define NEOPIXEL_PATTERN_3 pattern_green
#define NEOPIXEL_PATTERN_4 pattern_pink
#define NEOPIXEL_PATTERN_5 pattern_yellow

#endif // CONSOLE_LED_CONFIG_H
