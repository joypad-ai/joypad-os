/*
 * N642DC Console LED Configuration
 * Defines player LED colors and patterns for N64 to Dreamcast adapter
 */

#ifndef CONSOLE_LED_CONFIG_H
#define CONSOLE_LED_CONFIG_H

// Player 1 - Green (N64 controller color)
#define LED_P1_R 0
#define LED_P1_G 64
#define LED_P1_B 0
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

// Player 4 - Yellow
#define LED_P4_R 64
#define LED_P4_G 64
#define LED_P4_B 0
#define LED_P4_PATTERN 0b11011

// Player 5 - Purple
#define LED_P5_R 32
#define LED_P5_G 0
#define LED_P5_B 64
#define LED_P5_PATTERN 0b11111

// Default/Unassigned - dim yellow (waiting for controller)
#define LED_DEFAULT_R 32
#define LED_DEFAULT_G 32
#define LED_DEFAULT_B 0
#define LED_DEFAULT_PATTERN 0

// Neopixel (WS2812) board LED patterns by player count
#define NEOPIXEL_PATTERN_0 pattern_greys
#define NEOPIXEL_PATTERN_1 pattern_green
#define NEOPIXEL_PATTERN_2 pattern_blue
#define NEOPIXEL_PATTERN_3 pattern_red
#define NEOPIXEL_PATTERN_4 pattern_purple
#define NEOPIXEL_PATTERN_5 pattern_yellow

#endif // CONSOLE_LED_CONFIG_H
