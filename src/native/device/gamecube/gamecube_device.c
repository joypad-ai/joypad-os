// gamecube.c

#include "gamecube_device.h"
#include "gamecube_config.h"
#include "joybus.pio.h"
#include "GamecubeConsole.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pico/flash.h"
#include "tusb.h"
#include "core/services/storage/flash.h"

// Declaration of global variables
GamecubeConsole gc;
gc_report_t gc_report;
PIO pio = pio0;

// GameCube-specific state for USB device output
static uint8_t gc_rumble = 0;
static uint8_t gc_kb_led = 0;

uint8_t gc_get_rumble(void) { return gc_rumble; }
uint8_t gc_get_kb_led(void) { return gc_kb_led; }

// ============================================================================
// PROFILE SYSTEM
// ============================================================================

// All available profiles (stored in flash, const = read-only)
static const gc_profile_t profiles[GC_PROFILE_COUNT] = {
    GC_PROFILE_DEFAULT,      // Profile 0
    GC_PROFILE_SNES,         // Profile 1
    GC_PROFILE_SSBM,         // Profile 2
    GC_PROFILE_MKWII,        // Profile 3
    GC_PROFILE_FIGHTING,     // Profile 4
};

// Active profile pointer (4 bytes of RAM, points to flash data)
static const gc_profile_t* active_profile = &profiles[GC_DEFAULT_PROFILE_INDEX];

// Current profile index (for cycling through profiles)
static uint8_t active_profile_index = GC_DEFAULT_PROFILE_INDEX;

// Get the current active profile (for external access)
gc_profile_t* get_active_profile(void) {
    return (gc_profile_t*)active_profile;  // Cast away const for external access
}

// ============================================================================
// CONSOLE-LOCAL STATE (Phase 3: Router Migration)
// ============================================================================

#include "core/router/router.h"

// Console-specific state (not input data from router)
static struct {
    int button_mode;  // BUTTON_MODE_KB or BUTTON_MODE_3
} gc_state = {
    .button_mode = BUTTON_MODE_3  // Default to gamepad mode
};

extern void GamecubeConsole_init(GamecubeConsole* console, uint pin, PIO pio, int sm, int offset);
extern bool GamecubeConsole_WaitForPoll(GamecubeConsole* console);
extern void GamecubeConsole_SendReport(GamecubeConsole* console, gc_report_t *report);
extern void GamecubeConsole_SetMode(GamecubeConsole* console, GamecubeMode mode);
extern void neopixel_indicate_profile(uint8_t profile_index);
extern bool neopixel_is_indicating(void);
extern void feedback_trigger(uint8_t profile_index, uint8_t player_count);
extern bool feedback_is_active(void);

uint8_t hid_to_gc_key[256] = {[0 ... 255] = GC_KEY_NOT_FOUND};
uint8_t gc_last_rumble = 0;
uint8_t gc_kb_counter = 0;

// Left stick sensitivity is now configured in gamecube.h via GC_LEFT_STICK_SENSITIVITY

// Helper function to scale analog values relative to center (128)
static inline uint8_t scale_toward_center(uint8_t val, float scale, uint8_t center)
{
  int16_t rel = (int16_t)val - (int16_t)center;
  int16_t scaled = (int16_t)(rel * scale);
  int16_t result = scaled + (int16_t)center;
  if (result < 0) result = 0;
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
  hid_to_gc_key[HID_KEY_GRAVE] = GC_KEY_YEN; // HID_KEY_KANJI3
  hid_to_gc_key[HID_KEY_PRINT_SCREEN] = GC_KEY_AT; // hankaku/zenkaku HID_KEY_LANG5
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
  hid_to_gc_key[HID_KEY_GUI_LEFT] = GC_KEY_LEFTUNK1; // muhenkan HID_KEY_KANJI5
  hid_to_gc_key[HID_KEY_SPACE] = GC_KEY_SPACE;
  hid_to_gc_key[HID_KEY_GUI_RIGHT] = GC_KEY_RIGHTUNK1; // henkan/zenkouho HID_KEY_KANJI4
  hid_to_gc_key[HID_KEY_APPLICATION] = GC_KEY_RIGHTUNK2; // hiragana/katakana HID_KEY_LANG4
  hid_to_gc_key[HID_KEY_ARROW_LEFT] = GC_KEY_LEFT;
  hid_to_gc_key[HID_KEY_ARROW_DOWN] = GC_KEY_DOWN;
  hid_to_gc_key[HID_KEY_ARROW_UP] = GC_KEY_UP;
  hid_to_gc_key[HID_KEY_ARROW_RIGHT] = GC_KEY_RIGHT;
  hid_to_gc_key[HID_KEY_ENTER] = GC_KEY_ENTER;
  hid_to_gc_key[HID_KEY_HOME] = GC_KEY_HOME; // fn + up
  hid_to_gc_key[HID_KEY_END] = GC_KEY_END; // fn + right
  hid_to_gc_key[HID_KEY_PAGE_DOWN] = GC_KEY_PAGEDOWN; // fn + left
  hid_to_gc_key[HID_KEY_PAGE_UP] = GC_KEY_PAGEUP; // fn + down
  // hid_to_gc_key[HID_KEY_SCROLL_LOCK] = GC_KEY_SCROLLLOCK; // fn + insert
}

// init for gamecube communication
void ngc_init()
{
  // over clock CPU for correct timing with GC
  set_sys_clock_khz(130000, true);

  // Configure custom UART pins (12=TX, 13=RX)
  gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

  // corrects UART serial output after overclock
  stdio_init_all();

  // Initialize flash settings system
  flash_init();

  // Load saved profile from flash (if valid)
  flash_t settings;
  if (flash_load(&settings)) {
    // Valid settings found - restore saved profile
    if (settings.active_profile_index < GC_PROFILE_COUNT) {
      active_profile_index = settings.active_profile_index;
      active_profile = &profiles[active_profile_index];
      printf("Loaded profile from flash: %s (%s)\n", active_profile->name, active_profile->description);
    } else {
      printf("Invalid profile index in flash (%d), using default\n", settings.active_profile_index);
    }
  } else {
    printf("No valid settings in flash, using default profile\n");
  }

  // Ground gpio attatched to sheilding
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

  // Initialize the BOOTSEL_PIN as input
  gpio_init(BOOTSEL_PIN);
  gpio_set_dir(BOOTSEL_PIN, GPIO_IN);
  gpio_pull_up(BOOTSEL_PIN);

  // Reboot into bootsel mode if GC 3.3V not detected.
  gpio_init(GC_3V3_PIN);
  gpio_set_dir(GC_3V3_PIN, GPIO_IN);
  gpio_pull_down(GC_3V3_PIN);

  sleep_ms(200);
  if (!gpio_get(GC_3V3_PIN)) reset_usb_boot(0, 0);

  int sm = -1;
  int offset = -1;
  gc_kb_key_lookup_init();
  GamecubeConsole_init(&gc, GC_DATA_PIN, pio, sm, offset);
  gc_report = default_gc_report;
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

//
// core1_entry - inner-loop for the second core
void __not_in_flash_func(core1_entry)(void)
{
  // Initialize Core 1 for safe flash writes (required for flash_safe_execute)
  flash_safe_execute_core_init();

  while (1)
  {
    // Wait for GameCube console to poll controller
    gc_rumble = GamecubeConsole_WaitForPoll(&gc) ? 255 : 0;

    // Send GameCube controller button report
    GamecubeConsole_SendReport(&gc, &gc_report);

    gc_kb_counter++;
    gc_kb_counter &= 15;

    // TODO(Phase 3): Mouse motion handling needs refactoring for router architecture
    // Mouse delta accumulation should be in console-local state, not players[]
    // For now, disabled since GameCube MERGE mode primarily uses gamepads

    update_output();

    // printf("MODE: %d\n", gc._reading_mode);
  }
}

// ============================================================================
// PROFILE SWITCHING
// ============================================================================

// Switch to a specific profile with visual and haptic feedback
static void switch_to_profile(uint8_t new_profile_index)
{
  active_profile_index = new_profile_index;
  active_profile = &profiles[active_profile_index];

  // Trigger visual and haptic feedback to indicate which profile was selected
  neopixel_indicate_profile(active_profile_index);  // NeoPixel LED blinking
  feedback_trigger(active_profile_index, playersCount);  // Rumble and player LED

  // Save profile selection to flash (debounced - writes after 5 seconds)
  flash_t settings;
  settings.active_profile_index = active_profile_index;
  flash_save(&settings);

  printf("Profile switched to: %s (%s)\n", active_profile->name, active_profile->description);
}

// Check for profile switching: SELECT + D-pad Up/Down
static void check_profile_switch_combo(uint32_t buttons)
{
  static uint32_t select_hold_start = 0;   // When Select was first pressed
  static bool select_was_held = false;
  static bool dpad_up_was_pressed = false;
  static bool dpad_down_was_pressed = false;
  static bool initial_trigger_done = false; // Has first 2-second trigger happened?
  const uint32_t INITIAL_HOLD_TIME_MS = 2000; // Must hold 2 seconds for first trigger

  if (playersCount == 0) return; // No controllers connected
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
    uint8_t new_index = (active_profile_index + 1) % GC_PROFILE_COUNT;
    switch_to_profile(new_index);
    initial_trigger_done = true; // Mark that first trigger happened
  }
  dpad_up_was_pressed = dpad_up_pressed;

  // D-pad Down - cycle backward on rising edge
  if (dpad_down_pressed && !dpad_down_was_pressed)
  {
    uint8_t new_index = (active_profile_index == 0) ? (GC_PROFILE_COUNT - 1) : (active_profile_index - 1);
    switch_to_profile(new_index);
    initial_trigger_done = true; // Mark that first trigger happened
  }
  dpad_down_was_pressed = dpad_down_pressed;
}

// Helper function to apply a button mapping to the report
static inline void apply_button_mapping(gc_report_t* report, gc_button_output_t action, bool pressed)
{
  if (!pressed) return; // Button not pressed, nothing to do

  switch (action)
  {
    case GC_BTN_A:
      report->a = 1;
      break;
    case GC_BTN_B:
      report->b = 1;
      break;
    case GC_BTN_X:
      report->x = 1;
      break;
    case GC_BTN_Y:
      report->y = 1;
      break;
    case GC_BTN_Z:
      report->z = 1;
      break;
    case GC_BTN_START:
      report->start = 1;
      break;
    case GC_BTN_DPAD_UP:
      report->dpad_up = 1;
      break;
    case GC_BTN_DPAD_DOWN:
      report->dpad_down = 1;
      break;
    case GC_BTN_DPAD_LEFT:
      report->dpad_left = 1;
      break;
    case GC_BTN_DPAD_RIGHT:
      report->dpad_right = 1;
      break;
    case GC_BTN_L:
      report->l = 1;
      break;
    case GC_BTN_R:
      report->r = 1;
      break;
    case GC_BTN_L_FULL:
      report->l = 1;
      report->l_analog = 255;
      break;
    case GC_BTN_R_FULL:
      report->r = 1;
      report->r_analog = 255;
      break;
    case GC_BTN_L_LIGHT:
      // Light shield for SSBM - L analog at 1% (no digital)
      if (report->l_analog < 1) {
        report->l_analog = 1;
      }
      break;
    case GC_BTN_C_UP:
      report->cstick_y = 255;
      break;
    case GC_BTN_C_DOWN:
      report->cstick_y = 0;
      break;
    case GC_BTN_C_LEFT:
      report->cstick_x = 0;
      break;
    case GC_BTN_C_RIGHT:
      report->cstick_x = 255;
      break;
    case GC_BTN_NONE:
    default:
      // No action
      break;
  }
}

//
// update_output - updates gc_report output data for output to GameCube
void __not_in_flash_func(update_output)(void)
{
  static bool kbModeButtonHeld = false;

  // Get input from router (GameCube uses MERGE mode, all inputs merged to player 0)
  const input_event_t* event = router_get_output(OUTPUT_TARGET_GAMECUBE, 0);
  if (!event || playersCount == 0) return;  // No input available

  // Check for profile switching combo
  check_profile_switch_combo(event->buttons);

  // Build report locally to avoid Core 1 reading partial updates
  gc_report_t new_report;

  if (gc_state.button_mode == BUTTON_MODE_KB)
  {
    new_report = default_gc_kb_report;
  }
  else
  {
    new_report = default_gc_report;
  }

  // Process input event (replaces multi-player loop since router already merges)
  int32_t byte = (event->buttons & 0x3ffff);
  bool kbModeButtonPress = event->keys == HID_KEY_SCROLL_LOCK || event->keys == HID_KEY_F14;
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
      // All USBRetro buttons are mapped according to active_profile
      // ======================================================================

      // D-pad (always mapped directly)
      new_report.dpad_up    |= ((byte & USBR_BUTTON_DU) == 0) ? 1 : 0;
      new_report.dpad_right |= ((byte & USBR_BUTTON_DR) == 0) ? 1 : 0;
      new_report.dpad_down  |= ((byte & USBR_BUTTON_DD) == 0) ? 1 : 0;
      new_report.dpad_left  |= ((byte & USBR_BUTTON_DL) == 0) ? 1 : 0;

      // Face buttons (B1-B4) - profile configurable
      apply_button_mapping(&new_report, active_profile->b1_button, (byte & USBR_BUTTON_B1) == 0);
      apply_button_mapping(&new_report, active_profile->b2_button, (byte & USBR_BUTTON_B2) == 0);
      apply_button_mapping(&new_report, active_profile->b3_button, (byte & USBR_BUTTON_B3) == 0);
      apply_button_mapping(&new_report, active_profile->b4_button, (byte & USBR_BUTTON_B4) == 0);

      // Shoulder buttons (L1/R1) - profile configurable
      apply_button_mapping(&new_report, active_profile->l1_button, (byte & USBR_BUTTON_L1) == 0);
      apply_button_mapping(&new_report, active_profile->r1_button, (byte & USBR_BUTTON_R1) == 0);

      // System buttons (S1/S2) - profile configurable
      apply_button_mapping(&new_report, active_profile->s1_button, (byte & USBR_BUTTON_S1) == 0);
      apply_button_mapping(&new_report, active_profile->s2_button, (byte & USBR_BUTTON_S2) == 0);

      // Stick buttons (L3/R3) - profile configurable
      apply_button_mapping(&new_report, active_profile->l3_button, (byte & USBR_BUTTON_L3) == 0);
      apply_button_mapping(&new_report, active_profile->r3_button, (byte & USBR_BUTTON_R3) == 0);

      // Auxiliary buttons (A1/A2) - profile configurable
      apply_button_mapping(&new_report, active_profile->a1_button, (byte & USBR_BUTTON_A1) == 0);
      apply_button_mapping(&new_report, active_profile->a2_button, (byte & USBR_BUTTON_A2) == 0);

      // Trigger behavior (L2/R2) - profile configurable
      bool l2_pressed = ((byte & USBR_BUTTON_L2) == 0);
      bool r2_pressed = ((byte & USBR_BUTTON_R2) == 0);

      switch (active_profile->l2_behavior)
      {
        case GC_TRIGGER_L_THRESHOLD:
          if (l2_pressed) new_report.l = 1;
          break;
        case GC_TRIGGER_L_FULL:
          if (l2_pressed) { new_report.l = 1; new_report.l_analog = 255; }
          break;
        case GC_TRIGGER_Z_INSTANT:
          if (l2_pressed) new_report.z = 1;
          break;
        case GC_TRIGGER_L_CUSTOM:
          // Custom L trigger - use profile-defined analog value + digital at threshold
          if (active_profile->l2_analog_value > 0 && new_report.l_analog < active_profile->l2_analog_value) {
            new_report.l_analog = active_profile->l2_analog_value;
          }
          if (l2_pressed) new_report.l = 1;
          break;
        default:
          break;
      }

      switch (active_profile->r2_behavior)
      {
        case GC_TRIGGER_R_THRESHOLD:
          if (r2_pressed) new_report.r = 1;
          break;
        case GC_TRIGGER_R_FULL:
          if (r2_pressed) { new_report.r = 1; new_report.r_analog = 255; }
          break;
        case GC_TRIGGER_Z_INSTANT:
          if (r2_pressed) new_report.z = 1;
          break;
        case GC_TRIGGER_R_CUSTOM:
          // Custom R trigger - use profile-defined analog value + digital at threshold
          if (active_profile->r2_analog_value > 0 && new_report.r_analog < active_profile->r2_analog_value) {
            new_report.r_analog = active_profile->r2_analog_value;
          }
          if (r2_pressed) new_report.r = 1;
          break;
        case GC_TRIGGER_LR_BOTH:
          // SSBM quit combo - R2 triggers both L and R digital buttons
          if (r2_pressed) {
            new_report.l = 1;
            new_report.r = 1;
          }
          break;
        default:
          break;
      }

      // Analog sticks with profile-based sensitivity
      new_report.stick_x    = scale_toward_center(event->analog[0], active_profile->left_stick_sensitivity, 128);
      new_report.stick_y    = scale_toward_center(event->analog[1], active_profile->left_stick_sensitivity, 128);
      new_report.cstick_x   = scale_toward_center(event->analog[2], active_profile->right_stick_sensitivity, 128);
      new_report.cstick_y   = scale_toward_center(event->analog[3], active_profile->right_stick_sensitivity, 128);
      new_report.l_analog   = event->analog[5];
      new_report.r_analog   = event->analog[6];

      // Keyboard-specific transforms for GameCube
      if (event->type == INPUT_TYPE_KEYBOARD) {
        // Scale keyboard analog values to GameCube's smaller range
        // Keyboard outputs 64/128 intensity, GC needs ~28/78 range
        const float gc_kb_scale = 0.61f;  // 78/128 ≈ 0.61
        new_report.stick_x  = scale_toward_center(new_report.stick_x, gc_kb_scale, 128);
        new_report.stick_y  = scale_toward_center(new_report.stick_y, gc_kb_scale, 128);
        new_report.cstick_x = scale_toward_center(new_report.cstick_x, gc_kb_scale, 128);
        new_report.cstick_y = scale_toward_center(new_report.cstick_y, gc_kb_scale, 128);

        // A1 (Home/Ctrl+Alt+Del) → gc-swiss IGR combo (Select+D-down+B+R)
        if ((byte & USBR_BUTTON_A1) == 0) {
          new_report.dpad_down = 1;
          new_report.b = 1;
          new_report.r = 1;
          // S1 already mapped via profile, but ensure it's pressed for IGR
          apply_button_mapping(&new_report, GC_BTN_Z, true);  // Z acts as select equivalent
        }
      }
    }
    else
    {
      // Keyboard mode - input_event_t only has keys field, not individual keypress array
      // For now, map the single key to all three slots (TODO: proper multi-key support)
      uint8_t gc_key = gc_kb_key_lookup(event->keys);
      new_report.keyboard.keypress[0] = gc_key;
      new_report.keyboard.keypress[1] = GC_KEY_NOT_FOUND;
      new_report.keyboard.keypress[2] = GC_KEY_NOT_FOUND;
      new_report.keyboard.checksum = new_report.keyboard.keypress[0] ^
                                    new_report.keyboard.keypress[1] ^
                                    new_report.keyboard.keypress[2] ^ gc_kb_counter;
      new_report.keyboard.counter = gc_kb_counter;
    }

  codes_task();

  // Atomically update global report (prevents Core 1 from seeing partial updates)
  gc_report = new_report;

}

// post_input_event removed - replaced by router architecture
// Input flow: USB drivers → router_submit_input() → router → router_get_output() → update_output()

// ============================================================================
// OUTPUT INTERFACE
// ============================================================================

#include "core/output_interface.h"

// Get adaptive trigger threshold for DualSense L2/R2

const OutputInterface gamecube_output_interface = {
    .name = "GameCube",
    .init = ngc_init,
    .handle_input = NULL,  // Router architecture - inputs come via router_get_output()
    .core1_entry = core1_entry,
    .task = NULL,  // GameCube doesn't need periodic task
};
