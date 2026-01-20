// gamecube_device.c - GameCube Output Device
//
// Outputs controller data to GameCube via joybus protocol.
// Uses the universal profile system for button remapping.

#include "gamecube_device.h"
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

// Declaration of global variables
GamecubeConsole gc;
gc_report_t gc_report;
PIO pio = pio0;

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

extern void GamecubeConsole_init(GamecubeConsole* console, uint pin, PIO pio, int sm, int offset);
extern bool GamecubeConsole_WaitForPoll(GamecubeConsole* console);
extern void GamecubeConsole_SendReport(GamecubeConsole* console, gc_report_t *report);
extern void GamecubeConsole_SetMode(GamecubeConsole* console, GamecubeMode mode);

uint8_t hid_to_gc_key[256] = {[0 ... 255] = GC_KEY_NOT_FOUND};
uint8_t gc_last_rumble = 0;
uint8_t gc_kb_counter = 0;

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

  // Configure custom UART pins (12=TX, 13=RX)
  gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
  gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

  // corrects UART serial output after overclock
  stdio_init_all();

  // Initialize flash settings system
  flash_init();

  // Profile system is initialized by app - just set up callbacks
  profile_set_player_count_callback(gc_get_player_count_for_profile);

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

// core1_task - inner-loop for the second core
void __not_in_flash_func(core1_task)(void)
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

  // Always check profile switching combo with last known state
  // This ensures combo detection works even when controller doesn't send updates while buttons held
  if (playersCount > 0) {
    profile_check_switch_combo(last_buttons);
  }

  if (!event || playersCount == 0) return;  // No new input to process

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

  // Handle keyboard mode toggle
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
    // ======================================================================

    // Get active profile and apply it
    const profile_t* profile = profile_get_active(OUTPUT_TARGET_GAMECUBE);

    profile_output_t output;
    profile_apply(profile,
                  event->buttons,
                  event->analog[ANALOG_LX], event->analog[ANALOG_LY],  // left stick
                  event->analog[ANALOG_RX], event->analog[ANALOG_RY],  // right stick
                  event->analog[ANALOG_L2], event->analog[ANALOG_R2],  // triggers
                  &output);

    // Map profile output to GameCube report
    map_usbr_to_gc_report(&output, &new_report);

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
    // Keyboard mode
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

  // Atomically update global report
  gc_report = new_report;
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
};
