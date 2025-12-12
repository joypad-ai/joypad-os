// hid_keyboard.c
#include "hid_keyboard.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "pico/time.h"

// Analog stick intensity values (canonical - console layer can scale if needed)
#define KB_ANALOG_MID 64
#define KB_ANALOG_MAX 128

// DualSense instance state
typedef struct TU_ATTR_PACKED
{
  bool init;
  bool ready;
  uint8_t leds;
  uint8_t rumble;
} hid_kb_instance_t;

// Cached device report properties on mount
typedef struct TU_ATTR_PACKED
{
  hid_kb_instance_t instances[CFG_TUH_HID];
} hid_kb_device_t;

static hid_kb_device_t hid_kb_devices[MAX_DEVICES] = { 0 };

// Keyboard LED control
static uint8_t kbd_leds = 0;
static uint8_t prev_kbd_leds = 0xFF;

// Core functionality
// ------------------
static uint8_t const keycode2ascii[128][2] =  { HID_KEYCODE_TO_ASCII };

void calculate_coordinates(uint32_t stick_keys, int intensity, uint8_t *x_value, uint8_t *y_value) {
  uint16_t angle_degrees = 0;
  uint8_t offset = (127.0 - ((intensity/100.0) * 127.0));

  if (stick_keys && intensity) {
    if (stick_keys <= 0x000f) {
      switch (stick_keys)
      {
      case 0x01: // W
          angle_degrees = 0;
          break;
      case 0x02: // S
          angle_degrees = 180;
          break;
      case 0x04: // A
          angle_degrees = 270;
          break;
      case 0x08: // D
          angle_degrees = 90;
          break;
      default:
          break;
      }
    } else if (stick_keys <= 0x00ff) {
      switch (stick_keys)
      {
      case 0x12: // S ⇾ W
          angle_degrees = 0;
          break;
      case 0x81: // W ⇾ D
      case 0x18: // D ⇾ W
          angle_degrees = 45;
          break;
      case 0x84: // A ⇾ D
          angle_degrees = 90;
          break;
      case 0x82: // S ⇾ D
      case 0x28: // D ⇾ S
          angle_degrees = 135;
          break;
      case 0x21: // W ⇾ S
          angle_degrees = 180;
          break;
      case 0x42: // S ⇾ A
      case 0x24: // A ⇾ S
          angle_degrees = 225;
          break;
      case 0x41: // W ⇾ A
      case 0x14: // A ⇾ W
          angle_degrees = 315;
          break;
      case 0x48: // D ⇾ A
          angle_degrees = 270;
          break;
      default:
          break;
      }
    } else if (stick_keys <= 0x0fff) {
      switch (stick_keys)
      {
      case 0x841: // W ⇾ A ⇾ D
      case 0x812: // S ⇾ W ⇾ D
      case 0x182: // S ⇾ D ⇾ W
      case 0x814: // A ⇾ W ⇾ D
      case 0x184: // A ⇾ D ⇾ W
      case 0x128: // D ⇾ S ⇾ W
          angle_degrees = 45;
          break;
      case 0x821: // W ⇾ S ⇾ D
      case 0x281: // W ⇾ D ⇾ S
      case 0x842: // S ⇾ A ⇾ D
      case 0x824: // A ⇾ S ⇾ D
      case 0x284: // A ⇾ D ⇾ S
      case 0x218: // D ⇾ W ⇾ S
          angle_degrees = 135;
          break;
      case 0x421: // W ⇾ S ⇾ A
      case 0x241: // W ⇾ A ⇾ S
      case 0x482: // S ⇾ D ⇾ A
      case 0x214: // A ⇾ W ⇾ S
      case 0x248: // D ⇾ A ⇾ S
      case 0x428: // D ⇾ S ⇾ A
          angle_degrees = 225;
          break;
      case 0x124: // A ⇾ S ⇾ W
      case 0x418: // D ⇾ W ⇾ A
      case 0x148: // D ⇾ A ⇾ W
      case 0x481: // W ⇾ D ⇾ A
      case 0x412: // S ⇾ W ⇾ A
      case 0x142: // S ⇾ A ⇾ W
          angle_degrees = 315;
          break;
      default:
          break;
      }
    } else if (stick_keys <= 0xffff) {
      switch (stick_keys)
      {
      case 0x8412: // S ⇾ W ⇾ A ⇾ D
      case 0x8142: // S ⇾ A ⇾ W ⇾ D
      case 0x1842: // S ⇾ A ⇾ D ⇾ W
      case 0x8124: // A ⇾ S ⇾ W ⇾ D
      case 0x1824: // A ⇾ S ⇾ D ⇾ W
      case 0x1284: // A ⇾ D ⇾ S ⇾ W
          angle_degrees = 45;
          break;
      case 0x8421: // W ⇾ S ⇾ A ⇾ D
      case 0x8241: // W ⇾ A ⇾ S ⇾ D
      case 0x2841: // W ⇾ A ⇾ D ⇾ S
      case 0x8214: // A ⇾ W ⇾ S ⇾ D
      case 0x2814: // A ⇾ W ⇾ D ⇾ S
      case 0x2184: // A ⇾ D ⇾ W ⇾ S
          angle_degrees = 135;
          break;
      case 0x2148: // D ⇾ A ⇾ W ⇾ S
      case 0x4821: // W ⇾ S ⇾ D ⇾ A
      case 0x4281: // W ⇾ D ⇾ S ⇾ A
      case 0x2481: // W ⇾ D ⇾ A ⇾ S
      case 0x4218: // D ⇾ W ⇾ S ⇾ A
      case 0x2418: // D ⇾ W ⇾ A ⇾ S
          angle_degrees = 225;
          break;
      case 0x4812: // S ⇾ W ⇾ D ⇾ A
      case 0x4182: // S ⇾ D ⇾ W ⇾ A
      case 0x1482: // S ⇾ D ⇾ A ⇾ W
      case 0x4128: // D ⇾ S ⇾ W ⇾ A
      case 0x1428: // D ⇾ S ⇾ A ⇾ W
      case 0x1248: // D ⇾ A ⇾ S ⇾ W
          angle_degrees = 315;
          break;
      default:
          break;
      }
    }
  }

  // HID convention: Y-axis 0=up, 255=down
  switch (angle_degrees)
  {
  case 0: // Up
    *x_value = 128;
    *y_value = 1 + offset;
    break;

  case 45: // Up + Right
    *x_value = 245 - offset;
    *y_value = 11 + offset;
    break;

  case 90: // Right
    *x_value = 255 - offset;
    *y_value = 128;
    break;

  case 135: // Down + Right
    *x_value = 245 - offset;
    *y_value = 245 - offset;
    break;

  case 180: // Down
    *x_value = 128;
    *y_value = 255 - offset;
    break;

  case 225: // Down + Left
    *x_value = 11 + offset;
    *y_value = 245 - offset;
    break;

  case 270: // Left
    *x_value = 1 + offset;
    *y_value = 128;
    break;

  case 315: // Up + Left
    *x_value = 11 + offset;
    *y_value = 11 + offset;
    break;

  default:
    break;
  }

  TU_LOG1("in: %d° %d%, x:%d, y:%d, keys: %x\n", angle_degrees, intensity, *x_value, *y_value, stick_keys);
  return;
}

// look up new key in previous keys
static inline bool find_key_in_report(hid_keyboard_report_t const *report, uint8_t keycode)
{
  for(uint8_t i=0; i<6; i++) {
    if (report->keycode[i] == keycode)  return true;
  }
  return false;
}

// process usb hid input reports
void process_hid_keyboard(uint8_t dev_addr, uint8_t instance, uint8_t const* hid_kb_report, uint16_t len)
{
  uint32_t buttons;
  hid_keyboard_report_t const* report = (hid_keyboard_report_t const*)hid_kb_report;
  static hid_keyboard_report_t prev_report = { 0, 0, {0} }; // previous report to check key released

  uint8_t analog_left_x = 128;
  uint8_t analog_left_y = 128;
  uint8_t analog_right_x = 128;
  uint8_t analog_right_y = 128;
  uint8_t analog_l = 0;
  uint8_t analog_r = 0;
  bool dpad_left = false, dpad_down = false, dpad_right = false, dpad_up = false;
  bool btns_run = false, btns_sel = false, btns_b2 = false, btns_b1 = false,
       btns_b4 = false, btns_b3 = false, btns_l1 = false, btns_r1 = false,
       btns_a1 = false;

  uint32_t hatSwitchKeys = 0x0;
  uint32_t leftStickKeys = 0x0;
  uint32_t rightStickKeys = 0x0;
  uint8_t hatIndex = 0;
  uint8_t leftIndex = 0;
  uint8_t rightIndex = 0;

  bool const is_shift = report->modifier & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT);
  bool const is_ctrl = report->modifier & (KEYBOARD_MODIFIER_LEFTCTRL | KEYBOARD_MODIFIER_RIGHTCTRL);
  bool const is_alt = report->modifier & (KEYBOARD_MODIFIER_LEFTALT | KEYBOARD_MODIFIER_RIGHTALT);

  // parse 3 keycode bytes into single word to return
  uint32_t reportKeys = report->keycode[0] | (report->keycode[1] << 8) | (report->keycode[2] << 16);
  if (report->modifier & (KEYBOARD_MODIFIER_LEFTSHIFT)) {
    reportKeys = reportKeys << 8 | HID_KEY_SHIFT_LEFT;
  } else if (report->modifier & (KEYBOARD_MODIFIER_RIGHTSHIFT)) {
    reportKeys = reportKeys << 8 | HID_KEY_SHIFT_RIGHT;
  }
  if (is_ctrl) {
    reportKeys = reportKeys << 8 | HID_KEY_CONTROL_LEFT;
  }
  if (is_alt) {
    reportKeys = reportKeys << 8 | HID_KEY_ALT_LEFT;
  }
  if (report->modifier & (KEYBOARD_MODIFIER_LEFTGUI)) {
    reportKeys = reportKeys << 8 | HID_KEY_GUI_LEFT;
  } else if (report->modifier & (KEYBOARD_MODIFIER_RIGHTGUI)) {
    reportKeys = reportKeys << 8 | HID_KEY_GUI_RIGHT;
  }

  // wait until first report before sending init led output report
  if (!hid_kb_devices[dev_addr].instances[instance].ready) {
    hid_kb_devices[dev_addr].instances[instance].ready = true;
  }

  //------------- example code ignore control (non-printable) key affects -------------//
  for(uint8_t i=0; i<6; i++)
  {
    if ( report->keycode[i] )
    {
      if (report->keycode[i] == HID_KEY_ESCAPE || report->keycode[i] == HID_KEY_EQUAL) btns_run = true; // Start
      if (report->keycode[i] == HID_KEY_P || report->keycode[i] == HID_KEY_MINUS) btns_sel = true; // Select

      // Canonical button mapping (console layer handles any reordering)
      if (report->keycode[i] == HID_KEY_J || report->keycode[i] == HID_KEY_ENTER) btns_b1 = true;
      if (report->keycode[i] == HID_KEY_K || report->keycode[i] == HID_KEY_BACKSPACE) btns_b2 = true;
      if (report->keycode[i] == HID_KEY_L) btns_b4 = true;
      if (report->keycode[i] == HID_KEY_SEMICOLON) btns_b3 = true;
      if (report->keycode[i] == HID_KEY_U || report->keycode[i] == HID_KEY_PAGE_UP) btns_l1 = true;
      if (report->keycode[i] == HID_KEY_I || report->keycode[i] == HID_KEY_PAGE_DOWN) btns_r1 = true;
      // HAT SWITCH
      switch (report->keycode[i])
      {
      case HID_KEY_1:
      case HID_KEY_ARROW_UP:
          hatSwitchKeys |= (0x1 << (4 * hatIndex));
          hatIndex++;
          break;
      case HID_KEY_3:
      case HID_KEY_ARROW_DOWN:
          hatSwitchKeys |= (0x2 << (4 * hatIndex));
          hatIndex++;
          break;
      case HID_KEY_2:
      case HID_KEY_ARROW_LEFT:
          hatSwitchKeys |= (0x4 << (4 * hatIndex));
          hatIndex++;
          break;
      case HID_KEY_4:
      case HID_KEY_ARROW_RIGHT:
          hatSwitchKeys |= (0x8 << (4 * hatIndex));
          hatIndex++;
          break;
      default:
          break;
      }

      // LEFT STICK
      switch (report->keycode[i])
      {
      case HID_KEY_W:
          leftStickKeys |= (0x1 << (4 * leftIndex));
          leftIndex++;
          break;
      case HID_KEY_S:
          leftStickKeys |= (0x2 << (4 * leftIndex));
          leftIndex++;
          break;
      case HID_KEY_A:
          leftStickKeys |= (0x4 << (4 * leftIndex));
          leftIndex++;
          break;
      case HID_KEY_D:
          leftStickKeys |= (0x8 << (4 * leftIndex));
          leftIndex++;
          break;
      default:
          break;
      }

      // RIGHT STICK
      switch (report->keycode[i])
      {
      case HID_KEY_M:
          rightStickKeys |= (0x1 << (4 * rightIndex));
          rightIndex++;
          break;
      case HID_KEY_PERIOD:
          rightStickKeys |= (0x2 << (4 * rightIndex));
          rightIndex++;
          break;
      case HID_KEY_COMMA:
          rightStickKeys |= (0x4 << (4 * rightIndex));
          rightIndex++;
          break;
      case HID_KEY_SLASH:
          rightStickKeys |= (0x8 << (4 * rightIndex));
          rightIndex++;
          break;
      default:
          break;
      }

      // Ctrl+Alt+Delete -> Home/Guide button (console layer can map to IGR if needed)
      if (is_ctrl && is_alt && report->keycode[i] == HID_KEY_DELETE)
      {
        btns_a1 = true;
      }

      if ( find_key_in_report(&prev_report, report->keycode[i]) )
      {
        // exist in previous report means the current key is holding
      }else
      {
        // TU_LOG1("keycode(%d)\r\n", report->keycode[i]);
        // not existed in previous report means the current key is pressed
        // bool const is_shift = report->modifier & (KEYBOARD_MODIFIER_LEFTSHIFT | KEYBOARD_MODIFIER_RIGHTSHIFT);
        // uint8_t ch = keycode2ascii[report->keycode[i]][is_shift ? 1 : 0];
        // putchar(ch);
        // if ( ch == '\r' ) putchar('\n'); // added new line for enter key

        // fflush(stdout); // flush right away, else nanolib will wait for newline
      }
    }
  }

  // calculate left stick angle degrees (console layer can scale if needed)
  if (leftStickKeys) {
    int leftIntensity = is_shift ? KB_ANALOG_MID : KB_ANALOG_MAX;
    calculate_coordinates(leftStickKeys, leftIntensity, &analog_left_x, &analog_left_y);
  }

  if (rightStickKeys) {
    int rightIntensity = is_shift ? KB_ANALOG_MID : KB_ANALOG_MAX;
    calculate_coordinates(rightStickKeys, rightIntensity, &analog_right_x, &analog_right_y);
  }

  if (hatSwitchKeys) {
    uint8_t hat_switch_x, hat_switch_y;
    calculate_coordinates(hatSwitchKeys, 100, &hat_switch_x, &hat_switch_y);
    dpad_up = hat_switch_y > 128;
    dpad_down = hat_switch_y < 128;
    dpad_left = hat_switch_x < 128;
    dpad_right = hat_switch_x > 128;
  }

  // Active-high: set bit when button is pressed
  buttons = (((dpad_up)    ? USBR_BUTTON_DU : 0) |
             ((dpad_down)  ? USBR_BUTTON_DD : 0) |
             ((dpad_left)  ? USBR_BUTTON_DL : 0) |
             ((dpad_right) ? USBR_BUTTON_DR : 0) |
             ((btns_b1)    ? USBR_BUTTON_B1 : 0) |
             ((btns_b2)    ? USBR_BUTTON_B2 : 0) |
             ((btns_b3)    ? USBR_BUTTON_B3 : 0) |
             ((btns_b4)    ? USBR_BUTTON_B4 : 0) |
             ((btns_l1)    ? USBR_BUTTON_L1 : 0) |
             ((btns_r1)    ? USBR_BUTTON_R1 : 0) |
             ((btns_sel)   ? USBR_BUTTON_S1 : 0) |
             ((btns_run)   ? USBR_BUTTON_S2 : 0) |
             ((btns_a1)    ? USBR_BUTTON_A1 : 0));

  // TODO: map L2/R2/L3/R3 buttons

  input_event_t event = {
    .dev_addr = dev_addr,
    .instance = instance,
    .type = INPUT_TYPE_KEYBOARD,
    .buttons = buttons,
    .button_count = 6,  // Keyboard maps to 6 face buttons (B1-B4, L1, R1)
    .analog = {analog_left_x, analog_left_y, analog_right_x, analog_right_y, 128, analog_l, analog_r, 128},
    .keys = reportKeys
  };
  router_submit_input(&event);

  prev_report = *report;
}

// process usb hid output reports
void output_hid_keyboard(uint8_t dev_addr, uint8_t instance, device_output_config_t* config)
{
  // Keyboard LED control
  static uint8_t kbd_leds = 0;
  static uint8_t prev_kbd_leds = 0xFF;

  if (!hid_kb_devices[dev_addr].instances[instance].init && hid_kb_devices[dev_addr].instances[instance].ready)
  {
    hid_kb_devices[dev_addr].instances[instance].init = true;

    // kbd_leds = KEYBOARD_LED_NUMLOCK;
    tuh_hid_set_report(dev_addr, instance, 0, HID_REPORT_TYPE_OUTPUT, &kbd_leds, sizeof(kbd_leds));
  }
  else if (config->leds != hid_kb_devices[dev_addr].instances[instance].leds || config->test)
  {
    // LED state can be controlled externally via config->leds (from console/main.c)
    // or animated during fun mode. We use a local copy to preserve config as read-only.
    //
    // Flow:
    // 1. Copy config->leds to local variable (e.g., 0x00)
    // 2. Apply fun mode animation if active (e.g., → 0x05)
    // 3. Set keyboard hardware LEDs
    // 4. Store final LED state for comparison on next call
    //
    // This allows external control while adding test pattern effects without modifying
    // the read-only config parameter.
    uint8_t leds = config->leds;
    if (config->test) leds |= ((config->test >> (config->test & 0b00000111)) & 0b00000111);

    if (leds & 0x1) kbd_leds |= KEYBOARD_LED_NUMLOCK;
    else kbd_leds &= ~KEYBOARD_LED_NUMLOCK;
    if (leds & 0x2) kbd_leds |= KEYBOARD_LED_CAPSLOCK;
    else kbd_leds &= ~KEYBOARD_LED_CAPSLOCK;
    if (leds & 0x4) kbd_leds |= KEYBOARD_LED_SCROLLLOCK;
    else kbd_leds &= ~KEYBOARD_LED_SCROLLLOCK;

    tuh_hid_set_report(dev_addr, instance, 0, HID_REPORT_TYPE_OUTPUT, &kbd_leds, sizeof(kbd_leds));
    hid_kb_devices[dev_addr].instances[instance].leds = leds;  // Store for next comparison
  }
  if (config->rumble != hid_kb_devices[dev_addr].instances[instance].rumble)
  {
    if (config->rumble)
    {
      kbd_leds |= KEYBOARD_LED_CAPSLOCK | KEYBOARD_LED_SCROLLLOCK | KEYBOARD_LED_NUMLOCK;
    } else {
      kbd_leds = 0; // kbd_leds &= ~KEYBOARD_LED_CAPSLOCK;
    }
    hid_kb_devices[dev_addr].instances[instance].rumble = config->rumble;

    if (kbd_leds != prev_kbd_leds)
    {
      tuh_hid_set_report(dev_addr, instance, 0, HID_REPORT_TYPE_OUTPUT, &kbd_leds, sizeof(kbd_leds));
      prev_kbd_leds = kbd_leds;
    }
  }
}

// process usb hid output reports
void task_hid_keyboard(uint8_t dev_addr, uint8_t instance, device_output_config_t* config)
{
  const uint32_t interval_ms = 20;
  static uint32_t start_ms = 0;

  uint32_t current_time_ms = to_ms_since_boot(get_absolute_time());
  if (current_time_ms - start_ms >= interval_ms)
  {
    start_ms = current_time_ms;
    output_hid_keyboard(dev_addr, instance, config);
  }
}

// resets default values in case devices are hotswapped
void unmount_hid_keyboard(uint8_t dev_addr, uint8_t instance)
{
  hid_kb_devices[dev_addr].instances[instance].ready = false;
  hid_kb_devices[dev_addr].instances[instance].init = false;
  hid_kb_devices[dev_addr].instances[instance].leds = 0;
}

DeviceInterface hid_keyboard_interface = {
  .name = "HID Keyboard",
  .is_device = NULL,
  .init = NULL,
  .task = task_hid_keyboard,
  .process = process_hid_keyboard,
  .unmount = unmount_hid_keyboard,
};
