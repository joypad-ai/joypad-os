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
#include "globals.h"

bool update_pending = false;
uint8_t gc_rumble = 0;
uint8_t gc_kb_led = 0;

// Output interface abstraction
#include "common/output_interface.h"

// App provides output interface (replaces compile-time selection in output.c)
extern const OutputInterface* app_get_output_interface(void);

extern void hid_init(void);
extern void hid_task(uint8_t rumble, uint8_t leds, uint8_t trigger_threshold, uint8_t test);
extern void xinput_task(uint8_t rumble);

extern void neopixel_init(void);
extern void neopixel_task(int pat);

extern void profile_indicator_init(void);
extern void profile_indicator_task(void);
extern uint8_t profile_indicator_get_rumble(void);
extern uint8_t profile_indicator_get_player_led(uint8_t player_count);

extern void players_init(void);

// Generic flash settings (profile persistence) - used by all consoles with profiles
extern void flash_settings_task(void);

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

    // profile indicator task (rumble and player LED patterns)
    profile_indicator_task();

    // flash settings task (debounced flash writes for profile persistence)
    // Generic service used by all consoles with profiles (3DO, GameCube, etc.)
    flash_settings_task();

    // Combine GameCube console rumble with profile indicator rumble
    uint8_t combined_rumble = gc_rumble | profile_indicator_get_rumble();

    // Get player LED value (combines keyboard mode LED with profile indicator)
    uint8_t player_led = profile_indicator_get_player_led(playersCount) | gc_kb_led;

    // Get adaptive trigger threshold from universal profile system (DualSense L2/R2)
    // Apps register their profiles during app_init(), system provides threshold
    extern uint8_t profile_get_l2_threshold(void);
    uint8_t trigger_threshold = profile_get_l2_threshold();

    // test pattern counter (managed by application layer)
    static uint8_t test_counter = 0;
    if (is_fun) test_counter++;

    // xinput rumble task
    xinput_task(combined_rumble);

#if CFG_TUH_HID
    // hid_device rumble/led task
    hid_task(combined_rumble, player_led, trigger_threshold, test_counter);
#endif

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

  hid_init(); // init hid device interfaces

  tusb_init(); // init tinyusb for usb host input

  neopixel_init(); // init multi-color status led

  profile_indicator_init(); // init profile indicator (rumble and player LED)

  players_init(); // init multi-player management

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

  // if ((--playersCount) < 0) playersCount = 0;
  remove_players_by_address(dev_addr, -1);

  // resets fun
  is_fun = false;
}

#endif
