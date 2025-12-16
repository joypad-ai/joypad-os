// pcengine.c

#include "pcengine_device.h"
#include "hardware/clocks.h"

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
uint32_t output_word_0 = 0;
uint32_t output_word_1 = 0;

int state = 0; // countdown sequence for shift-register position

// Console-local state (not input data)
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/services/players/manager.h"
#include "core/services/codes/codes.h"

static struct {
    int button_mode[MAX_PLAYERS];  // Button mode per player (6-button, 2-button, etc.)
} pce_state = {
    .button_mode = {BUTTON_MODE_2, BUTTON_MODE_2, BUTTON_MODE_2, BUTTON_MODE_2, BUTTON_MODE_2}
};

static absolute_time_t init_time;
static absolute_time_t current_time;
static absolute_time_t loop_time;
static const int64_t reset_period = 600; // at 600us, reset the scan exclude flag

// init for pcengine communication
void pce_init()
{
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

  output_word_0 = 0x00FFFFFFFF;  // no buttons pushed
  output_word_1 = 0x00000000FF;  // no buttons pushed

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

// task process for checking pcengine polling cycles
void pce_task()
{
  //
  // check time offset in order to detect when a PCE scan is no longer
  // in process (so that fresh values can be sent to the state machine)
  //
  current_time = get_absolute_time();

  if (absolute_time_diff_us(init_time, current_time) > reset_period) {
    state = 3;
    update_output();
    output_exclude = false;
    init_time = get_absolute_time();
  }
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
    // wait for (and sync with) negedge of CLR signal; rx_data is throwaway
    rx_bit = pio_sm_get_blocking(pio, sm2);

    // Now we are in an update-sequence; set a lock
    // to prevent update during output transaction
    output_exclude = true;

    // assume data is already formatted in output_word and push it to the state machine
    pio_sm_put(pio, sm1, output_word_1);
    pio_sm_put(pio, sm1, output_word_0);

    // Sequence from state 3 down through state 0 (show different nybbles to PCE)
    //
    // Note that when state = zero, it doesn't transition to a next state; the reset to
    // state 3 will happen as part of a timed process on the second CPU & state machine
    //

    // Also note that staying in 'scan' (CLK = low, SEL = high), is not expected
    // last more than about a half of a millisecond
    //
    loop_time = get_absolute_time();
    while ((gpio_get(CLKIN_PIN) == 0) && (gpio_get(DATAIN_PIN) == 1))
    {
      if (absolute_time_diff_us(loop_time, get_absolute_time()) > 550)
      {
        state = 0;
        break;
      }
    }

    if (state != 0)
    {
      state--;
      update_output();

      // renew countdown timeframe
      init_time = get_absolute_time();
    }
    else
    {
      update_output();
      output_exclude = true;            // continue to lock the output values (which are now zero)
    }
  }
}

//
// update_output - updates output_word with multi-tap plex data that
//                 is sent to PCE based on state and device types
//
void __not_in_flash_func(update_output)(void)
{

  static uint32_t turbo_timer = 0;
  static bool turbo_state = false;
  int8_t bytes[5] = { 0 };
  int16_t hotkey = 0;

  // Increment the timer and check if it reaches the threshold
  turbo_timer++;
  if (turbo_timer >= timer_threshold)
  {
    turbo_timer = 0;
    turbo_state = !turbo_state;
  }

  unsigned short int i;
  for (i = 0; i < MAX_PLAYERS; ++i)
  {
    // Get input from router (PCEngine uses SIMPLE mode, 1:1 per player slot)
    const input_event_t* event = router_get_output(OUTPUT_TARGET_PCENGINE, i);

    // Skip if no input for this player slot
    if (!event || i >= playersCount) {
      if (!hotkey) {
        bytes[i] = 0xff;
        continue;
      }
    }

    // Map buttons to PCENGINE byte format
    // PCE byte: [S2, S1, B2, B1, DL, DD, DR, DU] = [Run, Select, II, I, Left, Down, Right, Up]
    // PCE bit positions: 7=S2(Run), 6=S1(Select), 5=B2(II), 4=B1(I), 3=DL(Left), 2=DD(Down), 1=DR(Right), 0=DU(Up)
    // PCEngine uses active-low: 0 = pressed, 1 = not pressed
    // Initialize to all buttons not pressed (0xFF)
    int8_t byte = 0xFF;
    
    // Map each button one by one (active-low: clear bit if pressed)
    // Lower nibble (bits 0-3): d-pad directions
    // DU -> PCE bit 0
    if (event->buttons & JP_BUTTON_DU) {
        byte &= ~(1 << 0);
    }
    // DR -> PCE bit 1
    if (event->buttons & JP_BUTTON_DR) {
        byte &= ~(1 << 1);
    }
    // DD -> PCE bit 2
    if (event->buttons & JP_BUTTON_DD) {
        byte &= ~(1 << 2);
    }
    // DL -> PCE bit 3
    if (event->buttons & JP_BUTTON_DL) {
        byte &= ~(1 << 3);
    }
    // Upper nibble (bits 4-7): buttons
    // B1 -> PCE bit 5 (II)
    if (event->buttons & JP_BUTTON_B1) {
        byte &= ~(1 << 5);
    }
    // B2 -> PCE bit 4 (I)
    if (event->buttons & JP_BUTTON_B2) {
        byte &= ~(1 << 4);
    }
    // S1 -> PCE bit 6
    if (event->buttons & JP_BUTTON_S1) {
        byte &= ~(1 << 6);
    }
    // S2 -> PCE bit 7
    if (event->buttons & JP_BUTTON_S2) {
        byte &= ~(1 << 7);
    }

    // Keyboard-specific transforms for PCEngine
    if (event->type == INPUT_TYPE_KEYBOARD) {
      // A1 (Home/Ctrl+Alt+Delete) → SSDS3 IGR combo (Select+Run)
      if (event->buttons & JP_BUTTON_A1) {
        byte &= ~((1 << 6) | (1 << 7));  // Clear Select (bit 6) and Run (bit 7) bits (0 = pressed in PCE)
      }
    }

    // check for 6-button enable/disable hotkeys (active-high: check if BOTH buttons are pressed)
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
    if (hotkey)
    {
      byte &= hotkey;
    }
    else if (i == 0)
    {
      // Check for hotkey combos (active-high buttons)
      int16_t btns = (byte & 0xff);
      if     (btns == 0x82) hotkey = ~0x82; // RUN + RIGHT
      else if(btns == 0x88) hotkey = ~0x88; // RUN + LEFT
      else if(btns == 0x84) hotkey = ~0x84; // RUN + DOWN
    }

    bool has6Btn = (event->button_count >= 6);
    bool isMouse = (event->type == INPUT_TYPE_MOUSE);
    bool is6btn = pce_state.button_mode[i] == BUTTON_MODE_6;
    bool is3btnSel = pce_state.button_mode[i] == BUTTON_MODE_3_SEL;
    bool is3btnRun = pce_state.button_mode[i] == BUTTON_MODE_3_RUN;

    // 6 button extra four buttons (III/IV/V/VI)
    // In 6-button mode, state 2 sends extended buttons instead of normal data
    // Format: [III, IV, V, VI, 0, 0, 0, 0] - bits 7-4 are buttons, bits 3-0 are always 0
    if (is6btn)
    {
      if (state == 2)
      {
        // Initialize to 0xF0: no extended buttons pressed (bits 7-4 = 1111), lower nibble = 0
        byte = 0xF0;
        // B3 -> PCE III (bit 7)
        if (event->buttons & JP_BUTTON_B3) {
          byte &= ~(1 << 4); // III
        }
        // B4 -> PCE IV (bit 6)
        if (event->buttons & JP_BUTTON_B4) {
          byte &= ~(1 << 5); // IV
        }
        // L1 -> PCE V (bit 5)
        if (event->buttons & JP_BUTTON_L1) {
          byte &= ~(1 << 6); // V
        }
        // R1 -> PCE VI (bit 4)
        if (event->buttons & JP_BUTTON_R1) {
          byte &= ~(1 << 7); // VI
        }
      }
    }

    //
    else if (is3btnSel)
    {
      // Check if L1 or R1 pressed (active-high)
      if (event->buttons & JP_BUTTON_B3) {
        byte &= ~(1 << 6);
      }
    }

    //
    else if (is3btnRun)
    {
      // Check if L1 or R1 pressed (active-high)
      if (event->buttons & JP_BUTTON_B3) {
        byte &= ~(1 << 7);
      }
    }

    // Simulated Turbo buttons B3/B4 for II/I and L1/R1 for speeds 1/2
    else {
      // Update the button state based on the turbo_state
      if (turbo_state)
      {
        // B3 -> turbo for II (bit 5), B4 -> turbo for I (bit 4)
        if (event->buttons & JP_BUTTON_B3) byte &= ~(1 << 5);  // Turbo II
        if (event->buttons & JP_BUTTON_B4) byte &= ~(1 << 4);  // Turbo I
      }
      else
      {
        // Set the button state as released
      }

      // Check turbo speed (L1 = speed 1, R1 = speed 2)
      if (event->buttons & JP_BUTTON_L1) timer_threshold = timer_threshold_a;
      if (event->buttons & JP_BUTTON_R1) timer_threshold = timer_threshold_b;
    }

    // mouse x/y states
    // PCEngine mouse expects inverted axes (0 - value)
    if (isMouse)
    {
      uint8_t mouse_x = (0 - event->analog[0]) & 0xff;  // Invert X for PCE
      uint8_t mouse_y = (0 - event->analog[1]) & 0xff;  // Invert Y for PCE
      switch (state)
      {
        case 3: // state 3: x most significant nybble
          byte |= (((mouse_x>>1) & 0xf0) >> 4);
        break;
        case 2: // state 2: x least significant nybble
          byte |= (((mouse_x>>1) & 0x0f));
        break;
        case 1: // state 1: y most significant nybble
          byte |= (((mouse_y>>1) & 0xf0) >> 4);
        break;
        case 0: // state 0: y least significant nybble
          byte |= (((mouse_y>>1) & 0x0f));
        break;
      }
    }

    bytes[i] = byte;
  }

  output_word_0 = ((bytes[0] & 0xff))      | // player 1
                  ((bytes[1] & 0xff) << 8) | // player 2
                  ((bytes[2] & 0xff) << 16)| // player 3
                  ((bytes[3] & 0xff) << 24); // player 4
  output_word_1 = ((bytes[4] & 0xff));       // player 5

  codes_task();

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
