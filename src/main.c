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
#include "pico/flash.h"

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

// Active/primary output interface (accessible from other modules)
const OutputInterface* active_output = NULL;

// Store core1 task for wrapper - can be set after Core 1 launch
static volatile void (*core1_actual_task)(void) = NULL;
static volatile bool core1_task_ready = false;

// Core 1 wrapper - initializes flash safety, then waits for and runs actual task
static void core1_wrapper(void) {
  // Initialize multicore lockout for flash_safe_execute to work
  // This allows Core 0 to safely write to flash while Core 1 is running
  flash_safe_execute_core_init();

  // Wait for Core 0 to assign a task (or signal no task needed)
  while (!core1_task_ready) {
    __wfe();  // Wait for event (woken by __sev() from Core 0)
  }

  // Run the actual core1 task if one was provided
  if (core1_actual_task) {
    core1_actual_task();
  } else {
    // No task - just idle forever while handling flash lockout requests
    while (1) {
      __wfi();  // Wait for interrupt (low power idle)
    }
  }
}

// Core 0 main loop - pinned in SRAM for consistent timing
static void __not_in_flash_func(core0_main)(void)
{
  printf("[joypad] Entering main loop\n");
  static bool first_loop = true;
  while (1)
  {
    if (first_loop) printf("[joypad] Loop: leds\n");
    leds_task();
    if (first_loop) printf("[joypad] Loop: players\n");
    players_task();
    if (first_loop) printf("[joypad] Loop: storage\n");
    storage_task();
    if (first_loop) printf("[joypad] Loop: app\n");
    app_task();

    // Poll all input interfaces declared by the app
    for (uint8_t i = 0; i < input_count; i++) {
      if (inputs[i] && inputs[i]->task) {
        if (first_loop) printf("[joypad] Loop: input %s\n", inputs[i]->name);
        inputs[i]->task();
      }
    }

    // Run all output interface tasks
    for (uint8_t i = 0; i < output_count; i++) {
      if (outputs[i] && outputs[i]->task) {
        if (first_loop) printf("[joypad] Loop: output %s\n", outputs[i]->name);
        outputs[i]->task();
      }
    }
    first_loop = false;
  }
}

int main(void)
{
  stdio_init_all();

  printf("\n[joypad] Starting...\n");

  sleep_ms(250);  // Brief pause for stability

  // Launch Core 1 early for flash_safe_execute support
  // Core 1 will init flash safety and wait for task assignment
  printf("[joypad] Launching core1 for flash safety...\n");
  multicore_launch_core1(core1_wrapper);
  sleep_ms(10);  // Brief delay to let Core 1 initialize

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
  if (output_count > 0 && outputs[0]) {
    active_output = outputs[0];  // Set primary output for other modules
  }
  for (uint8_t i = 0; i < output_count; i++) {
    if (outputs[i] && outputs[i]->init) {
      printf("[joypad] Initializing output: %s\n", outputs[i]->name);
      outputs[i]->init();
    }
  }

  // Find core1 task from first output that has one
  // Note: Only one output can use core1 (RP2040 has 2 cores)
  for (uint8_t i = 0; i < output_count; i++) {
    if (outputs[i] && outputs[i]->core1_task) {
      printf("[joypad] Core1 task from: %s\n", outputs[i]->name);
      core1_actual_task = outputs[i]->core1_task;
      break;  // Only one core1 task possible
    }
  }

  // Signal Core 1 that task assignment is complete
  // Core 1 was launched early for flash safety, now it can run its actual task
  printf("[joypad] Signaling core1 (task: %s)\n",
         core1_actual_task ? "yes" : "idle");
  core1_task_ready = true;
  __sev();  // Send event to wake Core 1 from __wfe()

  core0_main();

  return 0;
}
