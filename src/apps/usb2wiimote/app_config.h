/*
 * USB2WIIMOTE LED Configuration (Pico W — single-player).
 */

#ifndef CONSOLE_LED_CONFIG_H
#define CONSOLE_LED_CONFIG_H

// Player 1 - Wiimote white
#define LED_P1_R 64
#define LED_P1_G 64
#define LED_P1_B 64
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

#define NEOPIXEL_PATTERN_0 pattern_purples
#define NEOPIXEL_PATTERN_1 pattern_purple
#define NEOPIXEL_PATTERN_2 pattern_br
#define NEOPIXEL_PATTERN_3 pattern_brg
#define NEOPIXEL_PATTERN_4 pattern_brgp
#define NEOPIXEL_PATTERN_5 pattern_brgpy

#endif // CONSOLE_LED_CONFIG_H
