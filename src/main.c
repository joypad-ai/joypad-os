/*
 * USBRetro - Adapts USB controllers/mice/keyboards for use with
 *            retro consoles. Built for RP2040-based MCUs.
 *
 * Copyright (c) 2022-2023 Robert Dale Smith
 */

#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "core/output_interface.h"
#include "core/services/players/manager.h"
#include "core/services/leds/leds.h"
#include "core/services/storage/storage.h"
#include "usb/usbh/usbh.h"

// App layer (linked per-product)
extern void app_init(void);
extern const OutputInterface* app_get_output_interface(void);

// Cached output interface (set once at startup)
static const OutputInterface* output = NULL;

// Main loop - pinned in SRAM for consistent timing
static void __not_in_flash_func(process_signals)(void)
{
  while (1)
  {
    leds_task();
    players_task();
    storage_task();
    usbh_task();

    if (output->task) {
      output->task();
    }
  }
}

int main(void)
{
  stdio_init_all();

  printf("\n[usbretro] Starting...\n");

  sleep_ms(250);  // Brief pause for stability

  usbh_init();
  leds_init();
  storage_init();
  players_init();
  app_init();

  output = app_get_output_interface();
  output->init();

  if (output->core1_entry) {
    multicore_launch_core1(output->core1_entry);
  }

  process_signals();

  return 0;
}
