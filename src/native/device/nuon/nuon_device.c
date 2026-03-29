// nuon.c

#include "nuon_device.h"
#ifdef BTSTACK_USE_CYW43
#include "pico/cyw43_arch.h"
#endif
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

// Per-device polyface state for multi-device support
#define MAX_POLYFACE_DEVICES 4
typedef struct {
    uint8_t bit_slot;    // ALIVE bit slot (0-30)
    uint8_t id;          // assigned by console via BRAND (0 = unbranded)
    uint8_t channel;     // current ATOD channel
    uint16_t state;      // STATE register
    bool active;         // has a paired BT controller
    bool alive;          // responded to ALIVE
    bool tagged;         // TAG'd by console
    bool branded;        // BRAND'd by console
    bool focused;        // currently has FOCUS
    int requestsB;
} polyface_device_t;

static polyface_device_t pf_devices[MAX_POLYFACE_DEVICES];
static int pf_focused_idx = -1;  // which device currently has focus (-1 = none)

// Core 1 → Core 0 diagnostic
volatile uint32_t pf_diag_count = 0;

// Send a polyface response: wait for turnaround gap, then push data to send SM.
// Uses gpio_get() for clock edge counting — reads via SIO (single-cycle, per-core)
// instead of pio_sm_exec which goes through APB and contends with CYW43 DMA.
static void __no_inline_not_in_flash_func(polyface_respond)(uint32_t word1, uint32_t word0) {
    (void)word0;

    // Wait 30 clock edges via SIO gpio_get (zero APB contention).
    // This turnaround delay prevents our response from colliding with
    // the console's command tail on the shared data line.
    for (int d = 0; d < 30; d++) {
        while (!gpio_get(CLKIN_PIN)) tight_loop_contents();
        while (gpio_get(CLKIN_PIN)) tight_loop_contents();
    }

    pio_sm_put_blocking(pio, sm1, word1);
}

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

  pio = pio0;

  // Both polyface programs on PIO0 (13 + 18 = 31 ≤ 32).
  // PIO1 is left ENTIRELY for CYW43 SPI — no sharing, no interference.
  uint offset2 = pio_add_program(pio, &polyface_read_program);
  sm2 = pio_claim_unused_sm(pio, true);
  polyface_read_program_init(pio, sm2, offset2, DATAIO_PIN);

  uint offset1 = pio_add_program(pio, &polyface_send_program);
  sm1 = pio_claim_unused_sm(pio, true);
  polyface_send_program_init(pio, sm1, offset1, DATAIO_PIN);

  // Claim remaining PIO0 SMs (CYW43 can't fit on PIO0 — 32/32 instructions)
  for (int i = 0; i < 4; i++) {
    if (!pio_sm_is_claimed(pio, i)) pio_sm_claim(pio, i);
  }

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
uint32_t __no_inline_not_in_flash_func(map_nuon_buttons)(uint32_t buttons)
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

uint8_t __no_inline_not_in_flash_func(eparity)(uint32_t data)
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
uint32_t __no_inline_not_in_flash_func(crc_data_packet)(int32_t value, int8_t size)
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

int __no_inline_not_in_flash_func(crc_calc)(unsigned char data, int crc)
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
  // Periodic diagnostic: verify PIO state and clock signal
  static uint32_t diag_last = 0;
  uint32_t now = to_ms_since_boot(get_absolute_time());
  if (now - diag_last >= 2000) {
    diag_last = now;
    // Quick clock edge count (5ms sample)
    int edges = 0;
    bool last = gpio_get(CLKIN_PIN);
    absolute_time_t t_end = make_timeout_time_ms(5);
    while (!time_reached(t_end)) {
      bool c = gpio_get(CLKIN_PIN);
      if (c != last) { edges++; last = c; }
    }
    // Check PIO0 and PIO1 instruction usage + SM claims
    printf("[nuon] pf=%lu clk=%d pl=%d\n",
           (unsigned long)pf_diag_count, edges, playersCount);
  }

  update_output();

  // Get input from router for hotkey checking
  const input_event_t* event = router_get_output(OUTPUT_TARGET_NUON, 0);
  if (!event) return;

  // Check IGR hotkeys (internal Nuon reset mod)
  hotkeys_check(event->buttons, 0);
}

//
// core1_task - inner-loop for the second core
void __not_in_flash_func(core1_task)(void)
{
  // Give Core 1 high bus priority so CYW43's DMA wait tight-loop
  // on Core 0 doesn't starve Core 1's PIO register access.
  // BUSCTRL base = 0x40030000, BUS_PRIORITY at offset 0x00
  // bit 4 = PROC1 high priority
  *(volatile uint32_t *)0x40030000 = (1u << 4);

  uint64_t packet = 0;

  // Initialize device slots
  for (int d = 0; d < MAX_POLYFACE_DEVICES; d++) {
    pf_devices[d] = (polyface_device_t){
      .bit_slot = d, .id = 0, .channel = 0, .state = 0,
      .active = false, .alive = false, .tagged = false,
      .branded = false, .focused = false, .requestsB = 0
    };
  }

  while (1)
  {
    packet = 0;
    for (int i = 0; i < 2; ++i)
    {
      while (pio_sm_is_rx_fifo_empty(pio, sm2)) {
        tight_loop_contents();
      }
      uint32_t rxdata = pio_sm_get(pio, sm2);
      packet = ((packet) << 32) | (rxdata & 0xFFFFFFFF);
    }

    uint32_t pf_w0 = (uint32_t)(packet >> 32);
    uint32_t pf_w1 = (uint32_t)(packet & 0xFFFFFFFF);

    // Check if this is a DEVICE response (from physical controller) or HOST command
    static uint8_t sniff_last_cmd = 0;
    static uint8_t sniff_last_channel = 0;
    static bool sniff_detected = false;

    if ((pf_w0 & 1) == 0) {
        // Device response — physical controller responding to console.

        if (sniff_last_cmd == 0x80 && pf_w1 != 0 && !sniff_detected) {
            sniff_detected = true;
            // printf("[sniff] controller detected\n");
        }

        // Store sniffed data (no printf here — Core 1 can't block on UART)
        static uint16_t sniff_buttons = 0;
        static uint8_t sniff_analog[6] = {128,128,128,128,128,128};

        if (sniff_last_cmd == 0x30) {
            sniff_buttons = (pf_w1 >> 16) & 0xFFFF;
        } else if (sniff_last_cmd == 0x35) {
            uint8_t val = (pf_w1 >> 24) & 0xFF;
            if (sniff_last_channel >= 2 && sniff_last_channel <= 5)
                sniff_analog[sniff_last_channel] = val;
        }

        // // Throttled debug print — max once per 500ms
        // {
        //     static uint32_t last_print_ms = 0;
        //     uint32_t now = to_ms_since_boot(get_absolute_time());
        //     if (sniff_detected && now - last_print_ms >= 500) {
        //         last_print_ms = now;
        //         printf("[sniff] btn=%04X lx=%d ly=%d rx=%d ry=%d\n",
        //                sniff_buttons,
        //                sniff_analog[2], sniff_analog[3],
        //                sniff_analog[4], sniff_analog[5]);
        //     }
        // }
        continue;  // don't process device responses as commands
    }

    // HOST command from console — extract fields as before
    uint8_t dataA = ((packet>>17) & 0b11111111);
    uint8_t dataS = ((packet>>9) & 0b01111111);
    uint8_t dataC = ((packet>>1) & 0b01111111);
    uint8_t type0 = ((packet>>25) & 0b00000001);
    uint32_t word0 = 1;
    uint32_t word1 = 0;

    // Track console command for sniffing
    sniff_last_cmd = dataA;
    if (dataA == 0x34) sniff_last_channel = dataC;  // CHANNEL sets axis

    pf_diag_count++;

    // Update active device count based on paired BT controllers
    {
        int count = *(volatile int*)&playersCount;
        static int prev_count = 0;

        // Activate new device slots when BT controllers pair
        if (count > prev_count) {
            for (int d = prev_count; d < count && d < MAX_POLYFACE_DEVICES; d++) {
                // Reset device state for fresh detection
                int slot = sniff_detected ? d + 1 : d;  // offset if physical controller uses slot 0
                if (slot >= MAX_POLYFACE_DEVICES) break;
                pf_devices[slot].bit_slot = slot;
                pf_devices[slot].active = true;
                pf_devices[slot].alive = false;
                pf_devices[slot].tagged = false;
                pf_devices[slot].branded = false;
                pf_devices[slot].focused = false;
                pf_devices[slot].id = 0;
                pf_devices[slot].channel = 0;
                pf_devices[slot].state = 0;
                pf_devices[slot].requestsB = 0;
            }
        }
        // Deactivate slots when BT controllers disconnect
        if (count < prev_count) {
            for (int d = count; d < prev_count && d < MAX_POLYFACE_DEVICES; d++) {
                int slot = sniff_detected ? d + 1 : d;
                if (slot < MAX_POLYFACE_DEVICES)
                    pf_devices[slot].active = false;
            }
        }
        prev_count = count;

        // If no active devices, skip command processing (stay transparent)
        bool any_active = false;
        for (int d = 0; d < MAX_POLYFACE_DEVICES; d++)
            if (pf_devices[d].active) { any_active = true; break; }
        if (!any_active) continue;
    }

    if (dataA == 0xb1 && dataS == 0x00 && dataC == 0x00) // RESET
    {
      for (int d = 0; d < MAX_POLYFACE_DEVICES; d++) {
        pf_devices[d].id = 0;
        pf_devices[d].alive = false;
        pf_devices[d].tagged = false;
        pf_devices[d].branded = false;
        pf_devices[d].focused = false;
        pf_devices[d].state = 0;
        pf_devices[d].channel = 0;
      }
      pf_focused_idx = -1;
    }

    // FOCUS/BLUR (A=0xB0)
    if (dataA == 0xb0 && dataS == 0x00) {
      if (dataC == 0x01) {        // FOCUS
        // Unfocus all, then focus the tagged or first active device
        for (int d = 0; d < MAX_POLYFACE_DEVICES; d++)
          pf_devices[d].focused = false;
        pf_focused_idx = -1;
        for (int d = 0; d < MAX_POLYFACE_DEVICES; d++) {
          if (!pf_devices[d].active) continue;
          // Tagged device gets priority, otherwise first active
          if (pf_devices[d].tagged) {
            pf_devices[d].focused = true;
            pf_devices[d].tagged = false;
            pf_focused_idx = d;
            break;
          }
        }
        // If no tagged device, focus first active
        if (pf_focused_idx < 0) {
          for (int d = 0; d < MAX_POLYFACE_DEVICES; d++) {
            if (pf_devices[d].active) {
              pf_devices[d].focused = true;
              pf_focused_idx = d;
              break;
            }
          }
        }
      } else if (dataC == 0x02) { // BLUR
        for (int d = 0; d < MAX_POLYFACE_DEVICES; d++) {
          if (pf_devices[d].tagged) {
            // Tagged device converts BLUR→FOCUS
            for (int j = 0; j < MAX_POLYFACE_DEVICES; j++)
              pf_devices[j].focused = false;
            pf_devices[d].focused = true;
            pf_devices[d].tagged = false;
            pf_focused_idx = d;
            break;
          } else if (pf_devices[d].focused) {
            pf_devices[d].focused = false;
            pf_focused_idx = -1;
          }
        }
      }
    }

    // TAG (A=0xB2) — tag first unbranded active device
    if (dataA == 0xb2 && dataS == 0x00 && dataC == 0x00) {
      for (int d = 0; d < MAX_POLYFACE_DEVICES; d++) {
        if (pf_devices[d].active && !pf_devices[d].branded && !pf_devices[d].tagged) {
          pf_devices[d].tagged = true;
          break;
        }
      }
    }

    // Use focused device, or first active as fallback (ungated mode)
    polyface_device_t *fdev = (pf_focused_idx >= 0) ? &pf_devices[pf_focused_idx] : &pf_devices[0];

    if (dataA == 0x80) // ALIVE — BROADCAST, all active devices respond via bit slots
    {
      word0 = 1;
      uint32_t alive_bits = 0;
      for (int d = 0; d < MAX_POLYFACE_DEVICES; d++) {
        if (!pf_devices[d].active) continue;
        if (pf_devices[d].alive) {
          // Branded: respond in assigned ID's bit slot
          if (pf_devices[d].branded)
            alive_bits |= ((pf_devices[d].id & 0x7F) << 1);
        } else {
          // First ALIVE: respond in our bit slot
          alive_bits |= (1 << pf_devices[d].bit_slot);  // bit slot N → bit position N
          pf_devices[d].alive = true;
        }
      }
      word1 = __rev(alive_bits);
      polyface_respond(word1, word0);
    }
    else if (dataA == 0x88 && dataS == 0x04 && dataC == 0x40) // ERROR — BROADCAST
    {
      word0 = 1;
      word1 = 0;
      polyface_respond(word1, word0);
    }
    else if (dataA == 0x90 && !fdev->branded) // MAGIC
    {
      word0 = 1;
      word1 = __rev(MAGIC);
      polyface_respond(word1, word0);
    }
    else if (dataA == 0x94 ) // PROBE — FOCUS addressed
    {
      word0 = 1;
      word1 = ((DEFCFG  & 1)<<31) |
              ((VERSION & 0b01111111)<<24) |
              ((TYPE    & 0b11111111)<<16) |
              ((MFG     & 0b11111111)<<8) |
              (((fdev->tagged ? 1:0) & 1)<<7) |
              (((fdev->branded? 1:0) & 1)<<6) |
              ((fdev->id      & 0b00011111)<<1);
      word1 = __rev(word1 | eparity(word1));
      polyface_respond(word1, word0);
    }
    else if (dataA == 0x27 && dataS == 0x01 && dataC == 0x00 ) // REQUEST (ADDRESS)
    {
      word0 = 1;
      word1 = 0;

      if (fdev->channel == ATOD_CHANNEL_MODE)
      {
        // word1 = __rev(0b11000100100000101001101100000000); // 68
        word1 = __rev(crc_data_packet(0b11110100, 1)); // send & recv?
      } else {
        // word1 = __rev(0b11000110000000101001010000000000); // 70
        word1 = __rev(crc_data_packet(0b11110110, 1)); // send & recv?
      }

      polyface_respond(word1, word0);
    }
    else if (dataA == 0x84 && dataS == 0x04 && dataC == 0x40) // REQUEST — BROADCAST
    {
      // Use focused device if available, otherwise first active
      polyface_device_t *rdev = fdev;
      if (!rdev) {
        for (int d = 0; d < MAX_POLYFACE_DEVICES; d++)
          if (pf_devices[d].active) { rdev = &pf_devices[d]; break; }
      }
      word0 = 1;
      word1 = 0;

      if (rdev && (0b101001001100 >> rdev->requestsB) & 0b01)
      {
        word1 = __rev(0b10);
      }

      polyface_respond(word1, word0);

      if (rdev) {
        rdev->requestsB++;
        if (rdev->requestsB == 12) rdev->requestsB = 7;
      }
    }
    else if (dataA == 0x34 && dataS == 0x01 ) // CHANNEL
    {
      fdev->channel = dataC;
    }
    else if (dataA == 0x32 && dataS == 0x02 && dataC == 0x00 ) // QUADX
    {
      word0 = 1;
      word1 = __rev(0b10000000100000110000001100000000); //0

      word1 = __rev(output_quad_x);
      // TODO: solve how to set unique values to first two bytes plus checksum

      polyface_respond(word1, word0);
    }
    else if (dataA == 0x35 && dataS == 0x01 && dataC == 0x00 ) // ANALOG
    {
      word0 = 1;
      word1 = __rev(0b10000000100000110000001100000000); //0

      switch (fdev->channel)
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

      polyface_respond(word1, word0);
    }
    else if (dataA == 0x25 && dataS == 0x01 && dataC == 0x00 ) // CONFIG
    {
      word0 = 1;
      word1 = __rev(device_config); // device config packet?

      polyface_respond(word1, word0);
    }
    else if (dataA == 0x31 && dataS == 0x01 && dataC == 0x00 ) // {SWITCH[16:9]}
    {
      word0 = 1;
      word1 = __rev(device_switch); // extra device config?

      polyface_respond(word1, word0);
    }
    else if (dataA == 0x30 && dataS == 0x02 && dataC == 0x00 ) // {SWITCH[8:1]}
    {
      word0 = 1;
      word1 = __rev(output_buttons_0);

      polyface_respond(word1, word0);
    }
    else if (dataA == 0x99 && dataS == 0x01 ) // STATE
    {
      switch (type0)
      {
      case PACKET_TYPE_READ:
        word0 = 1;
        word1 = __rev(0b11000000000000101000000000000000);

        if (((fdev->state >> 8) & 0xff) == 0x41 && (fdev->state & 0xff) == 0x51)
        {
          word1 = __rev(0b11010001000000101110011000000000);
        }
        polyface_respond(word1, word0);
        break;
      default:
        fdev->state = ((fdev->state) << 8) | (dataC & 0xff);
        break;
      }
    }
    else if (dataA == 0xb4 && dataS == 0x00 ) // BRAND — FOCUS addressed
    {
      fdev->id = dataC;
      fdev->branded = true;
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
                event->analog[ANALOG_RZ],
                &mapped);

  // Map USBR buttons to Nuon button format
  int32_t nuon_buttons = map_nuon_buttons(mapped.buttons);

  output_buttons_0 = crc_data_packet(nuon_buttons, 2);
  output_analog_1x = crc_data_packet(mapped.left_x, 1);
  output_analog_1y = crc_data_packet(mapped.left_y, 1);
  output_analog_2x = crc_data_packet(mapped.right_x, 1);
  output_analog_2y = crc_data_packet(mapped.right_y, 1);

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
