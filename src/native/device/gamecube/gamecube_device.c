// gamecube_device.c - GameCube Output Device
//
// Outputs controller data to GameCube via joybus protocol.
// Uses the universal profile system for button remapping.

#include "gamecube_device.h"
#include <stdio.h>
#include "gamecube_buttons.h"
#include "joybus.pio.h"
#include "GamecubeConsole.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "tusb.h"
#include "core/services/storage/flash.h"
#include "core/services/profiles/profile.h"
#include "core/services/players/manager.h"
#include "core/services/codes/codes.h"
#include "core/router/router.h"
#include "platform/platform.h"

// Declaration of global variables
GamecubeConsole gc;
gc_report_t gc_report;
PIO pio = pio0;

// Config mode flag - set by app when GC 3.3V not detected
bool gc_config_mode = false;

// GameCube-specific state for USB device output
static uint8_t gc_rumble = 0;
static uint8_t gc_kb_led = 0;

static uint8_t gc_get_rumble(void) { return gc_rumble; }
static uint8_t gc_get_kb_led(void) { return gc_kb_led; }

// ============================================================================
// PROFILE SYSTEM ACCESSORS (for OutputInterface)
// ============================================================================

static uint8_t gc_get_player_count_for_profile(void) {
    return router_get_player_count(OUTPUT_TARGET_GAMECUBE);
}

static uint8_t gc_get_profile_count(void) {
    return profile_get_count(OUTPUT_TARGET_GAMECUBE);
}

static uint8_t gc_get_active_profile_index(void) {
    return profile_get_active_index(OUTPUT_TARGET_GAMECUBE);
}

static void gc_set_active_profile(uint8_t index) {
    profile_set_active(OUTPUT_TARGET_GAMECUBE, index);
}

static const char* gc_get_profile_name(uint8_t index) {
    return profile_get_name(OUTPUT_TARGET_GAMECUBE, index);
}

static uint8_t gc_get_trigger_threshold(void) {
    const profile_t* profile = profile_get_active(OUTPUT_TARGET_GAMECUBE);
    if (!profile || !profile->adaptive_triggers) {
        return 0;  // Disabled or no profile
    }
    return profile->l2_threshold;
}

// ============================================================================
// CONSOLE-LOCAL STATE
// ============================================================================

static struct {
    int button_mode;  // BUTTON_MODE_KB or BUTTON_MODE_3
} gc_state = {
    .button_mode = BUTTON_MODE_3  // Default to gamepad mode
};

// Externs for functions in GamecubeConsole.c (some not yet in header)
extern void GamecubeConsole_init(GamecubeConsole* console, uint pin, PIO pio, int sm, int offset);
extern bool GamecubeConsole_WaitForPoll(GamecubeConsole* console);
extern void GamecubeConsole_SendReport(GamecubeConsole* console, gc_report_t *report);
extern void GamecubeConsole_SetMode(GamecubeConsole* console, GamecubeMode mode);

uint8_t hid_to_gc_key[256] = {[0 ... 255] = GC_KEY_NOT_FOUND};
uint8_t gc_last_rumble = 0;
uint8_t gc_kb_counter = 0;

// ============================================================================
// KEYBOARD MODE ACTIVATION CHORD
// ============================================================================
// Third activation binding, for keyboards that have neither Scroll Lock nor an
// F13-F24 row — 65% boards and smaller (discussion #220).
//
// It has to be a chord rather than a fourth single key. gc_kb_key_lookup_init()
// below gives the GameCube keyboard its own keycode for 77 of the 112 HID codes
// in 0x04-0x73, and a toggle bound to one of those would switch the mode *and*
// type the character into the game. The 35 codes it leaves free are Scroll Lock,
// Pause, Num Lock, the numeric keypad, F13-F24 and EUROPE_1/2 — i.e. exactly the
// clusters a 65% board drops. (EUROPE_2 is free only because the GameCube
// keyboard is US/JP; it is a real typing key on every ISO board, so it is not a
// candidate either.) There is no single key that is both present on a small
// board and free of a GameCube keycode, which is why Scroll Lock and F14 were
// picked in the first place.
//
// To rebind, change these three defines. The modifier side does not matter:
// process_hid_keyboard() folds right Ctrl/Alt onto the LEFT keycodes before the
// event reaches us, so Ctrl and Alt on either side of the space bar both work.
#define GC_KB_TOGGLE_MOD1  HID_KEY_CONTROL_LEFT
#define GC_KB_TOGGLE_MOD2  HID_KEY_ALT_LEFT
#define GC_KB_TOGGLE_KEY   HID_KEY_K

// event->keys is a lossy 32-bit packing of the USB HID keyboard report: up to
// three keycodes, with any held modifiers shifted in at the low end (see
// process_hid_keyboard in hid_keyboard.c). A trigger therefore has to be matched
// as set membership over the four bytes, not as equality against the whole word.
// Comparing the word — as this did until now — means the key only toggles when
// pressed completely alone: holding any modifier shifts the keycode up a byte
// and the compare silently fails.
//
// No false matches are possible: modifier keycodes (0xE0-0xE7) never appear in
// the report's keycode[] array, and an unused byte is 0 while every trigger
// constant is non-zero.
static inline bool gc_keys_contain(uint32_t keys, uint8_t code)
{
  return (uint8_t)( keys        & 0xFF) == code ||
         (uint8_t)((keys >>  8) & 0xFF) == code ||
         (uint8_t)((keys >> 16) & 0xFF) == code ||
         (uint8_t)((keys >> 24) & 0xFF) == code;
}

// Helper function to scale analog values relative to center (128)
// Clamps to 1-255 range - some GameCube games reject 0 as invalid
static inline uint8_t scale_toward_center(uint8_t val, float scale, uint8_t center)
{
  int16_t rel = (int16_t)val - (int16_t)center;
  int16_t scaled = (int16_t)(rel * scale);
  int16_t result = scaled + (int16_t)center;
  if (result < 1) result = 1;
  if (result > 255) result = 255;
  return (uint8_t)result;
}

// init hid key to gc key lookup table
void gc_kb_key_lookup_init()
{
  hid_to_gc_key[HID_KEY_A] = GC_KEY_A;
  hid_to_gc_key[HID_KEY_B] = GC_KEY_B;
  hid_to_gc_key[HID_KEY_C] = GC_KEY_C;
  hid_to_gc_key[HID_KEY_D] = GC_KEY_D;
  hid_to_gc_key[HID_KEY_E] = GC_KEY_E;
  hid_to_gc_key[HID_KEY_F] = GC_KEY_F;
  hid_to_gc_key[HID_KEY_G] = GC_KEY_G;
  hid_to_gc_key[HID_KEY_H] = GC_KEY_H;
  hid_to_gc_key[HID_KEY_I] = GC_KEY_I;
  hid_to_gc_key[HID_KEY_J] = GC_KEY_J;
  hid_to_gc_key[HID_KEY_K] = GC_KEY_K;
  hid_to_gc_key[HID_KEY_L] = GC_KEY_L;
  hid_to_gc_key[HID_KEY_M] = GC_KEY_M;
  hid_to_gc_key[HID_KEY_N] = GC_KEY_N;
  hid_to_gc_key[HID_KEY_O] = GC_KEY_O;
  hid_to_gc_key[HID_KEY_P] = GC_KEY_P;
  hid_to_gc_key[HID_KEY_Q] = GC_KEY_Q;
  hid_to_gc_key[HID_KEY_R] = GC_KEY_R;
  hid_to_gc_key[HID_KEY_S] = GC_KEY_S;
  hid_to_gc_key[HID_KEY_T] = GC_KEY_T;
  hid_to_gc_key[HID_KEY_U] = GC_KEY_U;
  hid_to_gc_key[HID_KEY_V] = GC_KEY_V;
  hid_to_gc_key[HID_KEY_W] = GC_KEY_W;
  hid_to_gc_key[HID_KEY_X] = GC_KEY_X;
  hid_to_gc_key[HID_KEY_Y] = GC_KEY_Y;
  hid_to_gc_key[HID_KEY_Z] = GC_KEY_Z;
  hid_to_gc_key[HID_KEY_1] = GC_KEY_1;
  hid_to_gc_key[HID_KEY_2] = GC_KEY_2;
  hid_to_gc_key[HID_KEY_3] = GC_KEY_3;
  hid_to_gc_key[HID_KEY_4] = GC_KEY_4;
  hid_to_gc_key[HID_KEY_5] = GC_KEY_5;
  hid_to_gc_key[HID_KEY_6] = GC_KEY_6;
  hid_to_gc_key[HID_KEY_7] = GC_KEY_7;
  hid_to_gc_key[HID_KEY_8] = GC_KEY_8;
  hid_to_gc_key[HID_KEY_9] = GC_KEY_9;
  hid_to_gc_key[HID_KEY_0] = GC_KEY_0;
  hid_to_gc_key[HID_KEY_MINUS] = GC_KEY_MINUS;
  hid_to_gc_key[HID_KEY_EQUAL] = GC_KEY_CARET;
  hid_to_gc_key[HID_KEY_GRAVE] = GC_KEY_YEN;
  hid_to_gc_key[HID_KEY_PRINT_SCREEN] = GC_KEY_AT;
  hid_to_gc_key[HID_KEY_BRACKET_LEFT] = GC_KEY_LEFTBRACKET;
  hid_to_gc_key[HID_KEY_SEMICOLON] = GC_KEY_SEMICOLON;
  hid_to_gc_key[HID_KEY_APOSTROPHE] = GC_KEY_COLON;
  hid_to_gc_key[HID_KEY_BRACKET_RIGHT] = GC_KEY_RIGHTBRACKET;
  hid_to_gc_key[HID_KEY_COMMA] = GC_KEY_COMMA;
  hid_to_gc_key[HID_KEY_PERIOD] = GC_KEY_PERIOD;
  hid_to_gc_key[HID_KEY_SLASH] = GC_KEY_SLASH;
  hid_to_gc_key[HID_KEY_BACKSLASH] = GC_KEY_BACKSLASH;
  hid_to_gc_key[HID_KEY_F1] = GC_KEY_F1;
  hid_to_gc_key[HID_KEY_F2] = GC_KEY_F2;
  hid_to_gc_key[HID_KEY_F3] = GC_KEY_F3;
  hid_to_gc_key[HID_KEY_F4] = GC_KEY_F4;
  hid_to_gc_key[HID_KEY_F5] = GC_KEY_F5;
  hid_to_gc_key[HID_KEY_F6] = GC_KEY_F6;
  hid_to_gc_key[HID_KEY_F7] = GC_KEY_F7;
  hid_to_gc_key[HID_KEY_F8] = GC_KEY_F8;
  hid_to_gc_key[HID_KEY_F9] = GC_KEY_F9;
  hid_to_gc_key[HID_KEY_F10] = GC_KEY_F10;
  hid_to_gc_key[HID_KEY_F11] = GC_KEY_F11;
  hid_to_gc_key[HID_KEY_F12] = GC_KEY_F12;
  hid_to_gc_key[HID_KEY_ESCAPE] = GC_KEY_ESC;
  hid_to_gc_key[HID_KEY_INSERT] = GC_KEY_INSERT;
  hid_to_gc_key[HID_KEY_DELETE] = GC_KEY_DELETE;
  hid_to_gc_key[HID_KEY_GRAVE] = GC_KEY_GRAVE;
  hid_to_gc_key[HID_KEY_BACKSPACE] = GC_KEY_BACKSPACE;
  hid_to_gc_key[HID_KEY_TAB] = GC_KEY_TAB;
  hid_to_gc_key[HID_KEY_CAPS_LOCK] = GC_KEY_CAPSLOCK;
  hid_to_gc_key[HID_KEY_SHIFT_LEFT] = GC_KEY_LEFTSHIFT;
  hid_to_gc_key[HID_KEY_SHIFT_RIGHT] = GC_KEY_RIGHTSHIFT;
  hid_to_gc_key[HID_KEY_CONTROL_LEFT] = GC_KEY_LEFTCTRL;
  hid_to_gc_key[HID_KEY_ALT_LEFT] = GC_KEY_LEFTALT;
  hid_to_gc_key[HID_KEY_GUI_LEFT] = GC_KEY_LEFTUNK1;
  hid_to_gc_key[HID_KEY_SPACE] = GC_KEY_SPACE;
  hid_to_gc_key[HID_KEY_GUI_RIGHT] = GC_KEY_RIGHTUNK1;
  hid_to_gc_key[HID_KEY_APPLICATION] = GC_KEY_RIGHTUNK2;
  hid_to_gc_key[HID_KEY_ARROW_LEFT] = GC_KEY_LEFT;
  hid_to_gc_key[HID_KEY_ARROW_DOWN] = GC_KEY_DOWN;
  hid_to_gc_key[HID_KEY_ARROW_UP] = GC_KEY_UP;
  hid_to_gc_key[HID_KEY_ARROW_RIGHT] = GC_KEY_RIGHT;
  hid_to_gc_key[HID_KEY_ENTER] = GC_KEY_ENTER;
  hid_to_gc_key[HID_KEY_HOME] = GC_KEY_HOME;
  hid_to_gc_key[HID_KEY_END] = GC_KEY_END;
  hid_to_gc_key[HID_KEY_PAGE_DOWN] = GC_KEY_PAGEDOWN;
  hid_to_gc_key[HID_KEY_PAGE_UP] = GC_KEY_PAGEUP;
}

// init for gamecube communication
void ngc_init()
{
  // over clock CPU for correct timing with GC
  set_sys_clock_khz(130000, true);

  #ifdef UART_TX_PIN
  // Configure custom UART pins (KB2040: 12=TX, 13=RX)
  gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
  #endif

  // corrects UART serial output after overclock
  stdio_init_all();

  // Initialize flash settings system
  flash_init();

  // Profile system is initialized by app - just set up callbacks
  profile_set_player_count_callback(gc_get_player_count_for_profile);

  #ifdef CONFIG_NGC
  // KB2040-specific hardware: ground shield GPIOs, BOOTSEL button, 3V3 detect
  gpio_init(SHIELD_PIN_L);
  gpio_set_dir(SHIELD_PIN_L, GPIO_OUT);
  gpio_init(SHIELD_PIN_L+1);
  gpio_set_dir(SHIELD_PIN_L+1, GPIO_OUT);
  gpio_init(SHIELD_PIN_R);
  gpio_set_dir(SHIELD_PIN_R, GPIO_OUT);
  gpio_init(SHIELD_PIN_R+1);
  gpio_set_dir(SHIELD_PIN_R+1, GPIO_OUT);

  gpio_put(SHIELD_PIN_L, 0);
  gpio_put(SHIELD_PIN_L+1, 0);
  gpio_put(SHIELD_PIN_R, 0);
  gpio_put(SHIELD_PIN_R+1, 0);

  gpio_init(BOOTSEL_PIN);
  gpio_set_dir(BOOTSEL_PIN, GPIO_IN);
  gpio_pull_up(BOOTSEL_PIN);
  #endif

  int sm = -1;
  int offset = -1;
  gc_kb_key_lookup_init();

  // Allow runtime override of GC_DATA pin from flash settings (web config).
  // 0 = "use compile-time default" (also matches blank/legacy reserved bytes).
  uint8_t data_pin = GC_DATA_PIN;
  flash_t* settings = flash_get_settings();
  if (settings && settings->joybus_data_pin > 0 && settings->joybus_data_pin <= 28) {
    data_pin = settings->joybus_data_pin;
  }
  printf("[gc] joybus DATA pin: GPIO %d%s\n", data_pin,
         (data_pin != GC_DATA_PIN) ? " (override)" : "");
  GamecubeConsole_init(&gc, data_pin, pio, sm, offset);
  gc._reading_mode = GamecubeMode_3;  // every poll overwrites this; 3 is the sane default
  gc_report = default_gc_report;

  const profile_t* profile = profile_get_active(OUTPUT_TARGET_GAMECUBE);
  if (profile) {
    printf("[gc] Active profile: %s\n", profile->name);
  }
}

uint8_t gc_kb_key_lookup(uint8_t hid_key)
{
  return hid_to_gc_key[hid_key];
}

uint8_t furthest_from_center(uint8_t a, uint8_t b, uint8_t center)
{
  int distance_a = abs(a - center);
  int distance_b = abs(b - center);
  if (distance_a > distance_b) {
    return a;
  } else {
    return b;
  }
}

// ============================================================================
// ANALOG POLL MODES
// ============================================================================
// A console polls with {0x40, analog_mode, motor_state}. Buttons and the main
// stick occupy bytes 0-3 of the 8-byte reply in every mode; bytes 4-7 change
// meaning with the requested mode, because the controller's full state is 10
// bytes and only 8 of them fit in the reply. joybus-pio stores the mode byte in
// GamecubeConsole._reading_mode and never reads it back (its SendReport still
// carries the upstream "TODO: Translate report according to reading mode"), so
// every reply has gone out in mode 3 regardless of what was asked for.
//
// Layouts below are from libjoybus (src/target/gc_controller.c, unit-tested)
// and agree with BlueRetro (main/wired/nsi.c) on modes 0/1/3/4. A packed pair
// puts the FIRST axis in the HIGH nibble.
//
//   mode 0, 5-7 : cx         cy         L:4|R:4    A:4|B:4
//   mode 1      : cx:4|cy:4  L          R          A:4|B:4
//   mode 2      : cx:4|cy:4  L:4|R:4    A          B
//   mode 3      : cx         cy         L          R         <- default
//   mode 4      : cx         cy         A          B
//
// Analog A/B only existed on pre-production controllers; a retail pad reports
// zero for both, which is also what our origin reply already claims
// (gc_origin_t.reserved0 / reserved1). We do the same unless the input has
// pressure-sensitive buttons (DS3, DS2, OG Xbox), in which case the pressure of
// whatever the profile maps onto GC A/B goes out (#114). Mode 3 has no A/B
// bytes, so the default poll is unaffected either way.
static uint8_t gc_analog_a = 0;
static uint8_t gc_analog_b = 0;

// input_event_t.pressure[] slot for each pressure-capable JP button
static const struct { uint32_t button; uint8_t index; } gc_pressure_slots[] = {
  { JP_BUTTON_DU, 0 }, { JP_BUTTON_DR, 1 }, { JP_BUTTON_DD, 2 }, { JP_BUTTON_DL, 3 },
  { JP_BUTTON_L2, 4 }, { JP_BUTTON_R2, 5 }, { JP_BUTTON_L1, 6 }, { JP_BUTTON_R1, 7 },
  { JP_BUTTON_B4, 8 }, { JP_BUTTON_B2, 9 }, { JP_BUTTON_B1, 10 }, { JP_BUTTON_B3, 11 },
};

// Strongest pressure among the inputs the profile routes to gc_button. If the
// digital bit is set with no pressure behind it (a combo, a button without a
// pressure slot), report full press so analog never contradicts digital.
static uint8_t gc_button_pressure(const profile_t* profile, const input_event_t* event,
                                  uint32_t gc_button, bool pressed)
{
  uint32_t sources = profile_inputs_for_output(profile, gc_button);
  uint8_t value = 0;

  for (size_t i = 0; i < sizeof(gc_pressure_slots) / sizeof(gc_pressure_slots[0]); i++) {
    if ((sources & gc_pressure_slots[i].button) &&
        event->pressure[gc_pressure_slots[i].index] > value) {
      value = event->pressure[gc_pressure_slots[i].index];
    }
  }

  return (pressed && value == 0) ? 0xFF : value;
}

static void __not_in_flash_func(gc_pack_analog_mode)(gc_report_t* dest,
                                                     const gc_report_t* src,
                                                     int mode)
{
  *dest = *src;  // bytes 0-3 (buttons + main stick) are mode-independent

  const uint8_t cx = src->cstick_x;
  const uint8_t cy = src->cstick_y;
  const uint8_t l  = src->l_analog;
  const uint8_t r  = src->r_analog;
  const uint8_t a  = gc_analog_a;
  const uint8_t b  = gc_analog_b;

  switch (mode)
  {
    case GamecubeMode_1:
      dest->raw8[4] = (cx & 0xF0) | (cy >> 4);
      dest->raw8[5] = l;
      dest->raw8[6] = r;
      dest->raw8[7] = (a & 0xF0) | (b >> 4);
      break;
    case GamecubeMode_2:
      dest->raw8[4] = (cx & 0xF0) | (cy >> 4);
      dest->raw8[5] = (l & 0xF0) | (r >> 4);
      dest->raw8[6] = a;
      dest->raw8[7] = b;
      break;
    case GamecubeMode_3:
      dest->raw8[4] = cx;
      dest->raw8[5] = cy;
      dest->raw8[6] = l;
      dest->raw8[7] = r;
      break;
    case GamecubeMode_4:
      dest->raw8[4] = cx;
      dest->raw8[5] = cy;
      dest->raw8[6] = a;
      dest->raw8[7] = b;
      break;
    case GamecubeMode_0:
    default:  // modes 5-7 pack the same way as mode 0
      dest->raw8[4] = cx;
      dest->raw8[5] = cy;
      dest->raw8[6] = (l & 0xF0) | (r >> 4);
      dest->raw8[7] = (a & 0xF0) | (b >> 4);
      break;
  }
}

// core1_task - inner-loop for the second core
void __not_in_flash_func(core1_task)(void)
{
  // Initialize Core 1 for safe flash writes (required for flash_safe_execute)
  flash_safe_execute_core_init();

  while (1)
  {
    // Wait for GameCube console to poll controller
    gc_rumble = GamecubeConsole_WaitForPoll(&gc) ? 255 : 0;

    // Send the report packed for the analog mode the console actually asked
    // for. Mode 3 is what every production game but Luigi's Mansion uses and
    // goes out untouched; a keyboard poll (0x54) reuses the same argument byte
    // for something that is not an analog mode, so it is excluded.
    if (gc_state.button_mode == BUTTON_MODE_KB || gc._reading_mode == GamecubeMode_3)
    {
      GamecubeConsole_SendReport(&gc, &gc_report);
    }
    else
    {
      gc_report_t packed_report;
      gc_pack_analog_mode(&packed_report, &gc_report, gc._reading_mode);
      GamecubeConsole_SendReport(&gc, &packed_report);
    }

    gc_kb_counter++;
    gc_kb_counter &= 15;

    update_output();
  }
}

// ============================================================================
// USBR → GAMECUBE BUTTON MAPPING
// ============================================================================
// Maps profile output (USBR format) to GameCube gc_report_t

static void map_usbr_to_gc_report(const profile_output_t* output, gc_report_t* report)
{
    uint32_t buttons = output->buttons;

    // D-pad (always direct mapping)
    report->dpad_up    = ((buttons & JP_BUTTON_DU) != 0) ? 1 : 0;
    report->dpad_down  = ((buttons & JP_BUTTON_DD) != 0) ? 1 : 0;
    report->dpad_left  = ((buttons & JP_BUTTON_DL) != 0) ? 1 : 0;
    report->dpad_right = ((buttons & JP_BUTTON_DR) != 0) ? 1 : 0;

    // Face buttons (USBR → GC mapping via aliases)
    // GC_BUTTON_A = JP_BUTTON_B2, GC_BUTTON_B = JP_BUTTON_B1 (matches gc_host input)
    report->a = ((buttons & GC_BUTTON_A) != 0) ? 1 : 0;
    report->b = ((buttons & GC_BUTTON_B) != 0) ? 1 : 0;
    report->x = ((buttons & GC_BUTTON_X) != 0) ? 1 : 0;
    report->y = ((buttons & GC_BUTTON_Y) != 0) ? 1 : 0;

    // Shoulder buttons
    report->z = ((buttons & GC_BUTTON_Z) != 0) ? 1 : 0;

    // L/R digital: set from button OR when analog exceeds threshold
    // This allows Xbox-style triggers (analog only, no click) to work with threshold
    const profile_t* profile = profile_get_active(OUTPUT_TARGET_GAMECUBE);
    uint8_t l_threshold = profile ? profile->l2_threshold : 250;
    uint8_t r_threshold = profile ? profile->r2_threshold : 250;

    // Threshold of 0 means disabled (never trigger digital from analog)
    report->l = ((buttons & GC_BUTTON_L) != 0 || (l_threshold > 0 && output->l2_analog >= l_threshold)) ? 1 : 0;
    report->r = ((buttons & GC_BUTTON_R) != 0 || (r_threshold > 0 && output->r2_analog >= r_threshold)) ? 1 : 0;

    // Start
    report->start = ((buttons & GC_BUTTON_START) != 0) ? 1 : 0;

    // Analog sticks (invert Y: HID uses 0=up, GameCube uses 0=down)
    // Clamp to 1-255 range - some games reject 0 as invalid
    report->stick_x = output->left_x < 1 ? 1 : output->left_x;
    report->stick_y = (255 - output->left_y) < 1 ? 1 : (255 - output->left_y);
    report->cstick_x = output->right_x < 1 ? 1 : output->right_x;
    report->cstick_y = (255 - output->right_y) < 1 ? 1 : (255 - output->right_y);

    // Trigger analog values
    report->l_analog = output->l2_analog;
    report->r_analog = output->r2_analog;
}

// update_output - updates gc_report output data for output to GameCube
void __not_in_flash_func(update_output)(void)
{
  static bool kbModeButtonHeld = false;
  static uint32_t last_buttons = 0;  // Remember last button state for combo detection

  // Get input from router (GameCube uses MERGE mode, all inputs merged to player 0)
  const input_event_t* event = router_get_output(OUTPUT_TARGET_GAMECUBE, 0);

  // Update last_buttons when we have new input
  if (event) {
    last_buttons = event->buttons;
  }


  if (!event || playersCount == 0) return;  // No new input to process

  // Build report locally to avoid Core 1 reading partial updates
  gc_report_t new_report;
  uint8_t new_analog_a = 0;
  uint8_t new_analog_b = 0;

  if (gc_state.button_mode == BUTTON_MODE_KB)
  {
    new_report = default_gc_kb_report;
  }
  else
  {
    new_report = default_gc_report;
  }

  // Handle keyboard mode toggle
  bool kbModeButtonPress = gc_keys_contain(event->keys, HID_KEY_SCROLL_LOCK) ||
                           gc_keys_contain(event->keys, HID_KEY_F14) ||
                           (gc_keys_contain(event->keys, GC_KB_TOGGLE_MOD1) &&
                            gc_keys_contain(event->keys, GC_KB_TOGGLE_MOD2) &&
                            gc_keys_contain(event->keys, GC_KB_TOGGLE_KEY));
  if (kbModeButtonPress)
  {
    if (!kbModeButtonHeld)
    {
      if (gc_state.button_mode != BUTTON_MODE_KB)
      {
        gc_state.button_mode = BUTTON_MODE_KB;
        GamecubeConsole_SetMode(&gc, GamecubeMode_KB);
        new_report = default_gc_kb_report;
        gc_kb_led = 0x4;
      }
      else
      {
        gc_state.button_mode = BUTTON_MODE_3;
        GamecubeConsole_SetMode(&gc, GamecubeMode_3);
        new_report = default_gc_report;
        gc_kb_led = 0;
      }
    }
    kbModeButtonHeld = true;
  }
  else
  {
    kbModeButtonHeld = false;
  }

  if (gc_state.button_mode != BUTTON_MODE_KB)
  {
    // ======================================================================
    // PROFILE-BASED BUTTON MAPPING
    // ======================================================================

    // Get active profile and apply it
    const profile_t* profile = profile_get_active(OUTPUT_TARGET_GAMECUBE);

    profile_output_t output;
    profile_apply(profile,
                  event->buttons,
                  event->analog[ANALOG_LX], event->analog[ANALOG_LY],  // left stick
                  event->analog[ANALOG_RX], event->analog[ANALOG_RY],  // right stick
                  event->analog[ANALOG_L2], event->analog[ANALOG_R2],  // triggers
                  event->analog[ANALOG_RZ],
                  &output);

    // Map profile output to GameCube report
    map_usbr_to_gc_report(&output, &new_report);

    // Analog A/B (#114): only a pressure-sensitive source has anything to say
    if (event->has_pressure) {
      new_analog_a = gc_button_pressure(profile, event, GC_BUTTON_A, new_report.a);
      new_analog_b = gc_button_pressure(profile, event, GC_BUTTON_B, new_report.b);
    }

    // Keyboard-specific transforms for GameCube
    if (event->type == INPUT_TYPE_KEYBOARD) {
      // Scale keyboard analog values to GameCube's smaller range
      const float gc_kb_scale = 0.61f;  // 78/128 ≈ 0.61
      new_report.stick_x  = scale_toward_center(new_report.stick_x, gc_kb_scale, 128);
      new_report.stick_y  = scale_toward_center(new_report.stick_y, gc_kb_scale, 128);
      new_report.cstick_x = scale_toward_center(new_report.cstick_x, gc_kb_scale, 128);
      new_report.cstick_y = scale_toward_center(new_report.cstick_y, gc_kb_scale, 128);

      // A1 (Home/Ctrl+Alt+Del) → gc-swiss IGR combo (Select+D-down+B+R)
      if ((event->buttons & JP_BUTTON_A1) != 0) {
        new_report.dpad_down = 1;
        new_report.b = 1;
        new_report.r = 1;
        new_report.z = 1;  // Z acts as select equivalent for IGR
      }
    }
  }
  else
  {
    // Keyboard mode. event->keys packs up to 3 simultaneous USB HID
    // keycodes (low byte = keycode[0], next byte = keycode[1], etc. — see
    // process_hid_keyboard in hid_keyboard.c). Translate each independently
    // and fill all three GC keypress slots so the report supports the
    // protocol's 3-key rollover instead of just the first key.
    uint8_t k0 = (uint8_t)((event->keys >>  0) & 0xFF);
    uint8_t k1 = (uint8_t)((event->keys >>  8) & 0xFF);
    uint8_t k2 = (uint8_t)((event->keys >> 16) & 0xFF);

    // Don't type the activation chord itself. Scroll Lock and F14 were never an
    // issue — the lookup table has no GameCube keycode for either — but Ctrl,
    // Alt and every letter are all mapped (GC_KEY_LEFTCTRL, GC_KEY_LEFTALT,
    // GC_KEY_K), so without this the chord would be sent to the game for as long
    // as it is held, starting on the frame that enters keyboard mode. Suppress
    // while the chord is matched rather than only on its leading edge.
    if (kbModeButtonPress) {
      k0 = k1 = k2 = 0;
    }

    new_report.keyboard.keypress[0] = gc_kb_key_lookup(k0);
    new_report.keyboard.keypress[1] = gc_kb_key_lookup(k1);
    new_report.keyboard.keypress[2] = gc_kb_key_lookup(k2);
    new_report.keyboard.checksum = new_report.keyboard.keypress[0] ^
                                  new_report.keyboard.keypress[1] ^
                                  new_report.keyboard.keypress[2] ^ gc_kb_counter;
    new_report.keyboard.counter = gc_kb_counter;
  }

  codes_task_for_output(OUTPUT_TARGET_GAMECUBE);

  // Atomically update global report
  gc_report = new_report;
  gc_analog_a = new_analog_a;
  gc_analog_b = new_analog_b;
}

// ============================================================================
// NATIVE OUTPUT CONFIG (web config: Output > Joybus page)
// ============================================================================

// Minimal JSON int extractor — kept inline to avoid pulling in cdc_commands' static helpers.
static bool gc_json_get_int(const char* json, const char* key, int* out_val) {
    char search[32];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char* start = strstr(json, search);
    if (!start) return false;
    start += strlen(search);
    while (*start == ' ' || *start == '\t') start++;
    if (*start == '-' || (*start >= '0' && *start <= '9')) {
        *out_val = atoi(start);
        return true;
    }
    return false;
}

static uint16_t gc_get_native_config(char* buf, uint16_t buf_size) {
    flash_t* settings = flash_get_settings();
    int current_pin = GC_DATA_PIN;
    if (settings && settings->joybus_data_pin > 0 && settings->joybus_data_pin <= 28) {
        current_pin = settings->joybus_data_pin;
    }
    int n = snprintf(buf, buf_size,
        "\"type\":\"joybus\","
        "\"modes\":[\"gamecube\"],"
        "\"current_mode\":\"gamecube\","
        "\"pins\":{"
            "\"data\":{\"label\":\"Data\",\"value\":%d,\"min\":0,\"max\":28,\"default\":%d}"
        "}",
        current_pin, GC_DATA_PIN);
    return (n > 0 && n < buf_size) ? (uint16_t)n : 0;
}

static bool gc_set_native_config(const char* json, char* response_buf, uint16_t response_size) {
    int pin = -1;
    if (!gc_json_get_int(json, "data", &pin)) {
        snprintf(response_buf, response_size, "{\"ok\":false,\"err\":\"missing pins.data\"}");
        return false;
    }
    if (pin < 0 || pin > 28) {
        snprintf(response_buf, response_size, "{\"ok\":false,\"err\":\"pin out of range\"}");
        return false;
    }
    flash_t* settings = flash_get_settings();
    if (!settings) {
        snprintf(response_buf, response_size, "{\"ok\":false,\"err\":\"flash not initialized\"}");
        return false;
    }
    settings->joybus_data_pin = (uint8_t)pin;
    flash_save_force(settings);
    snprintf(response_buf, response_size, "{\"ok\":true,\"reboot\":true}");

    // Schedule reboot after the response has had a chance to send.
    sleep_ms(150);
    platform_reboot();
    return true;
}

// ============================================================================
// CONSOLE PRESENCE DETECTION
// ============================================================================
// The joybus data line idles HIGH only while a powered console holds it up
// through its ~1k pull-up; our internal ~50k pull-down wins when nothing is
// attached. That is the whole test: HIGH -> play mode, LOW -> CDC config mode.
//
// It used to be one gpio_get() taken 200 ms after boot, and a single sample can
// miss two ways. The adapter draws its power from the same connector as the
// console, so both come up together and the console's line is not guaranteed to
// be high at t=200 ms; and a sample can land inside a joybus transfer, which
// drives the line low for the length of the burst. Either miss stuck the
// adapter in config mode for good -- nothing re-checked it and nothing
// recovered, so the only way out was a replug in the right order (#164, #165).
//
// So: keep sampling for a while instead of once, and re-check while in config
// mode so a console that shows up late is picked up without a replug.

#define GC_DETECT_SETTLE_MS     200  // let the line settle before the first sample
#define GC_DETECT_WINDOW_MS     800  // keep sampling this long before giving up
#define GC_DETECT_POLL_MS         5

#define GC_RECHECK_INTERVAL_MS  250  // how often to re-sample while in config mode
#define GC_RECHECK_CONFIRM       10  // consecutive highs (2.5 s) before rebooting

static uint gc_detect_pin = GC_DATA_PIN;

uint gamecube_detect_pin(void)
{
    flash_init();  // idempotent -- needed this early to read the pin override
    flash_t* settings = flash_get_settings();
    if (settings && settings->joybus_data_pin > 0 && settings->joybus_data_pin <= 28) {
        return settings->joybus_data_pin;
    }
    return GC_DATA_PIN;
}

bool gamecube_console_detect(void)
{
    // Honour the runtime pin override so detection uses the same pin joybus
    // init will claim later.
    gc_detect_pin = gamecube_detect_pin();

    gpio_init(gc_detect_pin);
    gpio_set_dir(gc_detect_pin, GPIO_IN);
    gpio_pull_down(gc_detect_pin);

    sleep_ms(GC_DETECT_SETTLE_MS);
    if (gpio_get(gc_detect_pin)) return true;  // unchanged 200 ms boot into play mode

    for (uint32_t waited = 0; waited < GC_DETECT_WINDOW_MS; waited += GC_DETECT_POLL_MS) {
        sleep_ms(GC_DETECT_POLL_MS);
        if (gpio_get(gc_detect_pin)) {
            printf("[gc] console detected after %lu ms\n",
                   (unsigned long)(GC_DETECT_SETTLE_MS + waited + GC_DETECT_POLL_MS));
            return true;
        }
    }
    return false;
}

void gamecube_config_mode_task(void)
{
    static uint32_t last_sample_ms = 0;
    static uint8_t high_streak = 0;

    uint32_t now = platform_time_ms();
    if ((uint32_t)(now - last_sample_ms) < GC_RECHECK_INTERVAL_MS) return;
    last_sample_ms = now;

    // Nothing but a console can pull this line high: joybus never claimed the
    // pin in config mode, so it is still an input with our pull-down engaged.
    if (!gpio_get(gc_detect_pin)) {
        high_streak = 0;
        return;
    }
    if (++high_streak < GC_RECHECK_CONFIRM) return;
    high_streak = 0;

    // Don't yank the board out from under someone who is configuring it: a CDC
    // host has to assert DTR for this to be true, so a plain power source or a
    // console-only hookup -- the case we are here to recover -- does not count.
    if (tud_cdc_connected()) return;

    printf("[gc] console appeared after boot - rebooting into play mode\n");
    platform_reboot();
}

// ============================================================================
// OUTPUT INTERFACE
// ============================================================================

#include "core/output_interface.h"

const OutputInterface gamecube_output_interface = {
    .name = "GameCube",
    .target = OUTPUT_TARGET_GAMECUBE,
    .init = ngc_init,
    .core1_task = core1_task,
    .task = NULL,
    .get_rumble = gc_get_rumble,
    .get_player_led = gc_get_kb_led,
    .get_profile_count = gc_get_profile_count,
    .get_active_profile = gc_get_active_profile_index,
    .set_active_profile = gc_set_active_profile,
    .get_profile_name = gc_get_profile_name,
    .get_trigger_threshold = gc_get_trigger_threshold,
    .get_native_config = gc_get_native_config,
    .set_native_config = gc_set_native_config,
};
