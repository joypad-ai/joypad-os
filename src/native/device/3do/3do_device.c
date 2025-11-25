// 3do_device.c - 3DO console output implementation for USBRetro
// Based on FCare/USBTo3DO protocol implementation
// Adapted to USBRetro architecture

#include "3do_device.h"
#include "3do_config.h"
#include "core/services/storage/flash.h"
#include "core/services/players/feedback.h"
#include "core/router/router.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/structs/bus_ctrl.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "pico/multicore.h"
#include <string.h>

#if CFG_TUSB_DEBUG >= 1
#include "hardware/uart.h"
#endif

// ============================================================================
// PROFILE SYSTEM
// ============================================================================

// All available profiles (stored in flash, const = read-only)
static const tdo_profile_t profiles[TDO_PROFILE_COUNT] = {
    TDO_PROFILE_DEFAULT,      // Profile 0
    TDO_PROFILE_FIGHTING,     // Profile 1
    TDO_PROFILE_SHOOTER,      // Profile 2
};

// Active profile pointer (4 bytes of RAM, points to flash data)
static const tdo_profile_t* active_profile = &profiles[TDO_DEFAULT_PROFILE_INDEX];

// Current profile index (for cycling through profiles)
static uint8_t active_profile_index = TDO_DEFAULT_PROFILE_INDEX;

extern void neopixel_indicate_profile(uint8_t profile_index);
extern bool neopixel_is_indicating(void);

// PIO state machines
PIO pio;
uint sm_sampling = 0;
uint sm_output = 0;

// Report buffers (initialize with 0xFF = all buttons not pressed in active-low logic)
uint8_t current_reports[MAX_PLAYERS][9];
uint8_t report_sizes[MAX_PLAYERS] = {0};
volatile bool device_attached[MAX_PLAYERS] = {false};
uint8_t controller_buffer[201];

// Extension controller tracking
static uint8_t extension_controller_count = 0;

// Get total controller count (USB + extension)
uint8_t get_total_3do_controller_count(void) {
  extern uint8_t max_usb_controller;
  return max_usb_controller + extension_controller_count;
}

// DMA channels
typedef enum {
  CHAN_OUTPUT = 0,
  CHAN_INPUT,
  CHAN_MAX
} DMA_chan_t;

int dma_channels[CHAN_MAX];
uint instr_jmp[CHAN_MAX];
dma_channel_config dma_config[CHAN_MAX];

uint8_t max_usb_controller = 0;
volatile bool update_report_flag = false;
volatile uint32_t pio_irq_count = 0;  // Track PIO IRQ calls (incremented in IRQ, read from task)

// Forward declarations
static void start_dma_transfer(uint8_t channel, uint8_t *buffer, uint32_t count);
static void report_done(uint8_t instance);

//-----------------------------------------------------------------------------
// DMA Setup Functions
//-----------------------------------------------------------------------------

void setup_3do_dma_output(void) {
  dma_channels[CHAN_OUTPUT] = dma_claim_unused_channel(true);
  dma_config[CHAN_OUTPUT] = dma_channel_get_default_config(dma_channels[CHAN_OUTPUT]);

  channel_config_set_transfer_data_size(&dma_config[CHAN_OUTPUT], DMA_SIZE_8);
  channel_config_set_read_increment(&dma_config[CHAN_OUTPUT], true);
  channel_config_set_write_increment(&dma_config[CHAN_OUTPUT], false);
  channel_config_set_irq_quiet(&dma_config[CHAN_OUTPUT], true);
  channel_config_set_dreq(&dma_config[CHAN_OUTPUT], DREQ_PIO1_TX0 + sm_output);

  dma_channel_set_write_addr(dma_channels[CHAN_OUTPUT], &pio1->txf[sm_output], false);
  dma_channel_set_config(dma_channels[CHAN_OUTPUT], &dma_config[CHAN_OUTPUT], false);

  // Set DMA bus priority
  bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;
}

void setup_3do_dma_input(void) {
  dma_channels[CHAN_INPUT] = dma_claim_unused_channel(true);
  dma_config[CHAN_INPUT] = dma_channel_get_default_config(dma_channels[CHAN_INPUT]);

  channel_config_set_transfer_data_size(&dma_config[CHAN_INPUT], DMA_SIZE_8);
  channel_config_set_read_increment(&dma_config[CHAN_INPUT], false);
  channel_config_set_write_increment(&dma_config[CHAN_INPUT], true);
  channel_config_set_irq_quiet(&dma_config[CHAN_INPUT], true);
  channel_config_set_dreq(&dma_config[CHAN_INPUT], DREQ_PIO1_RX0 + sm_output);

  dma_channel_set_read_addr(dma_channels[CHAN_INPUT], &pio1->rxf[sm_output], false);
  dma_channel_set_config(dma_channels[CHAN_INPUT], &dma_config[CHAN_INPUT], false);

  // Set DMA bus priority
  bus_ctrl_hw->priority = BUSCTRL_BUS_PRIORITY_DMA_W_BITS | BUSCTRL_BUS_PRIORITY_DMA_R_BITS;
}

static void start_dma_transfer(uint8_t channel, uint8_t *buffer, uint32_t count) {
  if (channel == CHAN_OUTPUT) {
    dma_channel_transfer_from_buffer_now(dma_channels[channel], buffer, count);
  } else if (channel == CHAN_INPUT) {
    dma_channel_transfer_to_buffer_now(dma_channels[channel], buffer, count);
  }
}

//-----------------------------------------------------------------------------
// Report Management Functions
//-----------------------------------------------------------------------------

// Called after report is sent to clear relative data (e.g., mouse delta)
static void report_done(uint8_t instance) {
  if (instance >= MAX_PLAYERS) return;

  if (current_reports[instance][0] == 0x49) {
    // Mouse report - clear relative displacement to avoid continuous movement
    current_reports[instance][1] &= 0xF0;  // Keep buttons, clear dy_up
    current_reports[instance][2] = 0x00;    // Clear dx_up and dy_low
    current_reports[instance][3] = 0x00;    // Clear dx_low
  }
}

// PIO interrupt handler - triggered when CLK is high for 32 consecutive cycles
//
// NOTE: Current implementation uses buffered passthrough relay with one-poll delay (~16ms).
// Passthrough data read during this poll is stored and sent on the NEXT poll.
//
// Future enhancement: Implement real-time passthrough relay (zero latency) to match
// SNES23DO AVR implementation. See Phase 4, item #2 in 3DO_INTEGRATION_PLAN.md.
// Reference: /Users/robert/git/SNES23DO/code/SNES23DO/main.asm (lines 520-768)
//
void on_pio0_irq(void) {
  update_report_flag = true;
  pio_irq_count++;  // Fast counter increment (safe, no timing impact)

  // NOTE: Do NOT printf here! It breaks timing and kills passthrough.
  // Counter is printed from _3do_task() instead.

  // Abort any ongoing DMA transfers
  dma_channel_abort(dma_channels[CHAN_OUTPUT]);
  dma_channel_abort(dma_channels[CHAN_INPUT]);

  // Drain PIO FIFOs
  pio_sm_drain_tx_fifo(pio1, sm_output);
  while (!pio_sm_is_rx_fifo_empty(pio1, sm_output)) {
    pio_sm_get(pio1, sm_output);
  }

  // Restart PIO state machine
  pio_sm_restart(pio1, sm_output);
  pio_sm_exec(pio1, sm_output, instr_jmp[sm_output]);

  // Copy all USB controller reports to DMA buffer
  int total_report_size = 0;
  for (int i = 0; i < max_usb_controller; i++) {
    memcpy(&controller_buffer[total_report_size], &current_reports[i][0], report_sizes[i]);
    report_done(i);
    total_report_size += report_sizes[i];
  }

  // NOTE: Do NOT printf here! It breaks timing.
  // total_report_size can be logged from _3do_task() instead.

  // Start DMA transfers
  // OUTPUT: Sends USB controllers + buffered passthrough from previous poll
  start_dma_transfer(CHAN_OUTPUT, &controller_buffer[0], 201);
  pio_sm_set_enabled(pio1, sm_output, true);
  // INPUT: Reads new passthrough data (will be sent on NEXT poll)
  start_dma_transfer(CHAN_INPUT, &controller_buffer[total_report_size], 201 - total_report_size);

  // Clear PIO interrupt
  pio_interrupt_clear(pio1, 0);
  irq_clear(PIO1_IRQ_0);
}

//-----------------------------------------------------------------------------
// Report Constructor Functions
//-----------------------------------------------------------------------------

_3do_joypad_report new_3do_joypad_report(void) {
  _3do_joypad_report report;
  // Initialize to 0 to ensure clean state for bitfield struct
  memset(&report, 0, sizeof(report));
  report.id = 0b100;   // Standard gamepad ID
  report.tail = 0b00;  // Tail bits always 0
  // All buttons default to 0 (not pressed)
  // Note: 3DO protocol is active-HIGH (1 = pressed, 0 = not pressed)
  report.A = 0; report.B = 0; report.C = 0; report.X = 0;
  report.L = 0; report.R = 0; report.P = 0;
  report.left = 0; report.right = 0; report.up = 0; report.down = 0;
  return report;
}

_3do_joystick_report new_3do_joystick_report(void) {
  _3do_joystick_report report;
  // Initialize to 0 to ensure clean state
  memset(&report, 0, sizeof(report));
  report.id_0 = 0x01;
  report.id_1 = 0x7B;
  report.id_2 = 0x08;
  report.tail = 0x00;
  // Initialize analog to center and buttons to not pressed
  // Note: 3DO protocol is active-HIGH (1 = pressed, 0 = not pressed)
  report.analog1 = 128; report.analog2 = 128;
  report.analog3 = 128; report.analog4 = 128;
  report.A = 0; report.B = 0; report.C = 0; report.X = 0;
  report.L = 0; report.R = 0; report.P = 0; report.FIRE = 0;
  report.left = 0; report.right = 0; report.up = 0; report.down = 0;
  return report;
}

_3do_mouse_report new_3do_mouse_report(void) {
  _3do_mouse_report report;
  // Initialize to 0 to ensure clean state
  memset(&report, 0, sizeof(report));
  report.id = 0x49;
  return report;
}

//-----------------------------------------------------------------------------
// Extension Controller Detection
//-----------------------------------------------------------------------------

// Parse extension controller data to count connected controllers
// Based on 3DO PBUS protocol: https://3dodev.com/documentation/hardware/opera/pbus
// Returns number of extension controllers detected
static uint8_t parse_extension_controllers(uint8_t* buffer, size_t buffer_size) {
  uint8_t count = 0;
  size_t offset = 0;

  while (offset < buffer_size) {
    uint8_t byte1 = buffer[offset];

    // Check for end-of-chain: "string of zeros"
    if (byte1 == 0x00) {
      // Verify it's actually end (multiple zeros)
      bool is_end = true;
      for (size_t i = offset; i < buffer_size && i < offset + 4; i++) {
        if (buffer[i] != 0x00) {
          is_end = false;
          break;
        }
      }
      if (is_end) break;
    }

    // Check first 4 bits for device ID
    uint8_t id_nibble = (byte1 >> 4) & 0x0F;

    // Joypad: ID starts with 01, 10, or 11 (first 2 bits != 00)
    if ((id_nibble & 0b1100) != 0) {
      // Control Pad: 8 bits (1 byte)
      count++;
      offset += 1;
    }
    // Flightstick: ID 0x01, 0x7B, length 0x08
    else if (byte1 == 0x01 && offset + 2 < buffer_size &&
             buffer[offset + 1] == 0x7B && buffer[offset + 2] == 0x08) {
      // Flightstick: 3 ID bytes + 4 analog + 2 buttons = 9 bytes total
      count++;
      offset += 9;
    }
    // Mouse: ID 0x49
    else if (byte1 == 0x49) {
      // Mouse: 24 bits = 3 bytes
      count++;
      offset += 3;
    }
    // Lightgun: ID 0x4D
    else if (byte1 == 0x4D) {
      // Lightgun: 32 bits = 4 bytes
      count++;
      offset += 4;
    }
    // Arcade Buttons: ID 0xC0
    else if (byte1 == 0xC0) {
      // Arcade: 16 bits = 2 bytes
      count++;
      offset += 2;
    }
    else {
      // Unknown device - skip 1 byte and continue
      offset += 1;
    }

    // Safety: PBUS supports max 56 devices, but we cap at MAX_PLAYERS
    if (count >= MAX_PLAYERS || offset >= buffer_size) {
      break;
    }
  }

  return count;
}

//-----------------------------------------------------------------------------
// Report Update Functions
//-----------------------------------------------------------------------------

void update_3do_joypad(_3do_joypad_report report, uint8_t instance) {
  if (instance >= MAX_PLAYERS) return;

  memcpy(&current_reports[instance][0], &report, sizeof(_3do_joypad_report));
  report_sizes[instance] = 2;
  device_attached[instance] = true;

  max_usb_controller = (max_usb_controller < (instance + 1)) ? (instance + 1) : max_usb_controller;
}

void update_3do_joystick(_3do_joystick_report report, uint8_t instance) {
  if (instance >= MAX_PLAYERS) return;

  memcpy(&current_reports[instance][0], &report, sizeof(_3do_joystick_report));
  report_sizes[instance] = 9;
  device_attached[instance] = true;

  max_usb_controller = (max_usb_controller < (instance + 1)) ? (instance + 1) : max_usb_controller;
}

void update_3do_mouse(_3do_mouse_report report, uint8_t instance) {
  if (instance >= MAX_PLAYERS) return;

  memcpy(&current_reports[instance][0], &report, sizeof(_3do_mouse_report));
  report_sizes[instance] = 4;
  device_attached[instance] = true;

  max_usb_controller = (max_usb_controller < (instance + 1)) ? (instance + 1) : max_usb_controller;
}

//-----------------------------------------------------------------------------
// Initialization
//-----------------------------------------------------------------------------

void _3do_init(void) {
  #if CFG_TUSB_DEBUG >= 1
  // Initialize UART for debugging
  uart_init(UART_ID, BAUD_RATE);
  gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
  printf("3DO protocol initializing...\n");
  #endif

  // Initialize report buffers with 0xFF (all buttons not pressed in active-low logic)
  memset(current_reports, 0xFF, sizeof(current_reports));
  memset(controller_buffer, 0xFF, sizeof(controller_buffer));

  // Use PIO1 to isolate 3DO protocol from ws2812 on PIO0
  pio = pio1;

  // Initialize CLK pin as input
  gpio_init(CLK_PIN);
  gpio_set_dir(CLK_PIN, GPIO_IN);

  // Set up PIO interrupt
  pio_set_irq0_source_enabled(pio1, pis_interrupt0, true);
  irq_set_exclusive_handler(PIO1_IRQ_0, on_pio0_irq);
  irq_set_enabled(PIO1_IRQ_0, true);

  // Load and initialize sampling program (CLK monitor)
  // Use dynamic claiming to avoid conflicts
  sm_sampling = pio_claim_unused_sm(pio1, true);
  uint offset_sampling = pio_add_program(pio1, &sampling_program);
  sampling_program_init(pio1, sm_sampling, offset_sampling);

  // Load and initialize output program (serial data output)
  sm_output = pio_claim_unused_sm(pio1, true);
  uint offset_output = pio_add_program(pio1, &output_program);
  output_program_init(pio1, sm_output, offset_output);

  instr_jmp[sm_output] = pio_encode_jmp(offset_output);

  // Setup DMA channels
  setup_3do_dma_output();
  setup_3do_dma_input();

  // Initialize GPIO pins for PIO
  pio_gpio_init(pio1, DATA_IN_PIN);
  gpio_pull_up(DATA_IN_PIN);
  pio_sm_set_consecutive_pindirs(pio1, sm_output, DATA_IN_PIN, 1, false);

  pio_gpio_init(pio1, DATA_OUT_PIN);
  pio_sm_set_consecutive_pindirs(pio1, sm_output, DATA_OUT_PIN, 1, true);

  // Load saved profile from flash (if valid)
  flash_t settings;
  if (flash_load(&settings)) {
    // Valid settings found - restore saved profile
    if (settings.active_profile_index < TDO_PROFILE_COUNT) {
      active_profile_index = settings.active_profile_index;
      active_profile = &profiles[active_profile_index];
      #if CFG_TUSB_DEBUG >= 1
      printf("Loaded profile from flash: %s\n", active_profile->name);
      #endif
    }
  }

  #if CFG_TUSB_DEBUG >= 1
  printf("3DO protocol initialized successfully.\n");
  printf("Active profile: %s (%s)\n", active_profile->name, active_profile->description);
  #endif

  // Note: Core1 is launched by main.c, not here
}

//-----------------------------------------------------------------------------
// Profile Switching
//-----------------------------------------------------------------------------

// Switch to a new profile
static void switch_to_profile(uint8_t new_index)
{
  if (new_index >= TDO_PROFILE_COUNT) return;

  active_profile_index = new_index;
  active_profile = &profiles[new_index];

  // Trigger NeoPixel LED feedback (OFF blinks = profile number + 1)
  neopixel_indicate_profile(new_index);

  // Trigger controller LED + rumble feedback
  feedback_trigger(new_index, router_get_player_count(OUTPUT_TARGET_3DO));

  // Save profile selection to flash (debounced - writes after 5 seconds)
  flash_t settings;
  settings.active_profile_index = active_profile_index;
  flash_save(&settings);

  printf("Profile switched to: %s (%s)\n", active_profile->name, active_profile->description);
}

// Check for profile switching: SELECT + D-pad Up/Down
static void check_profile_switch_combo(void)
{
  static uint32_t select_hold_start = 0;   // When Select was first pressed
  static bool select_was_held = false;
  static bool dpad_up_was_pressed = false;
  static bool dpad_down_was_pressed = false;
  static bool initial_trigger_done = false; // Has first 2-second trigger happened?
  const uint32_t INITIAL_HOLD_TIME_MS = 2000; // Must hold 2 seconds for first trigger

  // Get input from router for player 0 (controller 1)
  const input_event_t* event = router_get_output(OUTPUT_TARGET_3DO, 0);
  if (!event) return; // No controller connected

  uint32_t buttons = event->buttons;
  bool select_held = ((buttons & USBR_BUTTON_S1) == 0);
  bool dpad_up_pressed = ((buttons & USBR_BUTTON_DU) == 0);
  bool dpad_down_pressed = ((buttons & USBR_BUTTON_DD) == 0);

  // Select released - reset everything
  if (!select_held)
  {
    select_hold_start = 0;
    select_was_held = false;
    dpad_up_was_pressed = false;
    dpad_down_was_pressed = false;
    initial_trigger_done = false;
    return;
  }

  // Select is held
  if (!select_was_held)
  {
    // Select just pressed - start timer
    select_hold_start = to_ms_since_boot(get_absolute_time());
    select_was_held = true;
  }

  uint32_t current_time = to_ms_since_boot(get_absolute_time());
  uint32_t select_hold_duration = current_time - select_hold_start;

  // Check if initial 2-second hold period has elapsed
  bool can_trigger = initial_trigger_done || (select_hold_duration >= INITIAL_HOLD_TIME_MS);

  if (!can_trigger)
  {
    // Still waiting for initial 2-second hold - don't trigger yet
    return;
  }

  // Can trigger - check for D-pad edge detection (rising edge = just pressed)
  // But don't allow switching while feedback (NeoPixel LED, rumble, player LED) is still active
  if (neopixel_is_indicating() || feedback_is_active())
  {
    // Still showing feedback from previous switch - wait for it to finish
    return;
  }

  // D-pad Up - cycle forward on rising edge
  if (dpad_up_pressed && !dpad_up_was_pressed)
  {
    uint8_t new_index = (active_profile_index + 1) % TDO_PROFILE_COUNT;
    switch_to_profile(new_index);
    initial_trigger_done = true; // Mark that first trigger happened
  }
  dpad_up_was_pressed = dpad_up_pressed;

  // D-pad Down - cycle backward on rising edge
  if (dpad_down_pressed && !dpad_down_was_pressed)
  {
    uint8_t new_index = (active_profile_index == 0) ? (TDO_PROFILE_COUNT - 1) : (active_profile_index - 1);
    switch_to_profile(new_index);
    initial_trigger_done = true; // Mark that first trigger happened
  }
  dpad_down_was_pressed = dpad_down_pressed;
}

//-----------------------------------------------------------------------------
// Button Mapping Helpers
//-----------------------------------------------------------------------------

// Apply a joypad button mapping (returns 1 if pressed, 0 if not)
static inline uint8_t apply_joypad_button(tdo_button_output_t action, uint32_t buttons)
{
  bool is_pressed = false;

  switch (action)
  {
    case TDO_BTN_A:
    case TDO_BTN_B:
    case TDO_BTN_C:
    case TDO_BTN_X:
    case TDO_BTN_L:
    case TDO_BTN_R:
    case TDO_BTN_P:
      // Will be handled by caller
      break;
    case TDO_BTN_FIRE:
    case TDO_BTN_NONE:
    default:
      break;
  }

  return is_pressed ? 1 : 0;
}

// Apply profile button mappings to joypad report
static inline void apply_joypad_profile(_3do_joypad_report* report, uint32_t buttons)
{
  // Check each USBRetro button and apply profile mapping
  // USBRetro active-low: button pressed = bit clear (0)
  // 3DO output active-HIGH: button pressed = bit set (1)

  // B1
  bool b1_pressed = (buttons & USBR_BUTTON_B1) == 0;
  if (b1_pressed) {
    switch (active_profile->joypad.b1_button) {
      case TDO_BTN_A: report->A = 1; break;
      case TDO_BTN_B: report->B = 1; break;
      case TDO_BTN_C: report->C = 1; break;
      case TDO_BTN_X: report->X = 1; break;
      case TDO_BTN_L: report->L = 1; break;
      case TDO_BTN_R: report->R = 1; break;
      case TDO_BTN_P: report->P = 1; break;
      default: break;
    }
  }

  // B2
  bool b2_pressed = (buttons & USBR_BUTTON_B2) == 0;
  if (b2_pressed) {
    switch (active_profile->joypad.b2_button) {
      case TDO_BTN_A: report->A = 1; break;
      case TDO_BTN_B: report->B = 1; break;
      case TDO_BTN_C: report->C = 1; break;
      case TDO_BTN_X: report->X = 1; break;
      case TDO_BTN_L: report->L = 1; break;
      case TDO_BTN_R: report->R = 1; break;
      case TDO_BTN_P: report->P = 1; break;
      default: break;
    }
  }

  // B3
  bool b3_pressed = (buttons & USBR_BUTTON_B3) == 0;
  if (b3_pressed) {
    switch (active_profile->joypad.b3_button) {
      case TDO_BTN_A: report->A = 1; break;
      case TDO_BTN_B: report->B = 1; break;
      case TDO_BTN_C: report->C = 1; break;
      case TDO_BTN_X: report->X = 1; break;
      case TDO_BTN_L: report->L = 1; break;
      case TDO_BTN_R: report->R = 1; break;
      case TDO_BTN_P: report->P = 1; break;
      default: break;
    }
  }

  // B4
  bool b4_pressed = (buttons & USBR_BUTTON_B4) == 0;
  if (b4_pressed) {
    switch (active_profile->joypad.b4_button) {
      case TDO_BTN_A: report->A = 1; break;
      case TDO_BTN_B: report->B = 1; break;
      case TDO_BTN_C: report->C = 1; break;
      case TDO_BTN_X: report->X = 1; break;
      case TDO_BTN_L: report->L = 1; break;
      case TDO_BTN_R: report->R = 1; break;
      case TDO_BTN_P: report->P = 1; break;
      default: break;
    }
  }

  // L1
  bool l1_pressed = (buttons & USBR_BUTTON_L1) == 0;
  if (l1_pressed) {
    switch (active_profile->joypad.l1_button) {
      case TDO_BTN_A: report->A = 1; break;
      case TDO_BTN_B: report->B = 1; break;
      case TDO_BTN_C: report->C = 1; break;
      case TDO_BTN_X: report->X = 1; break;
      case TDO_BTN_L: report->L = 1; break;
      case TDO_BTN_R: report->R = 1; break;
      case TDO_BTN_P: report->P = 1; break;
      default: break;
    }
  }

  // L2
  bool l2_pressed = (buttons & USBR_BUTTON_L2) == 0;
  if (l2_pressed) {
    switch (active_profile->joypad.l2_button) {
      case TDO_BTN_A: report->A = 1; break;
      case TDO_BTN_B: report->B = 1; break;
      case TDO_BTN_C: report->C = 1; break;
      case TDO_BTN_X: report->X = 1; break;
      case TDO_BTN_L: report->L = 1; break;
      case TDO_BTN_R: report->R = 1; break;
      case TDO_BTN_P: report->P = 1; break;
      default: break;
    }
  }

  // R1
  bool r1_pressed = (buttons & USBR_BUTTON_R1) == 0;
  if (r1_pressed) {
    switch (active_profile->joypad.r1_button) {
      case TDO_BTN_A: report->A = 1; break;
      case TDO_BTN_B: report->B = 1; break;
      case TDO_BTN_C: report->C = 1; break;
      case TDO_BTN_X: report->X = 1; break;
      case TDO_BTN_L: report->L = 1; break;
      case TDO_BTN_R: report->R = 1; break;
      case TDO_BTN_P: report->P = 1; break;
      default: break;
    }
  }

  // R2
  bool r2_pressed = (buttons & USBR_BUTTON_R2) == 0;
  if (r2_pressed) {
    switch (active_profile->joypad.r2_button) {
      case TDO_BTN_A: report->A = 1; break;
      case TDO_BTN_B: report->B = 1; break;
      case TDO_BTN_C: report->C = 1; break;
      case TDO_BTN_X: report->X = 1; break;
      case TDO_BTN_L: report->L = 1; break;
      case TDO_BTN_R: report->R = 1; break;
      case TDO_BTN_P: report->P = 1; break;
      default: break;
    }
  }

  // S1
  bool s1_pressed = (buttons & USBR_BUTTON_S1) == 0;
  if (s1_pressed) {
    switch (active_profile->joypad.s1_button) {
      case TDO_BTN_A: report->A = 1; break;
      case TDO_BTN_B: report->B = 1; break;
      case TDO_BTN_C: report->C = 1; break;
      case TDO_BTN_X: report->X = 1; break;
      case TDO_BTN_L: report->L = 1; break;
      case TDO_BTN_R: report->R = 1; break;
      case TDO_BTN_P: report->P = 1; break;
      default: break;
    }
  }

  // S2
  bool s2_pressed = (buttons & USBR_BUTTON_S2) == 0;
  if (s2_pressed) {
    switch (active_profile->joypad.s2_button) {
      case TDO_BTN_A: report->A = 1; break;
      case TDO_BTN_B: report->B = 1; break;
      case TDO_BTN_C: report->C = 1; break;
      case TDO_BTN_X: report->X = 1; break;
      case TDO_BTN_L: report->L = 1; break;
      case TDO_BTN_R: report->R = 1; break;
      case TDO_BTN_P: report->P = 1; break;
      default: break;
    }
  }
}

// Apply profile button mappings to joystick report
static inline void apply_joystick_profile(_3do_joystick_report* report, uint32_t buttons)
{
  // Similar to joypad but includes FIRE button

  // B1
  bool b1_pressed = (buttons & USBR_BUTTON_B1) == 0;
  if (b1_pressed) {
    switch (active_profile->joystick.b1_button) {
      case TDO_BTN_A: report->A = 1; break;
      case TDO_BTN_B: report->B = 1; break;
      case TDO_BTN_C: report->C = 1; break;
      case TDO_BTN_X: report->X = 1; break;
      case TDO_BTN_L: report->L = 1; break;
      case TDO_BTN_R: report->R = 1; break;
      case TDO_BTN_P: report->P = 1; break;
      case TDO_BTN_FIRE: report->FIRE = 1; break;
      default: break;
    }
  }

  // B2
  bool b2_pressed = (buttons & USBR_BUTTON_B2) == 0;
  if (b2_pressed) {
    switch (active_profile->joystick.b2_button) {
      case TDO_BTN_A: report->A = 1; break;
      case TDO_BTN_B: report->B = 1; break;
      case TDO_BTN_C: report->C = 1; break;
      case TDO_BTN_X: report->X = 1; break;
      case TDO_BTN_L: report->L = 1; break;
      case TDO_BTN_R: report->R = 1; break;
      case TDO_BTN_P: report->P = 1; break;
      case TDO_BTN_FIRE: report->FIRE = 1; break;
      default: break;
    }
  }

  // B3
  bool b3_pressed = (buttons & USBR_BUTTON_B3) == 0;
  if (b3_pressed) {
    switch (active_profile->joystick.b3_button) {
      case TDO_BTN_A: report->A = 1; break;
      case TDO_BTN_B: report->B = 1; break;
      case TDO_BTN_C: report->C = 1; break;
      case TDO_BTN_X: report->X = 1; break;
      case TDO_BTN_L: report->L = 1; break;
      case TDO_BTN_R: report->R = 1; break;
      case TDO_BTN_P: report->P = 1; break;
      case TDO_BTN_FIRE: report->FIRE = 1; break;
      default: break;
    }
  }

  // B4
  bool b4_pressed = (buttons & USBR_BUTTON_B4) == 0;
  if (b4_pressed) {
    switch (active_profile->joystick.b4_button) {
      case TDO_BTN_A: report->A = 1; break;
      case TDO_BTN_B: report->B = 1; break;
      case TDO_BTN_C: report->C = 1; break;
      case TDO_BTN_X: report->X = 1; break;
      case TDO_BTN_L: report->L = 1; break;
      case TDO_BTN_R: report->R = 1; break;
      case TDO_BTN_P: report->P = 1; break;
      case TDO_BTN_FIRE: report->FIRE = 1; break;
      default: break;
    }
  }

  // L1
  bool l1_pressed = (buttons & USBR_BUTTON_L1) == 0;
  if (l1_pressed) {
    switch (active_profile->joystick.l1_button) {
      case TDO_BTN_A: report->A = 1; break;
      case TDO_BTN_B: report->B = 1; break;
      case TDO_BTN_C: report->C = 1; break;
      case TDO_BTN_X: report->X = 1; break;
      case TDO_BTN_L: report->L = 1; break;
      case TDO_BTN_R: report->R = 1; break;
      case TDO_BTN_P: report->P = 1; break;
      case TDO_BTN_FIRE: report->FIRE = 1; break;
      default: break;
    }
  }

  // L2
  bool l2_pressed = (buttons & USBR_BUTTON_L2) == 0;
  if (l2_pressed) {
    switch (active_profile->joystick.l2_button) {
      case TDO_BTN_A: report->A = 1; break;
      case TDO_BTN_B: report->B = 1; break;
      case TDO_BTN_C: report->C = 1; break;
      case TDO_BTN_X: report->X = 1; break;
      case TDO_BTN_L: report->L = 1; break;
      case TDO_BTN_R: report->R = 1; break;
      case TDO_BTN_P: report->P = 1; break;
      case TDO_BTN_FIRE: report->FIRE = 1; break;
      default: break;
    }
  }

  // R1
  bool r1_pressed = (buttons & USBR_BUTTON_R1) == 0;
  if (r1_pressed) {
    switch (active_profile->joystick.r1_button) {
      case TDO_BTN_A: report->A = 1; break;
      case TDO_BTN_B: report->B = 1; break;
      case TDO_BTN_C: report->C = 1; break;
      case TDO_BTN_X: report->X = 1; break;
      case TDO_BTN_L: report->L = 1; break;
      case TDO_BTN_R: report->R = 1; break;
      case TDO_BTN_P: report->P = 1; break;
      case TDO_BTN_FIRE: report->FIRE = 1; break;
      default: break;
    }
  }

  // R2
  bool r2_pressed = (buttons & USBR_BUTTON_R2) == 0;
  if (r2_pressed) {
    switch (active_profile->joystick.r2_button) {
      case TDO_BTN_A: report->A = 1; break;
      case TDO_BTN_B: report->B = 1; break;
      case TDO_BTN_C: report->C = 1; break;
      case TDO_BTN_X: report->X = 1; break;
      case TDO_BTN_L: report->L = 1; break;
      case TDO_BTN_R: report->R = 1; break;
      case TDO_BTN_P: report->P = 1; break;
      case TDO_BTN_FIRE: report->FIRE = 1; break;
      default: break;
    }
  }

  // S1
  bool s1_pressed = (buttons & USBR_BUTTON_S1) == 0;
  if (s1_pressed) {
    switch (active_profile->joystick.s1_button) {
      case TDO_BTN_A: report->A = 1; break;
      case TDO_BTN_B: report->B = 1; break;
      case TDO_BTN_C: report->C = 1; break;
      case TDO_BTN_X: report->X = 1; break;
      case TDO_BTN_L: report->L = 1; break;
      case TDO_BTN_R: report->R = 1; break;
      case TDO_BTN_P: report->P = 1; break;
      case TDO_BTN_FIRE: report->FIRE = 1; break;
      default: break;
    }
  }

  // S2
  bool s2_pressed = (buttons & USBR_BUTTON_S2) == 0;
  if (s2_pressed) {
    switch (active_profile->joystick.s2_button) {
      case TDO_BTN_A: report->A = 1; break;
      case TDO_BTN_B: report->B = 1; break;
      case TDO_BTN_C: report->C = 1; break;
      case TDO_BTN_X: report->X = 1; break;
      case TDO_BTN_L: report->L = 1; break;
      case TDO_BTN_R: report->R = 1; break;
      case TDO_BTN_P: report->P = 1; break;
      case TDO_BTN_FIRE: report->FIRE = 1; break;
      default: break;
    }
  }
}

//-----------------------------------------------------------------------------
// Task Processing
//-----------------------------------------------------------------------------

// task process for 3DO (called from main loop)
void _3do_task() {
  // Periodic debug logging (safe to printf here, not in IRQ)
  #if CFG_TUSB_DEBUG >= 1
  static uint32_t last_log_time = 0;
  static uint32_t last_irq_count = 0;
  uint32_t now = to_ms_since_boot(get_absolute_time());

  if (now - last_log_time > 5000) {  // Log every 5 seconds
    uint32_t irq_delta = pio_irq_count - last_irq_count;
    printf("[3DO] IRQs: %lu (+%lu/5s), USB=%d, EXT=%d, attached=[",
           pio_irq_count, irq_delta, max_usb_controller, extension_controller_count);
    for (int i = 0; i < MAX_PLAYERS; i++) {
      printf("%d", device_attached[i] ? 1 : 0);
    }
    printf("], sizes=[");
    for (int i = 0; i < MAX_PLAYERS; i++) {
      printf("%d,", report_sizes[i]);
    }
    printf("]\n");

    last_log_time = now;
    last_irq_count = pio_irq_count;
  }
  #endif

  // Parse extension controller data to detect connected controllers
  // Extension data starts after USB controller reports in buffer
  static uint8_t last_usb_count = 0;
  if (max_usb_controller != last_usb_count) {
    last_usb_count = max_usb_controller;
  }

  // Calculate total size of USB reports sent
  int total_usb_size = 0;
  for (int i = 0; i < max_usb_controller; i++) {
    total_usb_size += report_sizes[i];
  }

  // Parse extension data (comes after USB reports in buffer)
  if (total_usb_size < 201) {
    extension_controller_count = parse_extension_controllers(
      &controller_buffer[total_usb_size],
      201 - total_usb_size
    );
  }

  // Update all player reports from router
  // This replaces the old post_globals() call chain
  for (int i = 0; i < MAX_PLAYERS; i++) {
    update_3do_report(i);
  }

  // Check for profile switching combo
  check_profile_switch_combo();
}

//-----------------------------------------------------------------------------
// Core1 Entry Point
//-----------------------------------------------------------------------------

void __not_in_flash_func(core1_entry)(void) {
  // Core1 can be used for periodic tasks if needed
  // For 3DO, most work is done in the interrupt handler
  while (1) {
    sleep_ms(100);
  }
}

//-----------------------------------------------------------------------------
// USB Input Integration - post_globals()
//-----------------------------------------------------------------------------

void __not_in_flash_func(update_3do_report)(uint8_t player_index) {
  if (player_index >= MAX_PLAYERS) return;

  // Get input from router (3DO supports up to 8 players)
  const input_event_t* event = router_get_output(OUTPUT_TARGET_3DO, player_index);
  if (!event) return;  // No input for this player slot

  uint32_t buttons = event->buttons;
  uint8_t ax = event->analog[ANALOG_X];   // Left stick X
  uint8_t ay = event->analog[ANALOG_Y];   // Left stick Y
  uint8_t az = event->analog[ANALOG_Z];   // Right stick X
  uint8_t at = event->analog[ANALOG_RX];  // Right stick Y

  // DEBUG: Log button state when S1 or B4 is pressed
  static uint32_t debug_counter = 0;
  static uint32_t last_buttons = 0;

  // Only log when button state changes to reduce spam
  if (buttons != last_buttons && ((buttons & USBR_BUTTON_S1) || (buttons & USBR_BUTTON_B4))) {
    uint32_t s1_val = (buttons & USBR_BUTTON_S1);
    uint32_t b4_val = (buttons & USBR_BUTTON_B4);
    bool or_result = (s1_val || b4_val);

    printf("[DEBUG] buttons=0x%08X | S1=0x%08X(%d) | B4=0x%08X(%d) | OR=%d\n",
      buttons,
      s1_val, s1_val ? 1 : 0,
      b4_val, b4_val ? 1 : 0,
      or_result ? 1 : 0
    );
    last_buttons = buttons;
  } else if (!(buttons & (USBR_BUTTON_S1 | USBR_BUTTON_B4))) {
    last_buttons = buttons;  // Reset tracking when both buttons released
  }

  // Determine if this is a flight stick (has significant analog input)
  // TODO: Add better heuristics or device-type detection
  bool is_joystick = false;

  if (is_joystick) {
    // Send joystick report
    _3do_joystick_report report = new_3do_joystick_report();

    // Map analog axes
    report.analog1 = ax;
    report.analog2 = ay;
    report.analog3 = az;
    report.analog4 = at;

    // Apply profile-based button mappings for joystick mode
    apply_joystick_profile(&report, buttons);

    // Map D-pad directly (not through profile system)
    // USB active-low: 0 = pressed, 3DO active-HIGH: 1 = pressed
    report.left = (buttons & USBR_BUTTON_DL) == 0 ? 1 : 0;
    report.right = (buttons & USBR_BUTTON_DR) == 0 ? 1 : 0;
    report.up = (buttons & USBR_BUTTON_DU) == 0 ? 1 : 0;
    report.down = (buttons & USBR_BUTTON_DD) == 0 ? 1 : 0;

    // If no digital D-pad pressed, use left analog stick
    if (report.left == 0 && report.right == 0 && report.up == 0 && report.down == 0) {
      report.left = (ax < 64) ? 1 : 0;
      report.right = (ax > 192) ? 1 : 0;
      report.up = (ay > 192) ? 1 : 0;      // Inverted Y axis
      report.down = (ay < 64) ? 1 : 0;     // Inverted Y axis
    }

    update_3do_joystick(report, player_index);
  } else {
    // Send joypad report
    _3do_joypad_report report = new_3do_joypad_report();

    // Apply profile-based button mappings for joypad mode
    apply_joypad_profile(&report, buttons);

    // Map D-pad directly (not through profile system)
    // USB active-low: 0 = pressed, 3DO active-HIGH: 1 = pressed
    report.left = (buttons & USBR_BUTTON_DL) == 0 ? 1 : 0;
    report.right = (buttons & USBR_BUTTON_DR) == 0 ? 1 : 0;
    report.up = (buttons & USBR_BUTTON_DU) == 0 ? 1 : 0;
    report.down = (buttons & USBR_BUTTON_DD) == 0 ? 1 : 0;

    // If no digital D-pad pressed, use left analog stick
    if (report.left == 0 && report.right == 0 && report.up == 0 && report.down == 0) {
      report.left = (ax < 64) ? 1 : 0;
      report.right = (ax > 192) ? 1 : 0;
      report.up = (ay > 192) ? 1 : 0;      // Inverted Y axis
      report.down = (ay < 64) ? 1 : 0;     // Inverted Y axis
    }

    update_3do_joypad(report, player_index);
  }
}

// post_globals() and post_mouse_globals() removed - replaced by router architecture
// Input flow: USB drivers → router_submit_input() → router → router_get_output() → update_3do_report()
// Mouse support: TODO - implement mouse event handling via router (Phase 5)

// ============================================================================
// OUTPUT INTERFACE
// ============================================================================

#include "core/output_interface.h"

const OutputInterface threedooutput_interface = {
    .name = "3DO",
    .init = _3do_init,
    .handle_input = NULL,  // Router architecture - inputs come via router_get_output()
    .core1_entry = core1_entry,
    .task = _3do_task,  // 3DO needs periodic polling and extension controller detection
};
