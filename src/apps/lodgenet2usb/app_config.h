/*
 * LodgeNet2USB LED Configuration
 * Defines player LED colors and patterns for controllers
 */

#ifndef CONSOLE_LED_CONFIG_H
#define CONSOLE_LED_CONFIG_H

// Player 1 - Teal (LodgeNet theme)
#define LED_P1_R 0
#define LED_P1_G 40
#define LED_P1_B 60
#define LED_P1_PATTERN 0b00100

// Default/Unassigned - White
#define LED_DEFAULT_R 32
#define LED_DEFAULT_G 32
#define LED_DEFAULT_B 32
#define LED_DEFAULT_PATTERN 0

// Neopixel patterns
#define NEOPIXEL_PATTERN_0 pattern_purples
#define NEOPIXEL_PATTERN_1 pattern_purple

#endif // CONSOLE_LED_CONFIG_H
