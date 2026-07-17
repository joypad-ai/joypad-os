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

// ---- REMAP INTEGRATION -----------------------------------------------------
#include "remap.h"
#include "core/services/leds/leds.h"
#include "core/services/profiles/runtime_profile.h"
#include "core/services/players/feedback.h"
// ----------------------------------------------------------------------------

// ============================================================================
// INTERNAL STATE
// ============================================================================

static gpio_device_port_t gpio_ports[GPIO_MAX_PLAYERS];
static bool initialized = false;

// ---- REMAP INTEGRATION -----------------------------------------------------
static neogeo_remap_ctx_t remap_ctx[GPIO_MAX_PLAYERS];
static neogeo_remap_t     remap_active[GPIO_MAX_PLAYERS];
static bool               remap_was_active[GPIO_MAX_PLAYERS];
// ----------------------------------------------------------------------------

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
  // ---- REMAP INTEGRATION ---------------------------------------------------
  // Suppress all GPIO output while remap collection is in progress
  if (remap_was_active[player_index]) return;
  // --------------------------------------------------------------------------

  if (runtime_profile_is_active()) return;
  const profile_t* profile = runtime_profile_get_active(OUTPUT_TARGET_GPIO);
  if (!profile) profile = profile_get_active(OUTPUT_TARGET_GPIO);
  if (!profile) return;
  profile_apply(profile, buttons, lx, ly, rx, ry, l2, r2, rz,
                &gpio_mapped[player_index]);

  const profile_output_t* mapped = &gpio_mapped[player_index];
  const gpio_device_port_t* port = &gpio_ports[player_index];
  uint32_t gpio_buttons = 0;

  // ---- REMAP INTEGRATION ---------------------------------------------------
  // Apply custom button remap on top of profile-mapped buttons.
  // IMPORTANT: remap runs against the RAW input buttons (before profile),
  // not mapped->buttons, so disabled inputs like L1/L2 still work as
  // remap sources. The 6 Neo Geo outputs are then driven directly.
  //
  // Synthesize L2/R2 digital buttons from analog trigger values so XInput
  // fight sticks with digital LT/RT can be used as remap inputs.
  // Threshold of 128 matches the profile l2_threshold/r2_threshold.
  uint32_t remap_buttons = buttons;
  if (l2 >= 128) remap_buttons |= JP_BUTTON_L2;
  if (r2 >= 128) remap_buttons |= JP_BUTTON_R2;
  uint8_t remapped = neogeo_remap_apply(&remap_active[player_index], remap_buttons);

  // Start with D-pad from profile output (handles analog stick → dpad)
  // then add S1/S2 directly from raw input — bypassing profile_apply
  // which can suppress S1 (Coin) in certain states.
  uint32_t preserve_mask = JP_BUTTON_DU | JP_BUTTON_DD |
                           JP_BUTTON_DL | JP_BUTTON_DR;
  uint32_t final_buttons = (mapped->buttons & preserve_mask);

  // Pass Coin (S1) and Start (S2) directly from raw input.
  // During the remap boot window, suppress both so that holding Start or Select
  // to trigger remap mode doesn't accidentally send a game input.
  // Once the window closes (boot_checked = true) they pass through normally.
  if (remap_ctx[player_index].boot_checked) {
      if (buttons & JP_BUTTON_S1) final_buttons |= JP_BUTTON_S1;
      if (buttons & JP_BUTTON_S2) final_buttons |= JP_BUTTON_S2;
  }

  if (remapped & (1 << NEOGEO_BTN_A))      final_buttons |= JP_BUTTON_B3;
  if (remapped & (1 << NEOGEO_BTN_B))      final_buttons |= JP_BUTTON_B4;
  if (remapped & (1 << NEOGEO_BTN_C))      final_buttons |= JP_BUTTON_R1;
  if (remapped & (1 << NEOGEO_BTN_D))      final_buttons |= JP_BUTTON_B1;
  if (remapped & (1 << NEOGEO_BTN_SELECT)) final_buttons |= JP_BUTTON_B2;
  if (remapped & (1 << NEOGEO_BTN_K3))     final_buttons |= JP_BUTTON_R2;
  // --------------------------------------------------------------------------

  gpio_buttons |= (final_buttons & JP_BUTTON_S2) ? port->mask_s2 : 0;
  gpio_buttons |= (final_buttons & JP_BUTTON_S1) ? port->mask_s1 : 0;
  gpio_buttons |= (final_buttons & JP_BUTTON_DD) ? port->mask_dd : 0;
  gpio_buttons |= (final_buttons & JP_BUTTON_DL) ? port->mask_dl : 0;
  gpio_buttons |= (final_buttons & JP_BUTTON_DU) ? port->mask_du : 0;
  gpio_buttons |= (final_buttons & JP_BUTTON_DR) ? port->mask_dr : 0;
  gpio_buttons |= (final_buttons & JP_BUTTON_B1) ? port->mask_b1 : 0;
  gpio_buttons |= (final_buttons & JP_BUTTON_B2) ? port->mask_b2 : 0;
  gpio_buttons |= (final_buttons & JP_BUTTON_B3) ? port->mask_b3 : 0;
  gpio_buttons |= (final_buttons & JP_BUTTON_B4) ? port->mask_b4 : 0;
  gpio_buttons |= (final_buttons & JP_BUTTON_L1) ? port->mask_l1 : 0;
  gpio_buttons |= (final_buttons & JP_BUTTON_R1) ? port->mask_r1 : 0;
  gpio_buttons |= (final_buttons & JP_BUTTON_L2) ? port->mask_l2 : 0;
  gpio_buttons |= (final_buttons & JP_BUTTON_R2) ? port->mask_r2 : 0;
  gpio_buttons |= (final_buttons & JP_BUTTON_L3) ? port->mask_l3 : 0;
  gpio_buttons |= (final_buttons & JP_BUTTON_R3) ? port->mask_r3 : 0;
  gpio_buttons |= (final_buttons & JP_BUTTON_L4) ? port->mask_l4 : 0;
  gpio_buttons |= (final_buttons & JP_BUTTON_R4) ? port->mask_r4 : 0;
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

  // ---- REMAP INTEGRATION ---------------------------------------------------
  for (int i = 0; i < GPIO_MAX_PLAYERS; i++) {
      neogeo_remap_ctx_init(&remap_ctx[i]);
      remap_active[i] = neogeo_remap_default;
      remap_was_active[i] = false;
  }
  // Set idle color on boot — will be overridden green when controller connects
  leds_set_color(0, 0, 32); // dim blue = waiting for controller
  // --------------------------------------------------------------------------

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
    // TE build: profile switching is intentionally disabled (single profile only).
    // Calling profile_check_switch_combo would cause SELECT to be suppressed
    // from output after being held 2s, breaking Coin input during normal play.
    // runtime_profile_check_combo handles the remap trigger via SELECT alone.
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

  // ---- REMAP INTEGRATION ---------------------------------------------------
  static int last_players_count = 0;

  if (playersCount != last_players_count) {
      for (int i = 0; i < GPIO_MAX_PLAYERS; i++) {
          if (players[i].dev_addr == -1 && i < last_players_count) {
              // Controller disconnected — reset remap and clear runtime profile
              neogeo_remap_ctx_init(&remap_ctx[i]);
              remap_active[i] = neogeo_remap_default;
              remap_was_active[i] = false;
              runtime_profile_clear();
              leds_set_color(0, 0, 32); // dim blue = waiting for controller
              printf("[te] Player %d disconnected: remap reset\n", i);
          } else if (players[i].dev_addr != -1 && playersCount > last_players_count) {
              // Controller connected — open fresh boot window, go purple
              neogeo_remap_ctx_init(&remap_ctx[i]);
              remap_active[i] = neogeo_remap_default;
              remap_was_active[i] = false;
              leds_set_color(128, 0, 128); // purple = remap window open
              printf("[te] Player %d connected: remap window open\n", i);
          }
      }
  }
  last_players_count = playersCount;

  // Track boot window closing — transition from purple to green
  static bool was_boot_checked = false;
  if (playersCount > 0) {
      bool now_checked = remap_ctx[0].boot_checked;
      if (now_checked && !was_boot_checked && !remap_was_active[0]) {
          leds_set_color(0, 180, 0); // green = window closed, normal play
      }
      was_boot_checked = now_checked;
  } else {
      was_boot_checked = false;
  }

  // Advance remap state machine every task loop
  if (playersCount > 0) {
      uint8_t p = 0;
      // Synthesize L2/R2 digital buttons from analog values so XInput
      // fight sticks with digital LT/RT work as remap collection inputs
      uint32_t remap_buttons = last_buttons;
      if (last_l2 >= 128) remap_buttons |= JP_BUTTON_L2;
      if (last_r2 >= 128) remap_buttons |= JP_BUTTON_R2;
      bool in_remap = neogeo_remap_update(
          &remap_ctx[p],
          remap_buttons,
          &remap_active[p]
      );

      if (in_remap && !remap_was_active[p]) {
          printf("[te] Remap mode active — press 6 buttons in order\n");
          // Short rumble pulse to signal remap mode entry
          feedback_set_rumble(p, 255, 255);
          remap_ctx[p].rumble_start_ms = platform_time_ms();
          // Controller LED — yellow to match NeoPixel
          feedback_set_led_rgb(p, 255, 180, 0);
      }

      // Stop rumble after 200ms
      if (remap_ctx[p].rumble_start_ms != 0) {
          if ((platform_time_ms() - remap_ctx[p].rumble_start_ms) >= 200) {
              feedback_set_rumble(p, 0, 0);
              remap_ctx[p].rumble_start_ms = 0;
          }
      }

      // Flash yellow on NeoPixel while collecting, red briefly on duplicate
      if (in_remap) {
          uint32_t now = platform_time_ms();
          if (remap_ctx[p].error_flash_ms != 0 &&
              (now - remap_ctx[p].error_flash_ms) < 500) {
              // Red flash for 500ms to signal invalid input
              leds_set_color(180, 0, 0);
              feedback_set_led_rgb(p, 180, 0, 0);
          } else {
              remap_ctx[p].error_flash_ms = 0;
              bool flash_on = (now / 250) % 2;
              leds_set_color(flash_on ? 255 : 0, flash_on ? 180 : 0, 0);
              feedback_set_led_rgb(p, 255, 180, 0);
          }
      }

      // Detect completion or abort
      if (!in_remap && remap_was_active[p]) {
          if (remap_ctx[p].completed) {
              printf("[te] Remap complete\n");
              leds_set_color(0, 180, 0);
          } else {
              printf("[te] Remap aborted\n");
              leds_set_color(180, 0, 0);
          }
          // Restore controller LED to normal player color
          feedback_set_led_player(p, p + 1);
      }

      remap_was_active[p] = in_remap;
  }
  // --------------------------------------------------------------------------
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
