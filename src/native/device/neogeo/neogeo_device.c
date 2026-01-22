// neogeo_device.c

#include "neogeo_device.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/structs/iobank0.h"
#include "hardware/structs/padsbank0.h"
#include "hardware/structs/sio.h"

// Early init constructor - runs before main() to set output pins HIGH
// This prevents "all buttons pressed" state during boot
__attribute__((constructor(101)))
static void neogeo_early_gpio_init(void)
{
    // Direct register access for fastest possible init
    // Set output pins as outputs with HIGH value
    
    // Enable outputs and set HIGH
    sio_hw->gpio_oe_set = NEOGEO_GPIO_MASK;
    sio_hw->gpio_set = NEOGEO_GPIO_MASK;
}

#if CFG_TUSB_DEBUG >= 1
#include "hardware/uart.h"
#endif

#include "core/router/router.h"
#include "core/input_event.h"
#include "core/services/players/manager.h"
#include "core/services/codes/codes.h"

// Forward declarations
void read_inputs(void);

// init for NEOGEO communication
void neogeo_init()
{
  // Set output pins HIGH immediately to prevent "all buttons pressed" during boot
  gpio_init_mask(NEOGEO_GPIO_MASK);
  gpio_set_dir_out_masked(NEOGEO_GPIO_MASK);
  gpio_put_masked(NEOGEO_GPIO_MASK, NEOGEO_GPIO_MASK);

  #if CFG_TUSB_DEBUG >= 1
  // Initialize chosen UART
  uart_init(UART_ID, BAUD_RATE);

  // Set the GPIO function for the UART pins
  gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
  #endif
}

// task process - runs on core0, keeps cached button values fresh
void neogeo_task()
{  
  // Continuously read input and cache it - core1 will use cached values
  read_inputs();
}

//

//-----------------------------------------------------------------------------
// Core1 Entry Point
//-----------------------------------------------------------------------------

void __not_in_flash_func(core1_task)(void) {
  while (1) {
    sleep_ms(100);
  }
}

//
// read_inputs - reads button state from router and caches it (HEAVY - once per scan)
//
void __not_in_flash_func(read_inputs)(void)
{
  int16_t hotkey = 0;

  for (unsigned short int i = 0; i < MAX_PLAYERS; ++i)
  {
    const input_event_t* event = router_get_output(OUTPUT_TARGET_NEOGEO, i);

    // Player slot out of range - reset to neutral (including mouse state)
    if (i >= playersCount) {
      continue;
    }
    
    // No new event - keep existing state (important for mouse!)
    if (!event) {
      continue;
    }

    uint32_t neogeo_buttons = 0;

    // Mapping the buttons (active-low: 0 = pressed)
    neogeo_buttons |= (event->buttons & JP_BUTTON_S2) ? NEOGEO_S2_PIN : 0;  // Option -> START
    neogeo_buttons |= (event->buttons & JP_BUTTON_S1) ? NEOGEO_S1_PIN : 0;  // Share -> SELECT
    neogeo_buttons |= (event->buttons & JP_BUTTON_DD) ? NEOGEO_DD_PIN : 0;  // Dpad Down -> D-DOWN
    neogeo_buttons |= (event->buttons & JP_BUTTON_DL) ? NEOGEO_DL_PIN : 0;  // Dpad Left -> D-LEFT
    neogeo_buttons |= (event->buttons & JP_BUTTON_DU) ? NEOGEO_DU_PIN : 0;  // Dpad Up -> D-UP
    neogeo_buttons |= (event->buttons & JP_BUTTON_DR) ? NEOGEO_DR_PIN : 0;  // Dpad Right -> D-RIGHT
    neogeo_buttons |= (event->buttons & JP_BUTTON_B3) ? NEOGEO_B1_PIN : 0;  // Square -> B1
    neogeo_buttons |= (event->buttons & JP_BUTTON_B4) ? NEOGEO_B2_PIN : 0;  // Triangle -> B2
    neogeo_buttons |= (event->buttons & JP_BUTTON_R1) ? NEOGEO_B3_PIN : 0;  // R1 -> B3
    neogeo_buttons |= (event->buttons & JP_BUTTON_B1) ? NEOGEO_B4_PIN : 0;  // Cross -> B4
    neogeo_buttons |= (event->buttons & JP_BUTTON_B2) ? NEOGEO_B5_PIN : 0;  // Circle -> B5
    neogeo_buttons |= (event->buttons & JP_BUTTON_R2) ? NEOGEO_B6_PIN : 0;  // R2 -> B6
    // D-pad from left analog stick (threshold at 64/192 from center 128)
    // HID convention: 0=up, 128=center, 255=down
    neogeo_buttons |= (event->analog[0] < 64)  ? NEOGEO_DL_PIN : 0;  // Dpad Left -> D-LEFT
    neogeo_buttons |= (event->analog[0] > 192) ? NEOGEO_DR_PIN : 0;  // Dpad Right -> D-RIGHT
    neogeo_buttons |= (event->analog[1] < 64)  ? NEOGEO_DU_PIN : 0;  // Dpad Up -> D-UP
    neogeo_buttons |= (event->analog[1] > 192) ? NEOGEO_DD_PIN : 0;  // Dpad Down -> D-DOWN

    gpio_put_masked(NEOGEO_GPIO_MASK, ~neogeo_buttons);
  }

  codes_task();
}

// post_input_event removed - replaced by router architecture
// Input flow: USB drivers → router_submit_input() → router → router_get_output() → update_output()

// ============================================================================
// OUTPUT INTERFACE
// ============================================================================

#include "core/output_interface.h"

const OutputInterface neogeo_output_interface = {
    .name = "NEOGEO",
    .target = OUTPUT_TARGET_NEOGEO,
    .init = neogeo_init,
    .core1_task = core1_task,
    .task = neogeo_task,  // NEOGEO needs periodic scan detection task
    .get_rumble = NULL,
    .get_player_led = NULL,
    // No profile system - NEOGEO uses fixed button mapping
    .get_profile_count = NULL,
    .get_active_profile = NULL,
    .set_active_profile = NULL,
    .get_profile_name = NULL,
    .get_trigger_threshold = NULL,
};
