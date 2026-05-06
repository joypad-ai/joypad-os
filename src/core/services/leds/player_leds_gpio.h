// player_leds_gpio.h - 4-LED player indicator on raw GPIO pins
//
// Drives four GPIO pins from the same 4-bit PLAYER_LEDS[] bitmap that
// PS3, Switch Pro, and SInput controllers use internally. Active-high,
// matching the BOARD_LED_PIN convention. Compile-time-only feature for
// custom controller builds (controller_btusb on a hand-built board).
//
// Pin → bit mapping:
//   pin1 ↔ bit 0 (LED1)   pin2 ↔ bit 1 (LED2)
//   pin3 ↔ bit 2 (LED3)   pin4 ↔ bit 3 (LED4)
//
// Player 1 lights LED1 alone, player 2 lights LED2 alone, etc. Players
// 5–10 use the standard combo patterns from manager.c PLAYER_LEDS[].

#ifndef PLAYER_LEDS_GPIO_H
#define PLAYER_LEDS_GPIO_H

#include <stdint.h>

// Configure 4 GPIO pins as outputs, drive low. Call once at app_init.
void player_leds_gpio_init(uint8_t pin1, uint8_t pin2,
                           uint8_t pin3, uint8_t pin4);

// Drive the 4 pins from feedback_get_state(0)->led.pattern.
// Call from app_task at any cadence (writes are idempotent).
void player_leds_gpio_task(void);

#endif
