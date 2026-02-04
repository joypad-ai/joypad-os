// loopy.c

#include "loopy_device.h"
#include "loopy_buttons.h"

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

// Console-local state (not input data)
#include "core/router/router.h"
#include "core/services/codes/codes.h"
#include "core/services/profiles/profile.h"
#include "core/uart.h"

PIO pio;
uint sm1, sm2, sm3;

uint32_t output_word = 0;

// init for casio loopy communication
void loopy_init()
{
  stdio_init_all();

  // Initialize chosen UART
  uart_init(UART_ID, BAUD_RATE);

  // Set the GPIO function for the UART pins
  gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

  // Initialize stdio (redirects printf to UART)
  stdio_uart_init();

  pio = pio0; // Both state machines can run on the same PIO processor

  gpio_init(ROW0_PIN);
  gpio_init(ROW1_PIN);
  gpio_init(ROW2_PIN);
  gpio_init(ROW3_PIN);
  gpio_init(ROW4_PIN);
  gpio_init(ROW5_PIN);

  gpio_set_dir(ROW0_PIN, GPIO_IN);
  gpio_set_dir(ROW1_PIN, GPIO_IN);
  gpio_set_dir(ROW2_PIN, GPIO_IN);
  gpio_set_dir(ROW3_PIN, GPIO_IN);
  gpio_set_dir(ROW4_PIN, GPIO_IN);
  gpio_set_dir(ROW5_PIN, GPIO_IN);

  unsigned short int i;
  for (i = 0; i < 8; ++i) {
    gpio_init(BIT0_PIN + i);
    gpio_set_dir(BIT0_PIN + i, GPIO_OUT);
    gpio_put(BIT0_PIN + i, 0);
  }

  // Load the plex (multiplex output) program, and configure a free state machine
  // to run the program.

  // uint offset = pio_add_program(pio1, &loopy_program);
  // sm1 = pio_claim_unused_sm(pio, true);
  // loopy_program_init(pio, sm1, offset, ROW0_PIN, BIT0_PIN);

  // uint offset2 = pio_add_program(pio, &loopy_program);
  // sm2 = pio_claim_unused_sm(pio, true);
  // loopy_program_init(pio, sm2, offset2, ROW1_PIN, BIT0_PIN);

  // uint offset3 = pio_add_program(pio, &loopy_program);
  // sm3 = pio_claim_unused_sm(pio, true);
  // loopy_program_init(pio, sm3, offset3, ROW2_PIN, BIT0_PIN);

  output_word = 0;  // no buttons pushed
}

//
// core1_task - inner-loop for the second core
//
void __not_in_flash_func(core1_task)(void)
{
  static bool rx_bit = 0;

  while (1)
  {
    //
    // rx_bit = pio_sm_get(pio, sm1);

    // assume data is already formatted in output_word and push it to the state machine
    // pio_sm_put(pio, sm1, output_word & 0x0f);
    // pio_sm_put(pio, sm2, (output_word << 4) & 0x0f);
    // pio_sm_put(pio, sm3, (output_word << 8) & 0x0f);
    // TODO: implement gamepad, mouse, multi-tap output with PIO.

    // Get input from router (Loopy uses SIMPLE mode, 1:1 per player slot)
    const input_event_t* event1 = router_get_output(OUTPUT_TARGET_LOOPY, 0);
    const input_event_t* event2 = router_get_output(OUTPUT_TARGET_LOOPY, 1);
    const input_event_t* event3 = router_get_output(OUTPUT_TARGET_LOOPY, 2);
    const input_event_t* event4 = router_get_output(OUTPUT_TARGET_LOOPY, 3);

    // Apply profile mapping to each player's input
    const profile_t* profile = profile_get_active(OUTPUT_TARGET_LOOPY);
    profile_output_t mapped1, mapped2, mapped3, mapped4;

    if (event1 && playersCount >= 1) {
      profile_apply(profile, event1->buttons,
                    event1->analog[ANALOG_LX], event1->analog[ANALOG_LY],
                    event1->analog[ANALOG_RX], event1->analog[ANALOG_RY],
                    event1->analog[ANALOG_L2], event1->analog[ANALOG_R2], &mapped1);
    }
    if (event2 && playersCount >= 2) {
      profile_apply(profile, event2->buttons,
                    event2->analog[ANALOG_LX], event2->analog[ANALOG_LY],
                    event2->analog[ANALOG_RX], event2->analog[ANALOG_RY],
                    event2->analog[ANALOG_L2], event2->analog[ANALOG_R2], &mapped2);
    }
    if (event3 && playersCount >= 3) {
      profile_apply(profile, event3->buttons,
                    event3->analog[ANALOG_LX], event3->analog[ANALOG_LY],
                    event3->analog[ANALOG_RX], event3->analog[ANALOG_RY],
                    event3->analog[ANALOG_L2], event3->analog[ANALOG_R2], &mapped3);
    }
    if (event4 && playersCount >= 4) {
      profile_apply(profile, event4->buttons,
                    event4->analog[ANALOG_LX], event4->analog[ANALOG_LY],
                    event4->analog[ANALOG_RX], event4->analog[ANALOG_RY],
                    event4->analog[ANALOG_L2], event4->analog[ANALOG_R2], &mapped4);
    }

    // Loopy hardware expects active-low (0 = pressed), so invert for output
    // When no player, set to 0xffff (all released in active-low)
    int16_t player_1 = (event1 && playersCount >= 1) ? (~mapped1.buttons & 0xffff) : 0xffff;
    int16_t player_2 = (event2 && playersCount >= 2) ? (~mapped2.buttons & 0xffff) : 0xffff;
    int16_t player_3 = (event3 && playersCount >= 3) ? (~mapped3.buttons & 0xffff) : 0xffff;
    int16_t player_4 = (event4 && playersCount >= 4) ? (~mapped4.buttons & 0xffff) : 0xffff;
    // Mouse detection: check if lower nybble is all 1s (active-low pressed)
    bool is_mouse = !(player_1 & 0x0f);
    // TODO: properly handle mouse detection at boot

    uint8_t loopy_byte = 0;

    // TODO: construct 48-bit output_word with this logic to reduce steps at this phase.
    if (!is_mouse) {
      // Gamepad output
      //
      //        bit0        bit1        bit2        bit3
      // ROW0   Presence    Start       Trigger-L   Trigger-R
      // ROW1   A           D           C           B
      // ROW2   Dpad-Up     Dpad-Down   Dpad-Left   Dpad-Right
      //

      if (gpio_get(ROW0_PIN)) {
        // Player 1 - ROW0
        loopy_byte |= LOOPY_BIT0; // Presence
        loopy_byte |= ((player_1 & JP_BUTTON_S2) == 0) ? LOOPY_BIT1 : 0; // Start
        loopy_byte |= ((player_1 & JP_BUTTON_L1) == 0) ? LOOPY_BIT2 : 0; // L
        loopy_byte |= ((player_1 & JP_BUTTON_R1) == 0) ? LOOPY_BIT3 : 0; // R

        // Player 2 - ROW0
        loopy_byte |= (playersCount >= 2               ) ? LOOPY_BIT4 : 0; // Presence
        loopy_byte |= ((player_2 & JP_BUTTON_S2) == 0) ? LOOPY_BIT5 : 0; // Start
        loopy_byte |= ((player_2 & JP_BUTTON_L1) == 0) ? LOOPY_BIT6 : 0; // L
        loopy_byte |= ((player_2 & JP_BUTTON_R1) == 0) ? LOOPY_BIT7 : 0; // R

      } else if (gpio_get(ROW1_PIN)) {
        // Player 1 - ROW1
        loopy_byte |= ((player_1 & JP_BUTTON_B1) == 0) ? LOOPY_BIT0 : 0; // A
        loopy_byte |= ((player_1 & JP_BUTTON_B4) == 0) ? LOOPY_BIT1 : 0; // D
        loopy_byte |= ((player_1 & JP_BUTTON_B3) == 0) ? LOOPY_BIT2 : 0; // C
        loopy_byte |= ((player_1 & JP_BUTTON_B2) == 0) ? LOOPY_BIT3 : 0; // B

        // Player 2 - ROW1
        loopy_byte |= ((player_2 & JP_BUTTON_B1) == 0) ? LOOPY_BIT4 : 0; // A
        loopy_byte |= ((player_2 & JP_BUTTON_B4) == 0) ? LOOPY_BIT5 : 0; // D
        loopy_byte |= ((player_2 & JP_BUTTON_B3) == 0) ? LOOPY_BIT6 : 0; // C
        loopy_byte |= ((player_2 & JP_BUTTON_B2) == 0) ? LOOPY_BIT7 : 0; // B

      } else if (gpio_get(ROW2_PIN)) {
        // Player 1 - ROW2
        loopy_byte |= ((player_1 & JP_BUTTON_DU) == 0) ? LOOPY_BIT0 : 0; // Up
        loopy_byte |= ((player_1 & JP_BUTTON_DD) == 0) ? LOOPY_BIT1 : 0; // Down
        loopy_byte |= ((player_1 & JP_BUTTON_DL) == 0) ? LOOPY_BIT2 : 0; // Left
        loopy_byte |= ((player_1 & JP_BUTTON_DR) == 0) ? LOOPY_BIT3 : 0; // Right

        // Player 2 - ROW2
        loopy_byte |= ((player_2 & JP_BUTTON_DU) == 0) ? LOOPY_BIT4 : 0; // Up
        loopy_byte |= ((player_2 & JP_BUTTON_DD) == 0) ? LOOPY_BIT5 : 0; // Down
        loopy_byte |= ((player_2 & JP_BUTTON_DL) == 0) ? LOOPY_BIT6 : 0; // Left
        loopy_byte |= ((player_2 & JP_BUTTON_DR) == 0) ? LOOPY_BIT7 : 0; // Right

      } else if (gpio_get(ROW3_PIN)) {
        // Player 3 - ROW0
        loopy_byte |= (playersCount >= 3               ) ? LOOPY_BIT0 : 0; // Presence
        loopy_byte |= ((player_3 & JP_BUTTON_S2) == 0) ? LOOPY_BIT1 : 0; // Start
        loopy_byte |= ((player_3 & JP_BUTTON_L1) == 0) ? LOOPY_BIT2 : 0; // L
        loopy_byte |= ((player_3 & JP_BUTTON_R1) == 0) ? LOOPY_BIT3 : 0; // R

        // Player 4 - ROW0
        loopy_byte |= (playersCount >= 4               ) ? LOOPY_BIT4 : 0; // Presence
        loopy_byte |= ((player_4 & JP_BUTTON_S2) == 0) ? LOOPY_BIT5 : 0; // Start
        loopy_byte |= ((player_4 & JP_BUTTON_L1) == 0) ? LOOPY_BIT6 : 0; // L
        loopy_byte |= ((player_4 & JP_BUTTON_R1) == 0) ? LOOPY_BIT7 : 0; // R

      } else if (gpio_get(ROW4_PIN)) {
        // Player 3 - ROW1
        loopy_byte |= ((player_3 & JP_BUTTON_B1) == 0) ? LOOPY_BIT0 : 0; // A
        loopy_byte |= ((player_3 & JP_BUTTON_B4) == 0) ? LOOPY_BIT1 : 0; // D
        loopy_byte |= ((player_3 & JP_BUTTON_B3) == 0) ? LOOPY_BIT2 : 0; // C
        loopy_byte |= ((player_3 & JP_BUTTON_B2) == 0) ? LOOPY_BIT3 : 0; // B

        // Player 4 - ROW1
        loopy_byte |= ((player_4 & JP_BUTTON_B1) == 0) ? LOOPY_BIT4 : 0; // A
        loopy_byte |= ((player_4 & JP_BUTTON_B4) == 0) ? LOOPY_BIT5 : 0; // D
        loopy_byte |= ((player_4 & JP_BUTTON_B3) == 0) ? LOOPY_BIT6 : 0; // C
        loopy_byte |= ((player_4 & JP_BUTTON_B2) == 0) ? LOOPY_BIT7 : 0; // B

      } else if (gpio_get(ROW5_PIN)) {
        // Player 3 - ROW2
        loopy_byte |= ((player_3 & JP_BUTTON_DU) == 0) ? LOOPY_BIT0 : 0; // Up
        loopy_byte |= ((player_3 & JP_BUTTON_DD) == 0) ? LOOPY_BIT1 : 0; // Down
        loopy_byte |= ((player_3 & JP_BUTTON_DL) == 0) ? LOOPY_BIT2 : 0; // Left
        loopy_byte |= ((player_3 & JP_BUTTON_DR) == 0) ? LOOPY_BIT3 : 0; // Right

        // Player 4 - ROW2
        loopy_byte |= ((player_4 & JP_BUTTON_DU) == 0) ? LOOPY_BIT4 : 0; // Up
        loopy_byte |= ((player_4 & JP_BUTTON_DD) == 0) ? LOOPY_BIT5 : 0; // Down
        loopy_byte |= ((player_4 & JP_BUTTON_DL) == 0) ? LOOPY_BIT6 : 0; // Left
        loopy_byte |= ((player_4 & JP_BUTTON_DR) == 0) ? LOOPY_BIT7 : 0; // Right
      }

    } else {
      // Mouse output
      //
      // bit0     bit1     bit2     bit3     bit4     bit5     bit6     bit7
      // [X encoder raw]   [Y encoder raw]   Left     N/C      Right    Presence
      //

      uint8_t x_gray = event1 ? event1->analog[0] : 128;
      uint8_t y_gray = event1 ? event1->analog[1] : 128;
      // printf("[raw_gray_code] [%d, %d]\n", x_gray, y_gray);

      loopy_byte |= ((x_gray)      & 0x1             ) ? LOOPY_BIT0 : 0; // X
      loopy_byte |= ((x_gray >> 1) & 0x1             ) ? LOOPY_BIT1 : 0; // X
      loopy_byte |= ((y_gray)      & 0x1             ) ? LOOPY_BIT2 : 0; // Y
      loopy_byte |= ((y_gray >> 1) & 0x1             ) ? LOOPY_BIT3 : 0; // Y
      loopy_byte |= ((player_1 & JP_BUTTON_B1) == 0) ? LOOPY_BIT4 : 0; // Left
      loopy_byte |= ((0)                             ) ? LOOPY_BIT5 : 0; // N/C
      loopy_byte |= ((player_1 & JP_BUTTON_B2) == 0) ? LOOPY_BIT6 : 0; // Right
      loopy_byte |= ((1)                             ) ? LOOPY_BIT7 : 0; // Presence
    }

    gpio_put(BIT0_PIN, (loopy_byte & LOOPY_BIT0) ? 1 : 0);
    gpio_put(BIT1_PIN, (loopy_byte & LOOPY_BIT1) ? 1 : 0);
    gpio_put(BIT2_PIN, (loopy_byte & LOOPY_BIT2) ? 1 : 0);
    gpio_put(BIT3_PIN, (loopy_byte & LOOPY_BIT3) ? 1 : 0);
    gpio_put(BIT4_PIN, (loopy_byte & LOOPY_BIT4) ? 1 : 0);
    gpio_put(BIT5_PIN, (loopy_byte & LOOPY_BIT5) ? 1 : 0);
    gpio_put(BIT6_PIN, (loopy_byte & LOOPY_BIT6) ? 1 : 0);
    gpio_put(BIT7_PIN, (loopy_byte & LOOPY_BIT7) ? 1 : 0);

    update_output();
  }
}

//
// update_output - updates output_word with loopy data that is sent
//                 to the console based on ROW state and device type
//
void __not_in_flash_func(update_output)(void)
{
  int8_t bytes[4] = { 0 };

  // unsigned short int i;
  // for (i = 0; i < MAX_PLAYERS; ++i)
  // {
  // }

  // TODO: parse Joypad button state to output_word here
  // if (playersCount < 1)
  // {
  //   bytes[0] = 0x00;
  //   bytes[1] = 0x00;
  //   bytes[2] = 0x00;
  // } else {
  //   bytes[0] = 0x01;
  //   bytes[1] = players[0].output_buttons & 0x0f;
  //   bytes[2] = players[0].output_buttons & 0xf0;
  // }

  // bool isMouse = !(players[0].output_buttons & 0x0f);

  // // mouse x/y states
  // if (isMouse)
  // {
  //   // TODO:
  //   int8_t mouse_byte = 0b10000000;

  //   bytes[0] = mouse_byte;
  //   bytes[1] = mouse_byte;
  //   bytes[2] = mouse_byte;
  // }

  // output_word = ((bytes[0] & 0xff))      | // ROW0
  //               ((bytes[1] & 0xff) << 8) | // ROW1
  //               ((bytes[2] & 0xff) << 16); // ROW2

  codes_task_for_output(OUTPUT_TARGET_LOOPY);
}

// post_input_event removed - replaced by router architecture
// Input flow: USB drivers → router_submit_input() → router → router_get_output() → core1_task()

// ============================================================================
// OUTPUT INTERFACE
// ============================================================================

#include "core/output_interface.h"

// Profile accessor functions for OutputInterface
static uint8_t loopy_get_profile_count(void) {
    return profile_get_count(OUTPUT_TARGET_LOOPY);
}

static uint8_t loopy_get_active_profile(void) {
    return profile_get_active_index(OUTPUT_TARGET_LOOPY);
}

static void loopy_set_active_profile(uint8_t index) {
    profile_set_active(OUTPUT_TARGET_LOOPY, index);
}

static const char* loopy_get_profile_name(uint8_t index) {
    return profile_get_name(OUTPUT_TARGET_LOOPY, index);
}

const OutputInterface loopy_output_interface = {
    .name = "Loopy",
    .target = OUTPUT_TARGET_LOOPY,
    .init = loopy_init,
    .core1_task = core1_task,
    .task = NULL,  // Loopy doesn't need periodic task
    .get_rumble = NULL,
    .get_player_led = NULL,
    .get_profile_count = loopy_get_profile_count,
    .get_active_profile = loopy_get_active_profile,
    .set_active_profile = loopy_set_active_profile,
    .get_profile_name = loopy_get_profile_name,
    .get_trigger_threshold = NULL,
};
