/* 
 * USBRetro - Adapts most USB controllers/mice/keyboards for use with
 *            various retro consoles and computers. Built specifically
 *            for the Raspberry Pi Pico or other RP2040 based MCUs.
 *
 * Copyright (c) 2022-2023 Robert Dale Smith for Controller Adapter
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "tusb.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "core/services/players/manager.h"
#include "core/services/codes/codes.h"

// Output interface abstraction
#include "core/output_interface.h"

// USB host layer (HID + X-input handling)
#include "usb/usbh/usbh.h"

// App provides output interface (replaces compile-time selection in output.c)
extern const OutputInterface* app_get_output_interface(void);

// NeoPixel LED control
#include "core/services/leds/ws2812.h"

// Flash storage (profile persistence)
#include "core/services/storage/flash.h"

// App layer initialization (every product has an app)
extern void app_init(void);

/*------------- MAIN -------------*/

// note that "__not_in_flash_func" functions are loaded
// and "pinned" in SRAM - not paged in/out from XIP flash
//

//
// process_signals - inner-loop processing of events:
//                   - USB polling
//                   - event processing
//
static void __not_in_flash_func(process_signals)(void)
{
  while (1)
  {
    // tinyusb host task
    tuh_task();

    // neopixel task
    neopixel_task(playersCount);

    // players task (feedback state machine for rumble and player LED patterns)
    players_task();

    // flash settings task (debounced flash writes for profile persistence)
    flash_task();

    // USB host feedback task (rumble, LEDs, triggers for HID and X-input)
    usbh_task();

    // Console-specific periodic task (if needed)
    const OutputInterface* output = app_get_output_interface();
    if (output->task) {
      output->task();
    }
  }
}

int main(void)
{
  stdio_init_all();

  // Get output interface from app layer (replaces compile-time CONFIG_* selection)
  const OutputInterface* active_output = app_get_output_interface();

  printf("\nUSB_RETRO::%s\n\n", active_output->name);

  // Pause briefly for stability before starting USB activity
  sleep_ms(250);

  usbh_init(); // init USB host layer (HID device registry)

  tusb_init(); // init tinyusb for usb host input

  neopixel_init(); // init multi-color status led

  players_init(); // init multi-player management (includes feedback subsystem)

  // Initialize app layer (Phase 5)
  // Every product MUST have an app that configures router/players/profiles
  // Apps own configuration (router, players, profiles), firmware provides runtime
  // If app_init() is not linked in, linker will fail with clear error
  app_init();

  // Initialize active output (console-specific or USB device)
  active_output->init();

  // Launch Core1 for console output protocol (if needed)
  if (active_output->core1_entry) {
    multicore_launch_core1(active_output->core1_entry);
  }

  process_signals();

  return 0;
}

//--------------------------------------------------------------------+
// TinyUSB Callbacks
//--------------------------------------------------------------------+
#if CFG_TUH_HID

void tuh_mount_cb(uint8_t dev_addr)
{
  // application set-up
  printf("A device with address %d is mounted\r\n", dev_addr);
}

void tuh_umount_cb(uint8_t dev_addr)
{
  // application tear-down
  printf("A device with address %d is unmounted \r\n", dev_addr);

  remove_players_by_address(dev_addr, -1);

  // Reset test mode when device disconnects
  codes_reset_test_mode();
}

#endif
