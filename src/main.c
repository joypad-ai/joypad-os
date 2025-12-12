/*
 * Joypad - Modular controller firmware for RP2040-based devices
 *
 * A flexible foundation for building controller adapters, arcade sticks,
 * custom controllers, and any device that routes inputs to outputs.
 * Apps define the product behavior while the core handles the complexity.
 *
 * Inputs:  USB host (HID, X-input), Native (console controllers), BLE*, UART
 * Outputs: Native (GameCube, PCEngine, etc.), USB device*, BLE*, UART
 * Core:    Router, players, profiles, feedback, storage, LEDs
 *
 * Whether you're building a simple adapter or a full custom controller,
 * configure an app and let the firmware handle the rest.
 *
 * (* planned)
 *
 * Copyright (c) 2022-2025 Robert Dale Smith
 * https://github.com/RobertDaleSmith/Joypad
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "core/input_interface.h"
#include "core/output_interface.h"
#include "core/services/players/manager.h"
#include "core/services/leds/leds.h"
#include "core/services/storage/storage.h"

// App layer (linked per-product)
extern void app_init(void);
extern void app_task(void);
extern const OutputInterface** app_get_output_interfaces(uint8_t* count);
extern const InputInterface** app_get_input_interfaces(uint8_t* count);

// Cached interfaces (set once at startup)
static const OutputInterface** outputs = NULL;
static uint8_t output_count = 0;
static const InputInterface** inputs = NULL;
static uint8_t input_count = 0;

// Core 0 main loop - pinned in SRAM for consistent timing
static void __not_in_flash_func(core0_main)(void)
{
  while (1)
  {
    leds_task();
    players_task();
    storage_task();
    app_task();

    // Poll all input interfaces declared by the app
    for (uint8_t i = 0; i < input_count; i++) {
      if (inputs[i] && inputs[i]->task) {
        inputs[i]->task();
      }
    }

    // Run all output interface tasks
    for (uint8_t i = 0; i < output_count; i++) {
      if (outputs[i] && outputs[i]->task) {
        outputs[i]->task();
      }
    }
  }
}

int main(void)
{
  stdio_init_all();

  printf("\n[joypad] Starting...\n");

  sleep_ms(250);  // Brief pause for stability

  leds_init();
  storage_init();
  players_init();
  app_init();

  // Get and initialize input interfaces from app
  inputs = app_get_input_interfaces(&input_count);
  for (uint8_t i = 0; i < input_count; i++) {
    if (inputs[i] && inputs[i]->init) {
      printf("[joypad] Initializing input: %s\n", inputs[i]->name);
      inputs[i]->init();
    }
  }

  // Get and initialize output interfaces from app
  outputs = app_get_output_interfaces(&output_count);
  for (uint8_t i = 0; i < output_count; i++) {
    if (outputs[i] && outputs[i]->init) {
      printf("[joypad] Initializing output: %s\n", outputs[i]->name);
      outputs[i]->init();
    }
  }

  // Launch core1 task from first output that has one
  // Note: Only one output can use core1 (RP2040 has 2 cores)
  for (uint8_t i = 0; i < output_count; i++) {
    if (outputs[i] && outputs[i]->core1_task) {
      printf("[joypad] Launching core1 for: %s\n", outputs[i]->name);
      multicore_launch_core1(outputs[i]->core1_task);
      break;  // Only one core1 task possible
    }
  }

  core0_main();

  return 0;
}
