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

// Native console output (e.g. gamecube_output_interface). Apps with a
// native console output set this so the web config can configure pins/modes
// even when running in CDC config mode (no console plugged in).
const OutputInterface* native_output = NULL;

// Store core1 task for wrapper - can be set after Core 1 launch
static volatile void (*core1_actual_task)(void) = NULL;
static volatile bool core1_task_ready = false;

// Core 1 wrapper - initializes flash safety, then waits for and runs actual task
static void core1_wrapper(void) {
  // Initialize multicore lockout for flash_safe_execute to work
  // This allows Core 0 to safely write to flash while Core 1 is running
  // NOTE: Skip for timing-critical output protocols (Nuon polyface, etc.)
  // The lockout interrupt can pause Core 1 mid-protocol and break communication.
#ifndef CONFIG_NO_FLASH_LOCKOUT
  flash_safe_execute_core_init();
#endif

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

    // Poll all input interfaces FIRST so output reads freshest data this iteration
    // (Eliminates one-loop-iteration latency vs polling input after output)
    for (uint8_t i = 0; i < input_count; i++) {
      if (inputs[i] && inputs[i]->task) {
        if (first_loop) printf("[joypad] Loop: input %s\n", inputs[i]->name);
        inputs[i]->task();
      }
    }

    // Service USB immediately after input polling. Input drivers may have
    // queued CDC streaming events via router_submit_input → cdc_data_write,
    // and there's no point letting them sit in the TX buffer through the
    // output task and app_task — push them out now so web-config sees
    // input updates with single-iteration latency. Mirrors the same fix
    // the ESP32 main loop has.
#if CFG_TUD_ENABLED
    tud_task();
#endif

    // Run output interface tasks (reads router state populated by input above)
    for (uint8_t i = 0; i < output_count; i++) {
      if (outputs[i] && outputs[i]->task) {
        if (first_loop) printf("[joypad] Loop: output %s\n", outputs[i]->name);
        outputs[i]->task();
      }
    }

    if (first_loop) printf("[joypad] Loop: app\n");
    app_task();
    first_loop = false;
  }
}

int main(void)
{
#ifdef BOARD_LED_PIN
  // Early boot indicator — toggle LED before any PIO init
  gpio_init(BOARD_LED_PIN);
  gpio_set_dir(BOARD_LED_PIN, GPIO_OUT);
  gpio_put(BOARD_LED_PIN, 1);
#endif

  // ========================================================================
  // PHASE 1: Time-critical — get Core 1 listening ASAP (before stdio/printf)
  // Console probes happen ~100-500ms after power-on. Every ms counts.
  // ========================================================================

  // Launch Core 1 for flash_safe_execute support
  multicore_launch_core1(core1_wrapper);

  // PIO/joybus init — no dependency on stdio, flash, or profiles
  outputs = app_get_output_interfaces(&output_count);
  if (output_count > 0 && outputs[0]) {
    active_output = outputs[0];
  }
  // Auto-discover native console output if any active output has the
  // get/set_native_config callbacks. Apps that hide their console output
  // in CDC config mode override this directly in app_init().
  for (uint8_t i = 0; i < output_count; i++) {
    if (outputs[i] && (outputs[i]->get_native_config || outputs[i]->set_native_config)) {
      native_output = outputs[i];
      break;
    }
  }
  for (uint8_t i = 0; i < output_count; i++) {
    if (outputs[i] && outputs[i]->init) {
      outputs[i]->init();
    }
  }

  // Signal Core 1 to start listening
  for (uint8_t i = 0; i < output_count; i++) {
    if (outputs[i] && outputs[i]->core1_task) {
      core1_actual_task = outputs[i]->core1_task;
      break;
    }
  }
  core1_task_ready = true;
  __sev();

  // ========================================================================
  // PHASE 2: Non-critical init — Core 1 is already listening
  // ========================================================================

  stdio_init_all();
  printf("\n[joypad] Output: %s, Core1: %s\n",
         output_count > 0 ? outputs[0]->name : "none",
         core1_actual_task ? "active" : "idle");

  // Now initialize core services and app (slower — BT, USB host, etc.)
  // Core 1 is already listening for console probes while this runs.
  leds_init();
  storage_init();
  players_init();
  app_init();

  // Render one LED frame before input init (which may block for seconds on MAX3421E)
  leds_task();

  // Get and initialize input interfaces
  inputs = app_get_input_interfaces(&input_count);
  for (uint8_t i = 0; i < input_count; i++) {
    if (inputs[i] && inputs[i]->init) {
      printf("[joypad] Initializing input: %s\n", inputs[i]->name);
      inputs[i]->init();
    }
  }

  core0_main();

  return 0;
}
