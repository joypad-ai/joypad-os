// nuon.c

#include "nuon_device.h"
#include "nuon_buttons.h"
#include "core/services/codes/codes.h"
#include "core/services/hotkeys/hotkeys.h"
#include "core/services/profiles/profile.h"
#include <math.h>

PIO pio;
uint sm1, sm2;
int crc_lut[256]; // crc look up table

// Definition of global variables
uint32_t output_buttons_0 = 0;
uint32_t output_analog_1x = 0;
uint32_t output_analog_1y = 0;
uint32_t output_analog_2x = 0;
uint32_t output_analog_2y = 0;
uint32_t output_quad_x = 0;

uint32_t device_mode   = 0b10111001100000111001010100000000;
uint32_t device_config = 0b10000000100000110000001100000000;
uint32_t device_switch = 0b10000000100000110000001100000000;

// Console-local state (not input data)
#include "core/router/router.h"

// Stick-to-spinner configuration
bool analog_stick_to_spinner = true;  // Enable right stick to spinner conversion
static int16_t last_stick_angle[MAX_PLAYERS] = {0};  // Track last angle per player

// IGR (In-Game Reset) combo button mask
// This combo triggers GPIO pins for the Nuon internal IGR mod
#define NUON_IGR_COMBO_MASK 0x3030  // Face buttons combo (preserved from original)
#define NUON_IGR_HOLD_DURATION 2000 // Hold duration for power button (ms)

// Forward declaration for GPIO trigger function
static void trigger_button_press(uint8_t pin);

// IGR callback for long hold (power button)
static void nuon_igr_power_callback(uint8_t player, uint32_t held_ms) {
    (void)player;
    (void)held_ms;
    trigger_button_press(POWER_PIN);
}

// IGR callback for quick tap (stop button)
static void nuon_igr_stop_callback(uint8_t player, uint32_t held_ms) {
    (void)player;
    (void)held_ms;
    trigger_button_press(STOP_PIN);
}

// init for nuon communication
void nuon_init(void)
{
  output_buttons_0 = 0b00000000100000001000001100000011; // no buttons pressed
  output_analog_1x = 0b10000000100000110000001100000000; // x1 = 0
  output_analog_1y = 0b10000000100000110000001100000000; // y1 = 0
  output_analog_2x = 0b10000000100000110000001100000000; // x2 = 0
  output_analog_2y = 0b10000000100000110000001100000000; // y2 = 0
  output_quad_x = 0b10000000000000000000000000000000; // quadx = 0

  // PROPERTIES DEV____MOD DEV___CONF DEV____EXT // CTRL_VALUES from SDK joystick.h
  // 0x0000001f 0b10111001 0b10000000 0b10000000 // ANALOG1, STDBUTTONS, DPAD, SHOULDER, EXTBUTTONS
  // 0x0000003f 0b10000000 0b01000000 0b01000000 // ANALOG1, ANALOG2, STDBUTTONS, DPAD, SHOULDER, EXTBUTTONS
  // 0x0000011d 0b11000000 0b00000000 0b10000000 // THROTTLE, ANALOG1, STDBUTTONS, SHOULDER, EXTBUTTONS
  // 0x0000011f 0b11000000 0b01000000 0b00010000 // THROTTLE, ANALOG1, STDBUTTONS, DPAD, SHOULDER, EXTBUTTONS
  // 0x0000014f 0b11010000 0b00000000 0b00000000 // THROTTLE, WHEEL|PADDLE, STDBUTTONS, DPAD, SHOULDER, EXTBUTTONS
  // 0x00000300 0b11000000 0b00000000 0b11000000 // BRAKE, THROTTLE
  // 0x00000341 0b11000000 0b00000000 0b00000000 // BRAKE, THROTTLE, WHEEL|PADDLE, STDBUTTONS
  // 0x0000034f 0b10111001 0b10000000 0b00000000 // BRAKE, THROTTLE, WHEEL|PADDLE, STDBUTTONS, DPAD, SHOULDER, EXTBUTTONS
  // 0x0000041d 0b11000000 0b11000000 0b00000000 // RUDDER|TWIST, ANALOG1, STDBUTTONS, DPAD, EXTBUTTONS
  // 0x00000513 0b10000000 0b00000000 0b00000000 // RUDDER|TWIST, THROTTLE, ANALOG1, DPAD, STDBUTTONS
  // 0x0000051f 0b10000000 0b10000000 0b10000000 // RUDDER|TWIST, THROTTLE, ANALOG1, STDBUTTONS, DPAD, SHOULDER, EXTBUTTONS
  // 0x00000800 0b11010000 0b00000000 0b10000000 // MOUSE|TRACKBALL
  // 0x00000808 0b11010000 0b10000000 0b10000000 // MOUSE|TRACKBALL, EXTBUTTONS
  // 0x00000811 0b11001000 0b00010000 0b00010000 // MOUSE|TRACKBALL, ANALOG1, STDBUTTONS
  // 0x00000815 0b11001000 0b11000000 0b00010000 // MOUSE|TRACKBALL, ANALOG1, STDBUTTONS, SHOULDER
  // 0x0000083f 0b10011101 0b10000000 0b10000000 // MOUSE|TRACKBALL, ANALOG1, ANALOG2, STDBUTTONS, DPAD, SHOULDER, EXTBUTTONS
  // 0x0000103f 0b10011101 0b11000000 0b11000000 // QUADSPINNER1, ANALOG1, ANALOG2, STDBUTTONS, DPAD, SHOULDER, EXTBUTTONS
  // 0x0000101f 0b10111001 0b10000000 0b01000000 // QUADSPINNER1, ANALOG1, STDBUTTONS, DPAD, SHOULDER, EXTBUTTONS
  // 0x00001301 0b11000000 0b11000000 0b11000000 // QUADSPINNER1, BRAKE, THROTTLE, STDBUTTONS
  // 0x0000401d 0b11010000 0b01000000 0b00010000 // THUMBWHEEL1, ANALOG1, STDBUTTONS, SHOULDER, EXTBUTTONS
  // 0x0000451b 0b10011101 0b00000000 0b00000000 // THUMBWHEEL1, RUDDER|TWIST, THROTTLE, STDBUTTONS, DPAD, EXTBUTTONS
  // 0x0000c011 0b10111001 0b11000000 0b01000000 // THUMBWHEEL1, THUMBWHEEL2, ANALOG1, STDBUTTONS
  // 0x0000c01f 0b11000000 0b00000000 0b01000000 // THUMBWHEEL1, THUMBWHEEL2, ANALOG1, STDBUTTONS, DPAD, SHOULDER, EXTBUTTONS
  // 0x0000c03f 0b10011101 0b01000000 0b01000000 // THUMBWHEEL1, THUMBWHEEL2, ANALOG1, ANALOG2, STDBUTTONS, DPAD, SHOULDER, EXTBUTTONS
  // 0x0000c51b 0b10000000 0b11000000 0b11000000 // THUMBWHEEL1, THUMBWHEEL2, RUDDER|TWIST, THROTTLE, ANALOG1, STDBUTTONS, DPAD, EXTBUTTONS
  // 0x0001001d 0b11000000 0b11000000 0b10000000 // FISHINGREEL, ANALOG1, STDBUTTONS, SHOULDER, EXTBUTTONS

  // Sets packets that define device properties
  device_mode   = crc_data_packet(0b10011101, 1);
  device_config = crc_data_packet(0b11000000, 1);
  device_switch = crc_data_packet(0b11000000, 1);

  pio = pio0; // Both state machines can run on the same PIO processor

  // Load the read and write programs, and configure a free state machines
  uint offset2 = pio_add_program(pio, &polyface_read_program);
  sm2 = pio_claim_unused_sm(pio, true);
  polyface_read_program_init(pio, sm2, offset2, DATAIO_PIN);

  uint offset1 = pio_add_program(pio1, &polyface_send_program);
  sm1 = pio_claim_unused_sm(pio1, true);
  polyface_send_program_init(pio1, sm1, offset1, DATAIO_PIN);

  // queue_init(&packet_queue, sizeof(int64_t), 1000);

  // Register IGR hotkeys for internal Nuon reset mod
  // Long hold (2s) triggers power button
  HotkeyDef power_hotkey = {
      .buttons = NUON_IGR_COMBO_MASK,
      .duration_ms = NUON_IGR_HOLD_DURATION,
      .trigger = HOTKEY_TRIGGER_ON_HOLD,
      .callback = nuon_igr_power_callback,
      .global = false
  };
  hotkeys_register(&power_hotkey);

  // Quick tap (release before 2s) triggers stop button
  HotkeyDef stop_hotkey = {
      .buttons = NUON_IGR_COMBO_MASK,
      .duration_ms = NUON_IGR_HOLD_DURATION,
      .trigger = HOTKEY_TRIGGER_ON_TAP,
      .callback = nuon_igr_stop_callback,
      .global = false
  };
  hotkeys_register(&stop_hotkey);
}

// maps default joypad button bit order to nuon's button packet data structure
uint32_t map_nuon_buttons(uint32_t buttons)
{
  uint32_t nuon_buttons = 0x0080;

  // Mapping the buttons (active-high: 1 = pressed)
  nuon_buttons |= (buttons & JP_BUTTON_B2) ? NUON_BUTTON_C_DOWN : 0;  // Circle -> C-DOWN
  nuon_buttons |= (buttons & JP_BUTTON_B1) ? NUON_BUTTON_A  : 0;  // Cross -> A
  nuon_buttons |= (buttons & JP_BUTTON_S2) ? NUON_BUTTON_START : 0;  // Option -> START
  nuon_buttons |= (buttons & JP_BUTTON_S1) ? NUON_BUTTON_NUON : 0;  // Share -> NUON/Z
  nuon_buttons |= (buttons & JP_BUTTON_DD) ? NUON_BUTTON_DOWN : 0;  // Dpad Down -> D-DOWN
  nuon_buttons |= (buttons & JP_BUTTON_DL) ? NUON_BUTTON_LEFT : 0;  // Dpad Left -> D-LEFT
  nuon_buttons |= (buttons & JP_BUTTON_DU) ? NUON_BUTTON_UP : 0;  // Dpad Up -> D-UP
  nuon_buttons |= (buttons & JP_BUTTON_DR) ? NUON_BUTTON_RIGHT : 0;  // Dpad Right -> D-RIGHT
  // Skipping the two buttons represented by 0x0080 and 0x0040 in the new format
  nuon_buttons |= (buttons & JP_BUTTON_L1) ? NUON_BUTTON_L : 0;  // L1 -> L
  nuon_buttons |= (buttons & JP_BUTTON_R1) ? NUON_BUTTON_R : 0;  // R1 -> R
  nuon_buttons |= (buttons & JP_BUTTON_B3) ? NUON_BUTTON_B : 0;  // Square -> B
  nuon_buttons |= (buttons & JP_BUTTON_B4) ? NUON_BUTTON_C_LEFT : 0;  // Triangle -> C-LEFT
  nuon_buttons |= (buttons & JP_BUTTON_L2) ? NUON_BUTTON_C_UP : 0;  // L2 -> C-UP
  nuon_buttons |= (buttons & JP_BUTTON_R2) ? NUON_BUTTON_C_RIGHT : 0;  // R2 -> C-RIGHT

  return nuon_buttons;
}

uint8_t eparity(uint32_t data)
{
  uint32_t eparity;
  eparity = (data>>16)^data;
  eparity ^= (eparity>>8);
  eparity ^= (eparity>>4);
  eparity ^= (eparity>>2);
  eparity ^= (eparity>>1);
  return ((eparity)&0x1);
}

// generates data response packet with crc check bytes
uint32_t crc_data_packet(int32_t value, int8_t size)
{
  uint32_t packet = 0;
  uint16_t crc = 0;

  // calculate crc and place bytes into packet position
  for (int i=0; i<size; i++)
  {
    uint8_t byte_val = (((value>>((size-i-1)*8)) & 0xff));
    crc = (crc_calc(byte_val, crc) & 0xffff);
    packet |= (byte_val << ((3-i)*8));
  }

  // place crc check bytes in packet position
  packet |= (crc << ((2-size)*8));

  return (packet);
}

int crc_build_lut()
{
	int i,j,k;
	for (i=0; i<256; i++)
  {
		for(j=i<<8,k=0; k<8; k++)
    {
			j=(j&0x8000) ? (j<<1)^CRC16 : (j<<1); crc_lut[i]=j;
		}
	}
	return(0);
}

int crc_calc(unsigned char data, int crc)
{
	if (crc_lut[1]==0) crc_build_lut();
	return(((crc_lut[((crc>>8)^data)&0xff])^(crc<<8))&0xffff);
}

static void trigger_button_press(uint8_t pin)
{
  // Configure the button pin as output
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_OUT);

  // Set the button pin to low
  gpio_put(pin, 0);

  // Wait for a brief moment
  sleep_ms(100); // Wait for 100 milliseconds

  // Reconfigure the button pin as an input
  gpio_set_dir(pin, GPIO_IN);
}

void nuon_task()
{
  // Get input from router (Nuon uses MERGE mode, all inputs merged to player 0)
  const input_event_t* event = router_get_output(OUTPUT_TARGET_NUON, 0);
  if (!event) return;

  // Check IGR hotkeys (internal Nuon reset mod)
  hotkeys_check(event->buttons, 0);
}

//
// core1_task - inner-loop for the second core
void __not_in_flash_func(core1_task)(void)
{
  uint64_t packet = 0;
  uint16_t state = 0;
  uint8_t channel = 0;
  uint8_t id = 0;
  bool alive = false;
  bool tagged = false;
  bool branded = false;
  int requestsB = 0;

  while (1)
  {
    packet = 0;
    for (int i = 0; i < 2; ++i)
    {
      uint32_t rxdata = pio_sm_get_blocking(pio, sm2);
      packet = ((packet) << 32) | (rxdata & 0xFFFFFFFF);
    }

    // queue_try_add(&packet_queue, &packet);

    uint8_t dataA = ((packet>>17) & 0b11111111);
    uint8_t dataS = ((packet>>9) & 0b01111111);
    uint8_t dataC = ((packet>>1) & 0b01111111);
    uint8_t type0 = ((packet>>25) & 0b00000001);
    uint32_t word0 = 1;
    uint32_t word1 = 0;

    if ((dataA == 0xb1 && dataS == 0x00 && dataC == 0x00) || // RESET
        (alive && !playersCount) // USB controller disconnected
    ) {
      id = 0;
      alive = false;
      tagged = false;
      branded = false;
      state = 0;
      channel = 0;
    }

    // No response unless USB controller connected
    if (!playersCount) continue;

    if (dataA == 0x80) // ALIVE
    {
      word0 = 1;
      word1 = __rev(0b01);
      if (alive) word1 = __rev(((id & 0b01111111) << 1));
      else alive = true;

      pio_sm_put_blocking(pio1, sm1, word1);
      pio_sm_put_blocking(pio1, sm1, word0);
    }
    else if (dataA == 0x88 && dataS == 0x04 && dataC == 0x40) // ERROR
    {
      word0 = 1;
      word1 = 0;
      pio_sm_put_blocking(pio1, sm1, word1);
      pio_sm_put_blocking(pio1, sm1, word0);
    }
    else if (dataA == 0x90 && !branded) // MAGIC
    {
      word0 = 1;
      word1 = __rev(MAGIC);
      pio_sm_put_blocking(pio1, sm1, word1);
      pio_sm_put_blocking(pio1, sm1, word0);
    }
    else if (dataA == 0x94) // PROBE
    {
      word0 = 1; // default res from HPI controller
      word1 = __rev(0b10001011000000110000000000000000);

      //DEFCFG VERSION     TYPE      MFG TAGGED BRANDED    ID P
      //   0b1 0001011 00000011 00000000      0       0 00000 0
      word1 = ((DEFCFG  & 1)<<31) |
              ((VERSION & 0b01111111)<<24) |
              ((TYPE    & 0b11111111)<<16) |
              ((MFG     & 0b11111111)<<8) |
              (((tagged ? 1:0) & 1)<<7) |
              (((branded? 1:0) & 1)<<6) |
              ((id      & 0b00011111)<<1);
      word1 = __rev(word1 | eparity(word1));

      pio_sm_put_blocking(pio1, sm1, word1);
      pio_sm_put_blocking(pio1, sm1, word0);
    }
    else if (dataA == 0x27 && dataS == 0x01 && dataC == 0x00) // REQUEST (ADDRESS)
    {
      word0 = 1;
      word1 = 0;

      if (channel == ATOD_CHANNEL_MODE)
      {
        // word1 = __rev(0b11000100100000101001101100000000); // 68
        word1 = __rev(crc_data_packet(0b11110100, 1)); // send & recv?
      } else {
        // word1 = __rev(0b11000110000000101001010000000000); // 70
        word1 = __rev(crc_data_packet(0b11110110, 1)); // send & recv?
      }

      pio_sm_put_blocking(pio1, sm1, word1);
      pio_sm_put_blocking(pio1, sm1, word0);
    }
    else if (dataA == 0x84 && dataS == 0x04 && dataC == 0x40) // REQUEST (B)
    {
      word0 = 1;
      word1 = 0;

      // 
      if ((0b101001001100 >> requestsB) & 0b01)
      {
        word1 = __rev(0b10);
      }

      pio_sm_put_blocking(pio1, sm1, word1);
      pio_sm_put_blocking(pio1, sm1, word0);

      requestsB++;
      if (requestsB == 12) requestsB = 7;
    }
    else if (dataA == 0x34 && dataS == 0x01) // CHANNEL
    {
      channel = dataC;
    }
    else if (dataA == 0x32 && dataS == 0x02 && dataC == 0x00) // QUADX
    {
      word0 = 1;
      word1 = __rev(0b10000000100000110000001100000000); //0

      word1 = __rev(output_quad_x);
      // TODO: solve how to set unique values to first two bytes plus checksum

      pio_sm_put_blocking(pio1, sm1, word1);
      pio_sm_put_blocking(pio1, sm1, word0);
    }
    else if (dataA == 0x35 && dataS == 0x01 && dataC == 0x00) // ANALOG
    {
      word0 = 1;
      word1 = __rev(0b10000000100000110000001100000000); //0

      // ALL_BUTTONS: CTRLR_STDBUTTONS & CTRLR_DPAD & CTRLR_SHOULDER & CTRLR_EXTBUTTONS
      // <= 23 - 0x51f CTRLR_TWIST & CTRLR_THROTTLE & CTRLR_ANALOG1 & ALL_BUTTONS
      // 29-47 - 0x83f CTRLR_MOUSE & CTRLR_ANALOG1 & CTRLR_ANALOG2 & ALL_BUTTONS
      // 48-69 - 0x01f CTRLR_ANALOG1 & ALL_BUTTONS
      // 70-92 - 0x808 CTRLR_MOUSE & CTRLR_EXTBUTTONS
      // >= 93 - ERROR?

      switch (channel)
      {
      case ATOD_CHANNEL_NONE:
        word1 = __rev(device_mode); // device mode packet?
        break;
      // case ATOD_CHANNEL_MODE:
      //   word1 = __rev(0b10000000100000110000001100000000);
      //   break;
      case ATOD_CHANNEL_X1:
        word1 = __rev(output_analog_1x);
        break;
      case ATOD_CHANNEL_Y1:
        word1 = __rev(output_analog_1y);
        break;
      case ATOD_CHANNEL_X2:
        word1 = __rev(output_analog_2x);
        break;
      case ATOD_CHANNEL_Y2:
        word1 = __rev(output_analog_2y);
        break;
      default:
        break;
      }

      pio_sm_put_blocking(pio1, sm1, word1);
      pio_sm_put_blocking(pio1, sm1, word0);
    }
    else if (dataA == 0x25 && dataS == 0x01 && dataC == 0x00) // CONFIG
    {
      word0 = 1;
      word1 = __rev(device_config); // device config packet?

      pio_sm_put_blocking(pio1, sm1, word1);
      pio_sm_put_blocking(pio1, sm1, word0);
    }
    else if (dataA == 0x31 && dataS == 0x01 && dataC == 0x00) // {SWITCH[16:9]}
    {
      word0 = 1;
      word1 = __rev(device_switch); // extra device config?

      pio_sm_put_blocking(pio1, sm1, word1);
      pio_sm_put_blocking(pio1, sm1, word0);
    }
    else if (dataA == 0x30 && dataS == 0x02 && dataC == 0x00) // {SWITCH[8:1]}
    {
      word0 = 1;
      word1 = __rev(output_buttons_0);

      pio_sm_put_blocking(pio1, sm1, word1);
      pio_sm_put_blocking(pio1, sm1, word0);
    }
    else if (dataA == 0x99 && dataS == 0x01) // STATE
    {
      switch (type0)
      {
      case PACKET_TYPE_READ:
        word0 = 1;
        word1 = __rev(0b11000000000000101000000000000000);

        if (((state >> 8) & 0xff) == 0x41 && (state & 0xff) == 0x51)
        {
          word1 = __rev(0b11010001000000101110011000000000);
        }
        pio_sm_put_blocking(pio1, sm1, word1);
        pio_sm_put_blocking(pio1, sm1, word0);
        break;
      // case PACKET_TYPE_WRITE:
      default:
        state = ((state) << 8) | (dataC & 0xff);
        break;
      }
    }
    else if (dataA == 0xb4 && dataS == 0x00) // BRAND
    {
      id = dataC;
      branded = true;
    }
  }
}

//
// update_output - updates output words with button/analog polyface packet
//
void __not_in_flash_func(update_output)(void)
{
  // Get input from router (Nuon uses MERGE mode, all inputs merged to player 0)
  const input_event_t* event = router_get_output(OUTPUT_TARGET_NUON, 0);
  if (!event || playersCount == 0) return;

  // Apply profile remapping
  const profile_t* profile = profile_get_active(OUTPUT_TARGET_NUON);
  profile_output_t mapped;
  profile_apply(profile, event->buttons,
                event->analog[ANALOG_LX], event->analog[ANALOG_LY],
                event->analog[ANALOG_RX], event->analog[ANALOG_RY],
                event->analog[ANALOG_L2], event->analog[ANALOG_R2],
                &mapped);

  // Map USBR buttons to Nuon button format
  int32_t nuon_buttons = map_nuon_buttons(mapped.buttons);

  output_buttons_0 = crc_data_packet(nuon_buttons, 2);
  output_analog_1x = crc_data_packet(mapped.left_x, 1);
  output_analog_1y = crc_data_packet(255 - mapped.left_y, 1);   // Invert Y: HID uses 0=up
  output_analog_2x = crc_data_packet(mapped.right_x, 1);
  output_analog_2y = crc_data_packet(255 - mapped.right_y, 1);  // Invert Y: HID uses 0=up

  // TODO Phase 5: Re-implement spinner/mouse wheel support
  // output_quad_x was accumulated in post_input_event() - need console-local accumulator
  output_quad_x    = crc_data_packet(0, 1);  // Disabled for now

  codes_task_for_output(OUTPUT_TARGET_NUON);

}

// post_input_event removed - replaced by router architecture
// Input flow: USB drivers → router_submit_input() → router → router_get_output() → update_output()

// ============================================================================
// PROFILE SYSTEM (Delegates to core profile service)
// ============================================================================

static uint8_t nuon_get_profile_count(void) {
    return profile_get_count(OUTPUT_TARGET_NUON);
}

static uint8_t nuon_get_active_profile(void) {
    return profile_get_active_index(OUTPUT_TARGET_NUON);
}

static void nuon_set_active_profile(uint8_t index) {
    profile_set_active(OUTPUT_TARGET_NUON, index);
}

static const char* nuon_get_profile_name(uint8_t index) {
    return profile_get_name(OUTPUT_TARGET_NUON, index);
}

// ============================================================================
// OUTPUT INTERFACE
// ============================================================================

#include "core/output_interface.h"

const OutputInterface nuon_output_interface = {
    .name = "Nuon",
    .target = OUTPUT_TARGET_NUON,
    .init = nuon_init,
    .core1_task = core1_task,
    .task = nuon_task,  // Nuon needs periodic soft reset task
    .get_rumble = NULL,
    .get_player_led = NULL,
    // Profile system
    .get_profile_count = nuon_get_profile_count,
    .get_active_profile = nuon_get_active_profile,
    .set_active_profile = nuon_set_active_profile,
    .get_profile_name = nuon_get_profile_name,
    .get_trigger_threshold = NULL,
};
