// gpio_device.c

#include "gpio_device.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/structs/iobank0.h"
#include "hardware/structs/padsbank0.h"
#include "hardware/structs/sio.h"

#if CFG_TUSB_DEBUG >= 1
#include "hardware/uart.h"
#endif

#include "core/router/router.h"
#include "core/input_event.h"
#include "core/services/players/manager.h"
#include "core/services/profiles/profile.h"
#include "core/services/profiles/profile_indicator.h"
#include "core/services/profiles/runtime_profile.h"
#include "core/services/codes/codes.h"

// ============================================================================
// INTERNAL STATE
// ============================================================================

static gpio_device_port_t gpio_ports[GPIO_MAX_PLAYERS];
static bool initialized = false;

// Last raw input state received from the tap callback.
// Used by gpio_device_task() for combo detection, cheat codes, and
// autofire periodic re-application (oscillation while button is held).
static uint32_t tap_last_buttons = 0;
static uint8_t  tap_last_lx      = 128;
static uint8_t  tap_last_ly      = 128;
static uint8_t  tap_last_rx      = 128;
static uint8_t  tap_last_ry      = 128;
static uint8_t  tap_last_l2      = 0;
static uint8_t  tap_last_r2      = 0;
static uint8_t  tap_last_rz      = 0;
static bool     tap_has_update   = false;

// Profile output buffer shared between tap callback and task loop.
static profile_output_t gpio_mapped[GPIO_MAX_PLAYERS];

// ============================================================================
// PROFILE SYSTEM (Delegates to core profile service)
// ============================================================================

static uint8_t gpio_get_player_count_for_profile(void) {
    return router_get_player_count(OUTPUT_TARGET_GPIO);
}

static uint8_t gpio_get_profile_count(void) {
    return profile_get_count(OUTPUT_TARGET_GPIO);
}

static uint8_t gpio_get_active_profile(void) {
    return profile_get_active_index(OUTPUT_TARGET_GPIO);
}

static void gpio_set_active_profile(uint8_t index) {
    profile_set_active(OUTPUT_TARGET_GPIO, index);
}

static const char* gpio_get_profile_name(uint8_t index) {
    return profile_get_name(OUTPUT_TARGET_GPIO, index);
}

// ============================================================================
// Internal GPIO Functions
// ============================================================================

static void gpioport_gpio_init(bool active_high)
{
    uint32_t gpio_mask = 0;

    for (int i = 0; i < GPIO_MAX_PLAYERS; i++) {
      gpio_device_port_t* port = &gpio_ports[i];
      gpio_mask |= port->gpio_mask;
    }

    gpio_init_mask(gpio_mask);
    gpio_clr_mask(gpio_mask);
    for (int i = 0; i < 30; i++) {
        if (gpio_mask & (1u << i)) {
            gpio_disable_pulls(i);
        }
    }

    if (active_high) {
      gpio_set_dir_out_masked(gpio_mask);
    } else {
      gpio_set_dir_in_masked(gpio_mask);
    }
}

// ============================================================================
// GPIO PORT Functions
// ============================================================================
void gpioport_init(gpio_device_port_t* port, gpio_device_config_t* config, bool active_high)
{
    port->active_high = active_high;
    port->gpio_mask = 0;

    port->mask_du = GPIO_MASK(config->pin_du);
    port->mask_dd = GPIO_MASK(config->pin_dd);
    port->mask_dr = GPIO_MASK(config->pin_dr);
    port->mask_dl = GPIO_MASK(config->pin_dl);
    port->mask_b1 = GPIO_MASK(config->pin_b1);
    port->mask_b2 = GPIO_MASK(config->pin_b2);
    port->mask_b3 = GPIO_MASK(config->pin_b3);
    port->mask_b4 = GPIO_MASK(config->pin_b4);
    port->mask_l1 = GPIO_MASK(config->pin_l1);
    port->mask_r1 = GPIO_MASK(config->pin_r1);
    port->mask_l2 = GPIO_MASK(config->pin_l2);
    port->mask_r2 = GPIO_MASK(config->pin_r2);
    port->mask_s1 = GPIO_MASK(config->pin_s1);
    port->mask_s2 = GPIO_MASK(config->pin_s2);
    port->mask_a1 = GPIO_MASK(config->pin_a1);
    port->mask_a2 = GPIO_MASK(config->pin_a2);
    port->mask_l3 = GPIO_MASK(config->pin_l3);
    port->mask_r3 = GPIO_MASK(config->pin_r3);
    port->mask_l4 = GPIO_MASK(config->pin_l4);
    port->mask_r4 = GPIO_MASK(config->pin_r4);

    port->gpio_mask = (port->mask_du | port->mask_dd | port->mask_dr | port->mask_dl |
                       port->mask_b1 | port->mask_b2 | port->mask_b3 | port->mask_b4 |
                       port->mask_l1 | port->mask_r1 | port->mask_l2 | port->mask_r2 |
                       port->mask_s1 | port->mask_s2 | port->mask_a1 | port->mask_a2 |
                       port->mask_l3 | port->mask_r3 | port->mask_l4 | port->mask_r4);

    port->last_read  = 0;
}

// ============================================================================
// PUSH-BASED OUTPUT VIA ROUTER TAP
// ============================================================================

// Select the active profile (runtime override → normal fallback), apply it,
// and write GPIO for one player. Suppressed during mapping mode.
static void gpio_apply_output(uint8_t player_index,
                                  uint32_t buttons,
                                  uint8_t lx, uint8_t ly,
                                  uint8_t rx, uint8_t ry,
                                  uint8_t l2, uint8_t r2, uint8_t rz)
{
  if (runtime_profile_is_active()) return;
  const profile_t* profile = runtime_profile_get_active(OUTPUT_TARGET_GPIO);
  if (!profile) profile = profile_get_active(OUTPUT_TARGET_GPIO);
  if (!profile) return;
  profile_apply(profile, buttons, lx, ly, rx, ry, l2, r2, rz,
                &gpio_mapped[player_index]);

  const profile_output_t* mapped = &gpio_mapped[player_index];
  const gpio_device_port_t* port = &gpio_ports[player_index];
  uint32_t gpio_buttons = 0;
  gpio_buttons |= (mapped->buttons & JP_BUTTON_S2) ? port->mask_s2 : 0;
  gpio_buttons |= (mapped->buttons & JP_BUTTON_S1) ? port->mask_s1 : 0;
  gpio_buttons |= (mapped->buttons & JP_BUTTON_DD) ? port->mask_dd : 0;
  gpio_buttons |= (mapped->buttons & JP_BUTTON_DL) ? port->mask_dl : 0;
  gpio_buttons |= (mapped->buttons & JP_BUTTON_DU) ? port->mask_du : 0;
  gpio_buttons |= (mapped->buttons & JP_BUTTON_DR) ? port->mask_dr : 0;
  gpio_buttons |= (mapped->buttons & JP_BUTTON_B1) ? port->mask_b1 : 0;
  gpio_buttons |= (mapped->buttons & JP_BUTTON_B2) ? port->mask_b2 : 0;
  gpio_buttons |= (mapped->buttons & JP_BUTTON_B3) ? port->mask_b3 : 0;
  gpio_buttons |= (mapped->buttons & JP_BUTTON_B4) ? port->mask_b4 : 0;
  gpio_buttons |= (mapped->buttons & JP_BUTTON_L1) ? port->mask_l1 : 0;
  gpio_buttons |= (mapped->buttons & JP_BUTTON_R1) ? port->mask_r1 : 0;
  gpio_buttons |= (mapped->buttons & JP_BUTTON_L2) ? port->mask_l2 : 0;
  gpio_buttons |= (mapped->buttons & JP_BUTTON_R2) ? port->mask_r2 : 0;
  gpio_buttons |= (mapped->buttons & JP_BUTTON_L3) ? port->mask_l3 : 0;
  gpio_buttons |= (mapped->buttons & JP_BUTTON_R3) ? port->mask_r3 : 0;
  gpio_buttons |= (mapped->buttons & JP_BUTTON_L4) ? port->mask_l4 : 0;
  gpio_buttons |= (mapped->buttons & JP_BUTTON_R4) ? port->mask_r4 : 0;
  gpio_buttons |= (mapped->left_x < 64)  ? port->mask_dl : 0;
  gpio_buttons |= (mapped->left_x > 192) ? port->mask_dr : 0;
  gpio_buttons |= (mapped->left_y < 64)  ? port->mask_du : 0;
  gpio_buttons |= (mapped->left_y > 192) ? port->mask_dd : 0;
  if (port->active_high) {
    gpio_put_masked(port->gpio_mask, gpio_buttons);
  } else {
    sio_hw->gpio_oe_set = gpio_buttons;
    sio_hw->gpio_oe_clr = port->gpio_mask & (~gpio_buttons);
  }
}

// Tap callback — fires immediately from router_submit_input().
// Must be fast: just store state + apply profile + update GPIO. No printf or blocking.
static void __not_in_flash_func(gpio_tap_callback)(output_target_t output,
                                                      uint8_t player_index,
                                                      const input_event_t* event)
{
  (void)output;

  if (playersCount == 0 || player_index >= GPIO_MAX_PLAYERS) return;

  // Store raw input for combo detection in task loop and autofire re-apply
  tap_last_buttons = event->buttons;
  tap_last_lx      = event->analog[ANALOG_LX];
  tap_last_ly      = event->analog[ANALOG_LY];
  tap_last_rx      = event->analog[ANALOG_RX];
  tap_last_ry      = event->analog[ANALOG_RY];
  tap_last_l2      = event->analog[ANALOG_L2];
  tap_last_r2      = event->analog[ANALOG_R2];
  tap_last_rz      = event->analog[ANALOG_RZ];
  tap_has_update   = true;

  gpio_apply_output(player_index,
                    event->buttons,
                    event->analog[ANALOG_LX], event->analog[ANALOG_LY],
                    event->analog[ANALOG_RX], event->analog[ANALOG_RY],
                    event->analog[ANALOG_L2], event->analog[ANALOG_R2], 
                    event->analog[ANALOG_RZ]);
}

// init for GPIO communication
void gpio_device_init()
{
  profile_set_player_count_callback(gpio_get_player_count_for_profile);
  runtime_profile_set_player_count_callback(gpio_get_player_count_for_profile);

  router_set_tap_exclusive(OUTPUT_TARGET_GPIO, gpio_tap_callback);

  #if CFG_TUSB_DEBUG >= 1
  uart_init(UART_ID, BAUD_RATE);
  gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
  #endif
}

void gpio_device_init_pins(gpio_device_config_t* config, bool active_high){
  for (int i = 0; i < GPIO_MAX_PLAYERS; i++) {
    gpio_device_port_t* port = &gpio_ports[i];
    gpio_device_config_t* port_config = &config[i];
    gpioport_init(port, port_config, active_high);
  }
  gpioport_gpio_init(active_high);
  initialized = true;
}

// Task loop — handles non-latency-critical work (combo detection, cheat codes).
// GPIO updates are now handled by the tap callback above.
void gpio_device_task()
{
  static uint32_t last_buttons = 0;
  static uint8_t  last_l2      = 0;
  static uint8_t  last_r2      = 0;
  static uint8_t  last_lx      = 128;
  static uint8_t  last_ly      = 128;
  static uint8_t  last_rx      = 128;
  static uint8_t  last_ry      = 128;
  static uint8_t  last_rz      = 0;
  bool had_update = false;

  // Pick up raw input state from tap callback
  if (tap_has_update) {
    last_buttons   = tap_last_buttons;
    last_l2        = tap_last_l2;
    last_r2        = tap_last_r2;
    last_lx        = tap_last_lx;
    last_ly        = tap_last_ly;
    last_rx        = tap_last_rx;
    last_ry        = tap_last_ry;
    last_rz        = tap_last_rz;
    tap_has_update = false;
    had_update     = true;
  }

  if (playersCount > 0) {
    // Profile-switch combo is suppressed while mapping so SELECT
    // is exclusively reserved for the mapping trigger/cancel.
    if (!runtime_profile_is_active()) {
      uint8_t before = profile_get_active_index(OUTPUT_TARGET_GPIO);
      profile_check_switch_combo(last_buttons);
      if (profile_get_active_index(OUTPUT_TARGET_GPIO) != before) {
        runtime_profile_clear();
      }
    }
    runtime_profile_check_combo(last_buttons, last_l2, last_r2);

    // Periodic re-apply: profile_apply reads platform_time_ms() so autofire
    // oscillates even when the USB driver stops sending reports (button held).
    // gpio_apply_output handles both runtime and normal profiles uniformly.
    for (int i = 0; i < playersCount && i < GPIO_MAX_PLAYERS; i++) {
      gpio_apply_output(i,
                        last_buttons,
                        last_lx, last_ly,
                        last_rx, last_ry,
                        last_l2, last_r2, last_rz);
    }
  }

  // Run cheat code detection when we had new input
  if (had_update && playersCount > 0) {
    codes_process_raw(last_buttons);
  }
}

//-----------------------------------------------------------------------------
// Core1 Entry Point
//-----------------------------------------------------------------------------

void __not_in_flash_func(core1_task)(void) {
  while (1) {
    sleep_ms(100);
  }
}

// Input flow: USB drivers → router_submit_input() → tap callback → GPIO (immediate)
//             Task loop handles combo detection and cheat codes

// ============================================================================
// OUTPUT INTERFACE
// ============================================================================

#include "core/output_interface.h"

const OutputInterface gpio_output_interface = {
    .name = "GPIO",
    .target = OUTPUT_TARGET_GPIO,
    .init = gpio_device_init,
    .core1_task = NULL,
    .task = gpio_device_task,
    .get_rumble = NULL,
    .get_player_led = NULL,
    .get_profile_count = gpio_get_profile_count,
    .get_active_profile = gpio_get_active_profile,
    .set_active_profile = gpio_set_active_profile,
    .get_profile_name = gpio_get_profile_name,
    .get_trigger_threshold = NULL,
};
