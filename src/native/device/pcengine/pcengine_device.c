// pcengine.c

#include "pcengine_device.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/structs/iobank0.h"
#include "hardware/structs/padsbank0.h"
#include "hardware/structs/sio.h"

// Early init constructor - runs before main() to set output pins HIGH
// This prevents "all buttons pressed" state during boot
__attribute__((constructor(101)))
static void pce_early_gpio_init(void)
{
    // Direct register access for fastest possible init
    // Set output pins as outputs with HIGH value
    // OUTD0_PIN through OUTD0_PIN+3 (either 4-7 for Pico or 26-29 for KB2040)
    
    #ifdef RPI_PICO_BUILD
    const uint32_t pin_mask = (1u << 4) | (1u << 5) | (1u << 6) | (1u << 7);
    #else
    const uint32_t pin_mask = (1u << 26) | (1u << 27) | (1u << 28) | (1u << 29);
    #endif
    
    // Enable outputs and set HIGH
    sio_hw->gpio_oe_set = pin_mask;
    sio_hw->gpio_set = pin_mask;
}

#if CFG_TUSB_DEBUG >= 1
#include "hardware/uart.h"
#endif

uint64_t cpu_frequency;
uint64_t timer_threshold;
uint64_t timer_threshold_a;
uint64_t timer_threshold_b;
uint64_t turbo_frequency;

PIO pio;
uint sm1, sm2, sm3;

// When PCE reads, set interlock to ensure atomic update
//
volatile bool  output_exclude = false;

// output_word -> is the word sent to the state machine for output
//
// Structure of the word sent to the FIFO from the ARM:
// |  word_1|                             word_0
// |PLAYER_5|PLAYER_4|PLAYER_3|PLAYER_2|PLAYER_1
//
// 2-button mode byte: [Left, Down, Right, Up, Run, Select, II, I]
//  - all player button bytes are sent every cycle.
// 6-button mode byte: [III, IV, V, VI, 0, 0, 0, 0]
//  - every other cycle alternates between default
//    2-button byte and extended button byte.
// pce-mouse mode bytes:
//  - when mouse present, player buttons [Run, Select, II, I] are sent
//    as the most significant nybble. the least significant nybble holds
//    the x-axis and y-axis broken into nyybles sent over four cycles.
//    |CYCLE__4|CYCLE__3|CYCLE__2|CYCLE__1
//    |bbbbXXXX|bbbbxxxx|bbbbYYYY|bbbbyyyy
// where:
//  - b = button values, arranged in Run/Sel/II/I sequence for PC Engine use
//  - Xx = mouse 'x' movement; left is {1 - 0x7F} ; right is {0xFF - 0x80 }
//  - Yy = mouse 'y' movement;  up  is {1 - 0x7F} ; down  is {0xFF - 0x80 }
//
volatile uint32_t output_word_0 = 0;
volatile uint32_t output_word_1 = 0;

volatile int state = 0; // countdown sequence for shift-register position (shared between cores)

// Timing for scan boundary detection (needed for mouse - like PCEMouse)
static volatile absolute_time_t init_time;
static const int64_t reset_period = 600; // at 600us of no CLK edges, scan is complete

// Console-local state (not input data)
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/services/players/manager.h"
#include "core/services/codes/codes.h"

static struct {
    volatile int button_mode[MAX_PLAYERS];  // Button mode per player (6-button, 2-button, etc.)
    volatile uint8_t normal_byte[MAX_PLAYERS];  // Cached normal output byte (d-pad + buttons)
    volatile uint8_t ext_byte[MAX_PLAYERS];     // Cached 6-button extended byte
    volatile bool is_mouse[MAX_PLAYERS];
    volatile int16_t mouse_global_x[MAX_PLAYERS];  // Accumulated X deltas (like PCEMouse global_x)
    volatile int16_t mouse_global_y[MAX_PLAYERS];  // Accumulated Y deltas (like PCEMouse global_y)
    volatile int16_t mouse_output_x[MAX_PLAYERS];  // Output X being sent (like PCEMouse output_x)
    volatile int16_t mouse_output_y[MAX_PLAYERS];  // Output Y being sent (like PCEMouse output_y)
} pce_state = {
    .button_mode = {BUTTON_MODE_2, BUTTON_MODE_2, BUTTON_MODE_2, BUTTON_MODE_2, BUTTON_MODE_2},
    .normal_byte = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    .ext_byte = {0xF0, 0xF0, 0xF0, 0xF0, 0xF0},
    .is_mouse = {false},
    .mouse_global_x = {0},
    .mouse_global_y = {0},
    .mouse_output_x = {0},
    .mouse_output_y = {0}
};

// No timers needed - state cycles event-driven on CLK edges

// Forward declarations
void read_inputs(void);
void assemble_output(void);

// init for pcengine communication
void pce_init()
{
  // Set output pins HIGH immediately to prevent "all buttons pressed" during boot
  // This must happen BEFORE PIO takes over the pins
  gpio_init(OUTD0_PIN);
  gpio_init(OUTD0_PIN + 1);
  gpio_init(OUTD0_PIN + 2);
  gpio_init(OUTD0_PIN + 3);
  gpio_set_dir(OUTD0_PIN, GPIO_OUT);
  gpio_set_dir(OUTD0_PIN + 1, GPIO_OUT);
  gpio_set_dir(OUTD0_PIN + 2, GPIO_OUT);
  gpio_set_dir(OUTD0_PIN + 3, GPIO_OUT);
  gpio_put(OUTD0_PIN, 1);
  gpio_put(OUTD0_PIN + 1, 1);
  gpio_put(OUTD0_PIN + 2, 1);
  gpio_put(OUTD0_PIN + 3, 1);

  #if CFG_TUSB_DEBUG >= 1
  // Initialize chosen UART
  uart_init(UART_ID, BAUD_RATE);

  // Set the GPIO function for the UART pins
  gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
  #endif

  // use turbo button feature with PCE
  turbo_init();

  pio = pio0; // Both state machines can run on the same PIO processor

  // Load the plex (multiplex output) program, and configure a free state machine
  // to run the program.

  uint offset1 = pio_add_program(pio, &plex_program);
  sm1 = pio_claim_unused_sm(pio, true);
  plex_program_init(pio, sm1, offset1, DATAIN_PIN, CLKIN_PIN, OUTD0_PIN);

  // Load the clock/select (synchronizing input) programs, and configure a free state machines
  // to run the programs.

  uint offset2 = pio_add_program(pio, &clock_program);
  sm2 = pio_claim_unused_sm(pio, true);
  clock_program_init(pio, sm2, offset2, CLKIN_PIN, OUTD0_PIN);

  uint offset3 = pio_add_program(pio, &select_program);
  sm3 = pio_claim_unused_sm(pio, true);
  select_program_init(pio, sm3, offset3, DATAIN_PIN);

  state = 3;

  output_word_0 = 0xFFFFFFFF;  // no buttons pushed
  output_word_1 = 0x000000FF;  // no buttons pushed
  
  // Prime the PIO FIFO - plex program starts at pull block waiting for data
  pio_sm_put(pio, sm1, output_word_1);
  pio_sm_put(pio, sm1, output_word_0);
  
  // Initialize timing (like PCEMouse)
  init_time = get_absolute_time();
}

// init turbo button timings
void turbo_init()
{
    cpu_frequency = clock_get_hz(clk_sys);
    turbo_frequency = 1000000; // Default turbo frequency
    timer_threshold_a = cpu_frequency / (turbo_frequency * 2);
    timer_threshold_b = cpu_frequency / (turbo_frequency * 20);
    timer_threshold = timer_threshold_a;
}

// task process - runs on core0, keeps cached button values fresh
void pce_task()
{
  // Check for scan boundary timeout (like PCEMouse process_signals)
  // After 600us of no CLK edges, the scan is complete - unlock for updates
  // Note: don't reset state here - let core1 handle state transitions
  absolute_time_t current_time = get_absolute_time();
  if (absolute_time_diff_us(init_time, current_time) > reset_period) {
    output_exclude = false;  // Allow core0 to update output values
    init_time = current_time;
  }
  
  // Continuously read input and cache it - core1 will use cached values
  read_inputs();
}

//

//
// core1_task - inner-loop for the second core
//             - when the "CLR" line is de-asserted, set lock flag
//               protecting the output state machine from inconsistent data
//
void __not_in_flash_func(core1_task)(void)
{
  static bool rx_bit = 0;

  while (1)
  {
    // wait for CLK rising edge (from clock.pio via sm2)
    rx_bit = pio_sm_get_blocking(pio, sm2);

    // Lock output values during scan (like PCEMouse)
    output_exclude = true;

    // Assemble output for CURRENT state using cached button values
    assemble_output();
    
    // Push to PIO and advance state ONLY when FIFO has room
    // This synchronizes state with actual console reads (critical for 6-button!)
    if (!pio_sm_is_tx_fifo_full(pio, sm1)) {
      pio_sm_put(pio, sm1, output_word_1);
      pio_sm_put(pio, sm1, output_word_0);
      
      // Advance state: 3 → 2 → 1 → 0 → 3 → ...
      if (state != 0) {
        state--;
        // Renew countdown timeframe (like PCEMouse)
        init_time = get_absolute_time();
      } else {
        // State 0: reset mouse outputs (matching PCEMouse exactly)
        for (int i = 0; i < MAX_PLAYERS; i++) {
          if (pce_state.is_mouse[i]) {
            pce_state.mouse_global_x[i] -= pce_state.mouse_output_x[i];
            pce_state.mouse_global_y[i] -= pce_state.mouse_output_y[i];
            pce_state.mouse_output_x[i] = 0;
            pce_state.mouse_output_y[i] = 0;
          }
        }
        // Reset to state 3 for next cycle
        state = 3;
        // Keep output_exclude = true for mouse - pce_task timeout will clear it
        output_exclude = true;
      }
    }
  }
}

//
// read_inputs - reads button state from router and caches it (HEAVY - once per scan)
//
void __not_in_flash_func(read_inputs)(void)
{
  static uint32_t turbo_timer = 0;
  static bool turbo_state = false;
  int16_t hotkey = 0;

  // Increment the timer and check if it reaches the threshold
  turbo_timer++;
  if (turbo_timer >= timer_threshold)
  {
    turbo_timer = 0;
    turbo_state = !turbo_state;
  }

  for (unsigned short int i = 0; i < MAX_PLAYERS; ++i)
  {
    const input_event_t* event = router_get_output(OUTPUT_TARGET_PCENGINE, i);

    // Player slot out of range - reset to neutral (including mouse state)
    if (i >= playersCount) {
      pce_state.normal_byte[i] = 0xFF;
      pce_state.ext_byte[i] = 0xF0;
      pce_state.is_mouse[i] = false;
      pce_state.mouse_global_x[i] = 0;
      pce_state.mouse_global_y[i] = 0;
      pce_state.mouse_output_x[i] = 0;
      pce_state.mouse_output_y[i] = 0;
      continue;
    }
    
    // No new event - keep existing state (important for mouse!)
    if (!event) {
      continue;
    }

    // Build normal byte (d-pad + buttons)
    uint8_t normal = 0xFF;
    
    // D-pad from digital buttons
    if (event->buttons & JP_BUTTON_DU) normal &= ~(1 << 0);
    if (event->buttons & JP_BUTTON_DR) normal &= ~(1 << 1);
    if (event->buttons & JP_BUTTON_DD) normal &= ~(1 << 2);
    if (event->buttons & JP_BUTTON_DL) normal &= ~(1 << 3);
    
    // D-pad from left analog stick (threshold at 64/192 from center 128)
    // HID convention: 0=up, 128=center, 255=down
    if (event->analog[0] < 64)  normal &= ~(1 << 3);  // Left
    if (event->analog[0] > 192) normal &= ~(1 << 1);  // Right
    if (event->analog[1] < 64)  normal &= ~(1 << 0);  // Up (low Y = stick up)
    if (event->analog[1] > 192) normal &= ~(1 << 2);  // Down (high Y = stick down)
    if (event->buttons & JP_BUTTON_B2) normal &= ~(1 << 4);  // I
    if (event->buttons & JP_BUTTON_B1) normal &= ~(1 << 5);  // II
    if (event->buttons & JP_BUTTON_S1) normal &= ~(1 << 6);  // Select
    if (event->buttons & JP_BUTTON_S2) normal &= ~(1 << 7);  // Run

    // Keyboard: A1 → Select+Run
    if (event->type == INPUT_TYPE_KEYBOARD && (event->buttons & JP_BUTTON_A1)) {
      normal &= ~((1 << 6) | (1 << 7));
    }

    // Hotkey detection
    bool s2_pressed = (event->buttons & JP_BUTTON_S2) != 0;
    if (s2_pressed && (event->buttons & JP_BUTTON_DU))
      pce_state.button_mode[i] = BUTTON_MODE_6;
    else if (s2_pressed && (event->buttons & JP_BUTTON_DD))
      pce_state.button_mode[i] = BUTTON_MODE_2;
    else if (s2_pressed && (event->buttons & JP_BUTTON_DL))
      pce_state.button_mode[i] = BUTTON_MODE_3_SEL;
    else if (s2_pressed && (event->buttons & JP_BUTTON_DR))
      pce_state.button_mode[i] = BUTTON_MODE_3_RUN;

    // Turbo EverDrive Pro hot-key fix
    if (hotkey) {
      normal &= hotkey;
    } else if (i == 0) {
      int16_t btns = (normal & 0xff);
      if (btns == 0x82) hotkey = ~0x82;
      else if (btns == 0x88) hotkey = ~0x88;
      else if (btns == 0x84) hotkey = ~0x84;
    }

    // 3-button modes
    bool is3btnSel = pce_state.button_mode[i] == BUTTON_MODE_3_SEL;
    bool is3btnRun = pce_state.button_mode[i] == BUTTON_MODE_3_RUN;
    bool is6btn = pce_state.button_mode[i] == BUTTON_MODE_6;

    if (is3btnSel && (event->buttons & JP_BUTTON_B3)) {
      normal &= ~(1 << 6);
    } else if (is3btnRun && (event->buttons & JP_BUTTON_B3)) {
      normal &= ~(1 << 7);
    } else if (!is6btn) {
      // Turbo buttons
      if (turbo_state) {
        if (event->buttons & JP_BUTTON_B3) normal &= ~(1 << 5);
        if (event->buttons & JP_BUTTON_B4) normal &= ~(1 << 4);
      }
      if (event->buttons & JP_BUTTON_L1) timer_threshold = timer_threshold_a;
      if (event->buttons & JP_BUTTON_R1) timer_threshold = timer_threshold_b;
    }

    // Build extended byte (6-button mode)
    uint8_t ext = 0xF0;  // Lower nibble = 0 is the 6-button signature
    if (event->buttons & JP_BUTTON_B3) ext &= ~(1 << 4);  // III
    if (event->buttons & JP_BUTTON_B4) ext &= ~(1 << 5);  // IV
    if (event->buttons & JP_BUTTON_L1) ext &= ~(1 << 6);  // V
    if (event->buttons & JP_BUTTON_R1) ext &= ~(1 << 7);  // VI

    // Mouse handling - accumulate deltas exactly like PCEMouse post_globals
    bool was_mouse = pce_state.is_mouse[i];
    pce_state.is_mouse[i] = (event->type == INPUT_TYPE_MOUSE);
    
    // Clear mouse state when device type changes (prevents drift on disconnect)
    if (was_mouse && !pce_state.is_mouse[i]) {
      pce_state.mouse_global_x[i] = 0;
      pce_state.mouse_global_y[i] = 0;
      pce_state.mouse_output_x[i] = 0;
      pce_state.mouse_output_y[i] = 0;
    }
    
    if (pce_state.is_mouse[i]) {
      // Negate deltas to match PCE direction convention
      uint8_t delta_x = (uint8_t)(-(int8_t)event->delta_x);
      uint8_t delta_y = (uint8_t)(-(int8_t)event->delta_y);
      
      // Accumulate into signed 16-bit (same logic as PCEMouse post_globals)
      if (delta_x >= 128)
        pce_state.mouse_global_x[i] -= (256 - delta_x);
      else
        pce_state.mouse_global_x[i] += delta_x;
      
      if (delta_y >= 128)
        pce_state.mouse_global_y[i] -= (256 - delta_y);
      else
        pce_state.mouse_global_y[i] += delta_y;
      
      // Only copy global to output when not in a scan (like PCEMouse)
      if (!output_exclude) {
        pce_state.mouse_output_x[i] = pce_state.mouse_global_x[i];
        pce_state.mouse_output_y[i] = pce_state.mouse_global_y[i];
      }
    }

    pce_state.normal_byte[i] = normal;
    pce_state.ext_byte[i] = ext;
  }

  codes_task_for_output(OUTPUT_TARGET_PCENGINE);
}

//
// assemble_output - fast assembly using cached values + current state (FAST - every CLK edge)
//
void __not_in_flash_func(assemble_output)(void)
{
  uint8_t bytes[5];

  for (int i = 0; i < MAX_PLAYERS; i++) {
    uint8_t byte;

    if (pce_state.is_mouse[i]) {
      // Mouse: buttons in upper nibble, position data in lower nibble
      byte = pce_state.normal_byte[i] & 0xF0;
      
      // Scale down for modern high-DPI mice (total >>2 = divide by 4)
      int16_t ox = pce_state.mouse_output_x[i] >> 1;
      int16_t oy = pce_state.mouse_output_y[i] >> 1;
      switch (state) {
        case 3: byte |= (((ox >> 1) & 0xf0) >> 4); break;  // X MSN
        case 2: byte |= (((ox >> 1) & 0x0f));      break;  // X LSN
        case 1: byte |= (((oy >> 1) & 0xf0) >> 4); break;  // Y MSN
        case 0: byte |= (((oy >> 1) & 0x0f));      break;  // Y LSN
      }
    } else if (pce_state.button_mode[i] == BUTTON_MODE_6 && (state == 2 || state == 0)) {
      // 6-button mode, states 2 and 0: output extended byte (with signature)
      byte = pce_state.ext_byte[i];
    } else {
      // Normal: output cached normal byte
      byte = pce_state.normal_byte[i];
    }

    bytes[i] = byte;
  }

  output_word_0 = (bytes[0]) | (bytes[1] << 8) | (bytes[2] << 16) | (bytes[3] << 24);
  output_word_1 = bytes[4];
}


// post_input_event removed - replaced by router architecture
// Input flow: USB drivers → router_submit_input() → router → router_get_output() → update_output()

// ============================================================================
// OUTPUT INTERFACE
// ============================================================================

#include "core/output_interface.h"

const OutputInterface pcengine_output_interface = {
    .name = "PCEngine",
    .target = OUTPUT_TARGET_PCENGINE,
    .init = pce_init,
    .core1_task = core1_task,
    .task = pce_task,  // PCEngine needs periodic scan detection task
    .get_rumble = NULL,
    .get_player_led = NULL,
    // No profile system - PCEngine uses fixed button mapping
    .get_profile_count = NULL,
    .get_active_profile = NULL,
    .set_active_profile = NULL,
    .get_profile_name = NULL,
    .get_trigger_threshold = NULL,
};
