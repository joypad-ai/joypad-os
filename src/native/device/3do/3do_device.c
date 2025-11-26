// 3do_device.c - 3DO console output implementation for USBRetro
// Based on FCare/USBTo3DO protocol implementation
// Adapted to USBRetro architecture

#include "3do_device.h"
#include "3do_buttons.h"
#include "core/services/storage/flash.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/services/profile/profile.h"
#include "core/services/players/feedback.h"
#include "core/services/leds/ws2812.h"
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
// PROFILE SYSTEM (Delegates to core profile service)
// ============================================================================

// Player count callback for profile feedback
static uint8_t tdo_get_player_count_for_profile(void) {
    return router_get_player_count(OUTPUT_TARGET_3DO);
}

// Profile system accessors for OutputInterface
static uint8_t tdo_get_profile_count(void) {
    return profile_get_count(OUTPUT_TARGET_3DO);
}

static uint8_t tdo_get_active_profile(void) {
    return profile_get_active_index(OUTPUT_TARGET_3DO);
}

static void tdo_set_active_profile(uint8_t index) {
    profile_set_active(OUTPUT_TARGET_3DO, index);
}

static const char* tdo_get_profile_name(uint8_t index) {
    return profile_get_name(OUTPUT_TARGET_3DO, index);
}

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

// Output mode (normal vs silly pad for arcade)
static tdo_output_mode_t output_mode = TDO_MODE_NORMAL;

// Extension mode (passthrough vs managed)
static tdo_extension_mode_t extension_mode = TDO_EXT_PASSTHROUGH;

// Previous button state for extension controllers (for change detection)
static uint32_t ext_prev_buttons[MAX_PLAYERS] = {0};

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

_3do_silly_report new_3do_silly_report(void) {
  _3do_silly_report report;
  // Initialize to 0 to ensure clean state
  memset(&report, 0, sizeof(report));
  report.id = 0xC0;  // Silly control pad ID
  return report;
}

//-----------------------------------------------------------------------------
// Mode Management
//-----------------------------------------------------------------------------

tdo_output_mode_t tdo_get_output_mode(void) {
  return output_mode;
}

void tdo_set_output_mode(tdo_output_mode_t mode) {
  output_mode = mode;
  #if CFG_TUSB_DEBUG >= 1
  printf("[3DO] Output mode set to: %s\n", mode == TDO_MODE_SILLY ? "SILLY" : "NORMAL");
  #endif
}

void tdo_toggle_output_mode(void) {
  if (output_mode == TDO_MODE_NORMAL) {
    tdo_set_output_mode(TDO_MODE_SILLY);
  } else {
    tdo_set_output_mode(TDO_MODE_NORMAL);
  }
}

// Callback for D-pad Left/Right output mode switching
// direction: -1 = left (previous), +1 = right (next)
// Returns true if mode changed
static bool tdo_output_mode_switch_callback(int8_t direction) {
  (void)direction;  // Only 2 modes, direction doesn't matter
  tdo_toggle_output_mode();

  // Trigger feedback (mode 1 = silly, mode 0 = normal)
  uint8_t mode_index = (output_mode == TDO_MODE_SILLY) ? 1 : 0;
  uint8_t player_count = router_get_player_count(OUTPUT_TARGET_3DO);
  feedback_trigger(mode_index, player_count);
  neopixel_indicate_profile(mode_index);

  return true;
}

//-----------------------------------------------------------------------------
// Extension Mode Management
//-----------------------------------------------------------------------------

tdo_extension_mode_t tdo_get_extension_mode(void) {
  return extension_mode;
}

void tdo_set_extension_mode(tdo_extension_mode_t mode) {
  extension_mode = mode;
  #if CFG_TUSB_DEBUG >= 1
  printf("[3DO] Extension mode set to: %s\n",
         mode == TDO_EXT_MANAGED ? "MANAGED" : "PASSTHROUGH");
  #endif
}

void tdo_toggle_extension_mode(void) {
  if (extension_mode == TDO_EXT_PASSTHROUGH) {
    tdo_set_extension_mode(TDO_EXT_MANAGED);
  } else {
    tdo_set_extension_mode(TDO_EXT_PASSTHROUGH);
  }
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

// Parse extension controller data and submit to router (managed mode)
// Uses dev_addr 0xE0+ range for 3DO extension controllers
// Returns number of controllers processed
static uint8_t parse_extension_to_router(uint8_t* buffer, size_t buffer_size) {
  uint8_t count = 0;
  size_t offset = 0;

  while (offset < buffer_size && count < MAX_PLAYERS) {
    uint8_t byte0 = buffer[offset];

    // Check for end-of-chain
    if (byte0 == 0x00) {
      bool is_end = true;
      for (size_t i = offset; i < buffer_size && i < offset + 4; i++) {
        if (buffer[i] != 0x00) {
          is_end = false;
          break;
        }
      }
      if (is_end) break;
    }

    input_event_t event;
    init_input_event(&event);
    event.dev_addr = 0xE0 + count;  // 3DO extension range
    event.instance = 0;

    uint8_t id_nibble = (byte0 >> 4) & 0x0F;

    // Joypad: ID starts with 01, 10, or 11 (upper 2 bits of nibble != 00)
    if ((id_nibble & 0b1100) != 0) {
      if (offset + 2 > buffer_size) break;

      event.type = INPUT_TYPE_GAMEPAD;
      uint32_t buttons = 0xFFFFFFFF;  // Active-low

      // Byte 0: [A][Left][Right][Up][Down][ID2][ID1][ID0]
      // Note: 3DO is active-HIGH, we convert to active-LOW
      if (byte0 & 0x80) buttons &= ~USBR_BUTTON_B3;  // A → B3
      if (byte0 & 0x40) buttons &= ~USBR_BUTTON_DL;  // Left
      if (byte0 & 0x20) buttons &= ~USBR_BUTTON_DR;  // Right
      if (byte0 & 0x10) buttons &= ~USBR_BUTTON_DU;  // Up
      if (byte0 & 0x08) buttons &= ~USBR_BUTTON_DD;  // Down

      // Byte 1: [Tail1][Tail0][L][R][X][P][C][B]
      uint8_t byte1 = buffer[offset + 1];
      if (byte1 & 0x20) buttons &= ~USBR_BUTTON_L1;  // L
      if (byte1 & 0x10) buttons &= ~USBR_BUTTON_R1;  // R
      if (byte1 & 0x08) buttons &= ~USBR_BUTTON_S1;  // X → Select
      if (byte1 & 0x04) buttons &= ~USBR_BUTTON_S2;  // P → Start
      if (byte1 & 0x02) buttons &= ~USBR_BUTTON_B2;  // C → B2
      if (byte1 & 0x01) buttons &= ~USBR_BUTTON_B1;  // B → B1

      event.buttons = buttons;
      offset += 2;

      // Only submit if changed
      if (buttons != ext_prev_buttons[count]) {
        ext_prev_buttons[count] = buttons;
        router_submit_input(&event);
      }
      count++;
    }
    // Joystick: 3-byte ID header (0x01, 0x7B, 0x08)
    else if (byte0 == 0x01 && offset + 2 < buffer_size &&
             buffer[offset + 1] == 0x7B && buffer[offset + 2] == 0x08) {
      if (offset + 9 > buffer_size) break;

      event.type = INPUT_TYPE_FLIGHTSTICK;
      uint32_t buttons = 0xFFFFFFFF;

      // Analog axes (bytes 3-6)
      event.analog[ANALOG_X] = buffer[offset + 3];
      event.analog[ANALOG_Y] = buffer[offset + 4];
      event.analog[ANALOG_Z] = buffer[offset + 5];
      event.analog[ANALOG_RX] = buffer[offset + 6];

      // Byte 7: [Left][Right][Down][Up][C][B][A][FIRE]
      uint8_t byte7 = buffer[offset + 7];
      if (byte7 & 0x80) buttons &= ~USBR_BUTTON_DL;
      if (byte7 & 0x40) buttons &= ~USBR_BUTTON_DR;
      if (byte7 & 0x20) buttons &= ~USBR_BUTTON_DD;
      if (byte7 & 0x10) buttons &= ~USBR_BUTTON_DU;
      if (byte7 & 0x08) buttons &= ~USBR_BUTTON_B2;  // C
      if (byte7 & 0x04) buttons &= ~USBR_BUTTON_B1;  // B
      if (byte7 & 0x02) buttons &= ~USBR_BUTTON_B3;  // A
      if (byte7 & 0x01) buttons &= ~USBR_BUTTON_L2;  // FIRE → L2

      // Byte 8: [Tail:4][R][L][X][P]
      uint8_t byte8 = buffer[offset + 8];
      if (byte8 & 0x08) buttons &= ~USBR_BUTTON_R1;
      if (byte8 & 0x04) buttons &= ~USBR_BUTTON_L1;
      if (byte8 & 0x02) buttons &= ~USBR_BUTTON_S1;  // X
      if (byte8 & 0x01) buttons &= ~USBR_BUTTON_S2;  // P

      event.buttons = buttons;
      offset += 9;

      // Always submit joystick (analog changes)
      router_submit_input(&event);
      count++;
    }
    // Mouse: ID 0x49
    else if (byte0 == 0x49) {
      if (offset + 4 > buffer_size) break;

      event.type = INPUT_TYPE_MOUSE;
      uint32_t buttons = 0xFFFFFFFF;

      uint8_t byte1 = buffer[offset + 1];
      uint8_t byte2 = buffer[offset + 2];
      uint8_t byte3 = buffer[offset + 3];

      // Buttons
      if (byte1 & 0x01) buttons &= ~USBR_BUTTON_B1;  // Left
      if (byte1 & 0x02) buttons &= ~USBR_BUTTON_B3;  // Middle
      if (byte1 & 0x04) buttons &= ~USBR_BUTTON_B2;  // Right

      // Delta Y (10-bit signed)
      int16_t dy = ((byte1 >> 4) & 0x0F) << 6 | (byte2 & 0x3F);
      if (dy & 0x200) dy |= 0xFC00;
      event.delta_y = (int8_t)(dy > 127 ? 127 : (dy < -128 ? -128 : dy));

      // Delta X (10-bit signed)
      int16_t dx = ((byte2 >> 6) & 0x03) << 8 | byte3;
      if (dx & 0x200) dx |= 0xFC00;
      event.delta_x = (int8_t)(dx > 127 ? 127 : (dx < -128 ? -128 : dx));

      event.buttons = buttons;
      offset += 4;

      // Always submit mouse (relative motion)
      router_submit_input(&event);
      count++;
    }
    // Lightgun: ID 0x4D (skip for now)
    else if (byte0 == 0x4D) {
      offset += 4;
      count++;
    }
    // Arcade: ID 0xC0 (skip for now)
    else if (byte0 == 0xC0) {
      offset += 2;
      count++;
    }
    // Unknown - skip 1 byte
    else {
      offset++;
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

void update_3do_silly(_3do_silly_report report, uint8_t instance) {
  if (instance >= MAX_PLAYERS) return;

  memcpy(&current_reports[instance][0], &report, sizeof(_3do_silly_report));
  report_sizes[instance] = 2;  // Silly pad is 2 bytes
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

  // Profile system is initialized by app_init() - we just set up the callbacks
  profile_set_player_count_callback(tdo_get_player_count_for_profile);
  profile_set_output_mode_callback(tdo_output_mode_switch_callback);

  #if CFG_TUSB_DEBUG >= 1
  printf("3DO protocol initialized successfully.\n");
  const profile_t* active = profile_get_active(OUTPUT_TARGET_3DO);
  if (active) {
    printf("Active profile: %s (%s)\n", active->name, active->description);
  }
  #endif

  // Note: Core1 is launched by main.c, not here
}

//-----------------------------------------------------------------------------
// Button Mapping Helpers
//-----------------------------------------------------------------------------

// Map USBR buttons (after profile remapping) to 3DO joypad report
// Uses 3DO button aliases: TDO_BUTTON_A = USBR_BUTTON_B3, etc.
static inline void map_usbr_to_3do_joypad(_3do_joypad_report* report, uint32_t buttons)
{
    // USBR is active-low (0 = pressed), 3DO is active-HIGH (1 = pressed)
    // 3DO button aliases map to USBR positions:
    //   TDO_BUTTON_A = USBR_BUTTON_B3 (top)
    //   TDO_BUTTON_B = USBR_BUTTON_B1 (middle)
    //   TDO_BUTTON_C = USBR_BUTTON_B2 (bottom)
    //   TDO_BUTTON_L = USBR_BUTTON_L1
    //   TDO_BUTTON_R = USBR_BUTTON_R1
    //   TDO_BUTTON_X = USBR_BUTTON_S1
    //   TDO_BUTTON_P = USBR_BUTTON_S2

    report->A = (buttons & TDO_BUTTON_A) == 0 ? 1 : 0;
    report->B = (buttons & TDO_BUTTON_B) == 0 ? 1 : 0;
    report->C = (buttons & TDO_BUTTON_C) == 0 ? 1 : 0;
    report->L = (buttons & TDO_BUTTON_L) == 0 ? 1 : 0;
    report->R = (buttons & TDO_BUTTON_R) == 0 ? 1 : 0;
    report->X = (buttons & TDO_BUTTON_X) == 0 ? 1 : 0;
    report->P = (buttons & TDO_BUTTON_P) == 0 ? 1 : 0;
}

// Map USBR buttons to 3DO joystick report (includes FIRE button)
static inline void map_usbr_to_3do_joystick(_3do_joystick_report* report, uint32_t buttons)
{
    // Same as joypad, plus FIRE on L2
    report->A = (buttons & TDO_BUTTON_A) == 0 ? 1 : 0;
    report->B = (buttons & TDO_BUTTON_B) == 0 ? 1 : 0;
    report->C = (buttons & TDO_BUTTON_C) == 0 ? 1 : 0;
    report->L = (buttons & TDO_BUTTON_L) == 0 ? 1 : 0;
    report->R = (buttons & TDO_BUTTON_R) == 0 ? 1 : 0;
    report->X = (buttons & TDO_BUTTON_X) == 0 ? 1 : 0;
    report->P = (buttons & TDO_BUTTON_P) == 0 ? 1 : 0;

    // FIRE button mapped to L2 for joystick mode
    report->FIRE = (buttons & USBR_BUTTON_L2) == 0 ? 1 : 0;
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
    if (extension_mode == TDO_EXT_MANAGED) {
      // Managed mode: parse extension controllers and submit to router
      // They'll be assigned player slots like any other input device
      extension_controller_count = parse_extension_to_router(
        &controller_buffer[total_usb_size],
        201 - total_usb_size
      );
    } else {
      // Passthrough mode: just count extension controllers for debug
      // Data is relayed unchanged by DMA
      extension_controller_count = parse_extension_controllers(
        &controller_buffer[total_usb_size],
        201 - total_usb_size
      );
    }
  }

  // Update all player reports from router
  // This replaces the old post_globals() call chain
  for (int i = 0; i < MAX_PLAYERS; i++) {
    update_3do_report(i);
  }

  // Check for profile/mode switching combo (delegated to core)
  const input_event_t* event = router_get_output(OUTPUT_TARGET_3DO, 0);
  if (event) {
    profile_check_switch_combo(event->buttons);
  }
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

  // Skip slots without an actual controller attached
  // INPUT_TYPE_NONE means no USB device connected to this slot
  if (event->type == INPUT_TYPE_NONE) return;

  uint32_t buttons = event->buttons;
  uint8_t ax = event->analog[ANALOG_X];   // Left stick X
  uint8_t ay = event->analog[ANALOG_Y];   // Left stick Y
  uint8_t az = event->analog[ANALOG_Z];   // Right stick X
  uint8_t at = event->analog[ANALOG_RX];  // Right stick Y
  uint8_t l2 = event->analog[ANALOG_RZ];    // L2 trigger (stored in RZ)
  uint8_t r2 = event->analog[ANALOG_SLIDER]; // R2 trigger (stored in SLIDER)

  // Apply profile remapping
  const profile_t* profile = profile_get_active(OUTPUT_TARGET_3DO);
  profile_output_t mapped;
  profile_apply(profile, buttons, ax, ay, az, at, l2, r2, &mapped);

  // Check if silly pad mode is enabled
  if (output_mode == TDO_MODE_SILLY) {
    // Silly control pad mode for arcade JAMMA integration
    _3do_silly_report report = new_3do_silly_report();

    // Map buttons to arcade functions
    // USB active-low: 0 = pressed, 3DO active-HIGH: 1 = pressed
    // Player 1: Select = Coin, Start = Start
    // Player 2: L1 = Coin, R1 = Start
    // Service: L1 + R1 together
    if (player_index == 0) {
      report.p1_coin = (mapped.buttons & USBR_BUTTON_S1) == 0 ? 1 : 0;   // Select = P1 Coin
      report.p1_start = (mapped.buttons & USBR_BUTTON_S2) == 0 ? 1 : 0;  // Start = P1 Start
      report.p2_coin = (mapped.buttons & USBR_BUTTON_L1) == 0 ? 1 : 0;   // L1 = P2 Coin
      report.p2_start = (mapped.buttons & USBR_BUTTON_R1) == 0 ? 1 : 0;  // R1 = P2 Start
      // Service = L2 + R2 together
      bool l2_pressed = (mapped.buttons & USBR_BUTTON_L2) == 0;
      bool r2_pressed = (mapped.buttons & USBR_BUTTON_R2) == 0;
      report.service = (l2_pressed && r2_pressed) ? 1 : 0;
    }

    update_3do_silly(report, player_index);
    return;
  }

  // Determine if this is a flight stick (has significant analog input)
  // TODO: Add better heuristics or device-type detection
  bool is_joystick = false;

  if (is_joystick) {
    // Send joystick report
    _3do_joystick_report report = new_3do_joystick_report();

    // Map analog axes
    report.analog1 = mapped.left_x;
    report.analog2 = mapped.left_y;
    report.analog3 = mapped.right_x;
    report.analog4 = mapped.right_y;

    // Apply remapped buttons to joystick report
    map_usbr_to_3do_joystick(&report, mapped.buttons);

    // Map D-pad directly (D-pad passes through profile unchanged)
    // USB active-low: 0 = pressed, 3DO active-HIGH: 1 = pressed
    report.left = (mapped.buttons & USBR_BUTTON_DL) == 0 ? 1 : 0;
    report.right = (mapped.buttons & USBR_BUTTON_DR) == 0 ? 1 : 0;
    report.up = (mapped.buttons & USBR_BUTTON_DU) == 0 ? 1 : 0;
    report.down = (mapped.buttons & USBR_BUTTON_DD) == 0 ? 1 : 0;

    // If no digital D-pad pressed, use left analog stick
    if (report.left == 0 && report.right == 0 && report.up == 0 && report.down == 0) {
      report.left = (mapped.left_x < 64) ? 1 : 0;
      report.right = (mapped.left_x > 192) ? 1 : 0;
      report.up = (mapped.left_y > 192) ? 1 : 0;      // Inverted Y axis
      report.down = (mapped.left_y < 64) ? 1 : 0;     // Inverted Y axis
    }

    update_3do_joystick(report, player_index);
  } else {
    // Send joypad report
    _3do_joypad_report report = new_3do_joypad_report();

    // Apply remapped buttons to joypad report
    map_usbr_to_3do_joypad(&report, mapped.buttons);

    // Map D-pad directly (D-pad passes through profile unchanged)
    // USB active-low: 0 = pressed, 3DO active-HIGH: 1 = pressed
    report.left = (mapped.buttons & USBR_BUTTON_DL) == 0 ? 1 : 0;
    report.right = (mapped.buttons & USBR_BUTTON_DR) == 0 ? 1 : 0;
    report.up = (mapped.buttons & USBR_BUTTON_DU) == 0 ? 1 : 0;
    report.down = (mapped.buttons & USBR_BUTTON_DD) == 0 ? 1 : 0;

    // If no digital D-pad pressed, use left analog stick
    if (report.left == 0 && report.right == 0 && report.up == 0 && report.down == 0) {
      report.left = (mapped.left_x < 64) ? 1 : 0;
      report.right = (mapped.left_x > 192) ? 1 : 0;
      report.up = (mapped.left_y > 192) ? 1 : 0;      // Inverted Y axis
      report.down = (mapped.left_y < 64) ? 1 : 0;     // Inverted Y axis
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

const OutputInterface tdo_output_interface = {
    .name = "3DO",
    .init = _3do_init,
    .core1_entry = core1_entry,
    .task = _3do_task,  // 3DO needs periodic polling and extension controller detection
    .get_rumble = NULL,  // 3DO doesn't have rumble
    .get_player_led = NULL,  // 3DO doesn't override player LED
    // Profile system
    .get_profile_count = tdo_get_profile_count,
    .get_active_profile = tdo_get_active_profile,
    .set_active_profile = tdo_set_active_profile,
    .get_profile_name = tdo_get_profile_name,
    .get_trigger_threshold = NULL,  // 3DO profiles don't use adaptive triggers
};
