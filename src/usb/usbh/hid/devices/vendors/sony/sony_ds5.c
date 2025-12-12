// sony_ds5.c
#include "sony_ds5.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "pico/time.h"
#include "app_config.h"

static uint16_t tpadLastPos;
static bool tpadDragging;

// DualSense instance state
typedef struct TU_ATTR_PACKED
{
  uint8_t rumble;
  uint8_t player;
} ds5_instance_t;

// Cached device report properties on mount
typedef struct TU_ATTR_PACKED
{
  ds5_instance_t instances[CFG_TUH_HID];
} ds5_device_t;

static ds5_device_t ds5_devices[MAX_DEVICES] = { 0 };

const char* dpad_str[] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW", "none" };

// check if device is Sony PlayStation 5 controllers
bool is_sony_ds5(uint16_t vid, uint16_t pid) {
  return ((vid == 0x054c && pid == 0x0ce6)); // Sony DualSense
}

// check if 2 reports are different enough
bool diff_report_ds5(sony_ds5_report_t const* rpt1, sony_ds5_report_t const* rpt2) {
  // Check x1 to ry with a threshold
  if (diff_than_n(rpt1->x1, rpt2->x1, 2) || diff_than_n(rpt1->y1, rpt2->y1, 2) ||
      diff_than_n(rpt1->x2, rpt2->x2, 2) || diff_than_n(rpt1->y2, rpt2->y2, 2) ||
      diff_than_n(rpt1->rx, rpt2->rx, 2) || diff_than_n(rpt1->ry, rpt2->ry, 2)) {
    return true;
  }

  // check the base buttons dpad -> r3 then
  // manually check fields up to 'counter'
  if (memcmp(&rpt1->rz + 1, &rpt2->rz + 1, 2) ||
      rpt1->ps != rpt2->ps ||
      rpt1->tpad != rpt2->tpad ||
      rpt1->mute != rpt2->mute) {
    return true;
  }

  // Check tpad_f1_down and tpad_f1_pos
  if (rpt1->tpad_f1_down != rpt2->tpad_f1_down ||
    memcmp(rpt1->tpad_f1_pos, rpt2->tpad_f1_pos, sizeof(rpt1->tpad_f1_pos)) != 0) {
    return true;
  }

  return false;
}

// process usb hid input reports
void input_sony_ds5(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  uint32_t buttons;
  // previous report used to compare for changes
  static sony_ds5_report_t prev_report[5] = { 0 };

  uint8_t const report_id = report[0];
  report++;
  len--;

  // all buttons state is stored in ID 1
  if (report_id == 1)
  {
    sony_ds5_report_t ds5_report;
    memcpy(&ds5_report, report, sizeof(ds5_report));

    // counter is +1, assign to make it easier to compare 2 report
    prev_report[dev_addr-1].counter = ds5_report.counter;

    if ( diff_report_ds5(&prev_report[dev_addr-1], &ds5_report) )
    {
      TU_LOG1("(x1, y1, x2, y2, rx, ry) = (%u, %u, %u, %u, %u, %u)\r\n", ds5_report.x1, ds5_report.y1, ds5_report.x2, ds5_report.y2, ds5_report.rx, ds5_report.ry);
      TU_LOG1("DPad = %s ", dpad_str[ds5_report.dpad]);

      if (ds5_report.square   ) TU_LOG1("Square ");
      if (ds5_report.cross    ) TU_LOG1("Cross ");
      if (ds5_report.circle   ) TU_LOG1("Circle ");
      if (ds5_report.triangle ) TU_LOG1("Triangle ");

      if (ds5_report.l1       ) TU_LOG1("L1 ");
      if (ds5_report.r1       ) TU_LOG1("R1 ");
      if (ds5_report.l2       ) TU_LOG1("L2 ");
      if (ds5_report.r2       ) TU_LOG1("R2 ");

      if (ds5_report.share    ) TU_LOG1("Share ");
      if (ds5_report.option   ) TU_LOG1("Option ");
      if (ds5_report.l3       ) TU_LOG1("L3 ");
      if (ds5_report.r3       ) TU_LOG1("R3 ");

      if (ds5_report.ps       ) TU_LOG1("PS ");
      if (ds5_report.tpad     ) TU_LOG1("TPad ");
      if (ds5_report.mute     ) TU_LOG1("Mute ");

      if (!ds5_report.tpad_f1_down) TU_LOG1("F1 ");

      uint16_t tx = (((ds5_report.tpad_f1_pos[1] & 0x0f) << 8)) | ((ds5_report.tpad_f1_pos[0] & 0xff) << 0);
      uint16_t ty = (((ds5_report.tpad_f1_pos[1] & 0xf0) >> 4)) | ((ds5_report.tpad_f1_pos[2] & 0xff) << 4);
      // TU_LOG1(" (tx, ty) = (%u, %u)\r\n", tx, ty);
      TU_LOG1("\r\n");

      bool dpad_up    = (ds5_report.dpad == 0 || ds5_report.dpad == 1 || ds5_report.dpad == 7);
      bool dpad_right = (ds5_report.dpad >= 1 && ds5_report.dpad <= 3);
      bool dpad_down  = (ds5_report.dpad >= 3 && ds5_report.dpad <= 5);
      bool dpad_left  = (ds5_report.dpad >= 5 && ds5_report.dpad <= 7);

      // Touchpad left/right click detection (touchpad is ~1920 wide, center at 960)
      bool tpad_left = ds5_report.tpad && !ds5_report.tpad_f1_down && tx < 960;
      bool tpad_right = ds5_report.tpad && !ds5_report.tpad_f1_down && tx >= 960;

      buttons = (((dpad_up)             ? JP_BUTTON_DU : 0) |
                 ((dpad_down)           ? JP_BUTTON_DD : 0) |
                 ((dpad_left)           ? JP_BUTTON_DL : 0) |
                 ((dpad_right)          ? JP_BUTTON_DR : 0) |
                 ((ds5_report.cross)    ? JP_BUTTON_B1 : 0) |
                 ((ds5_report.circle)   ? JP_BUTTON_B2 : 0) |
                 ((ds5_report.square)   ? JP_BUTTON_B3 : 0) |
                 ((ds5_report.triangle) ? JP_BUTTON_B4 : 0) |
                 ((ds5_report.l1)       ? JP_BUTTON_L1 : 0) |
                 ((ds5_report.r1)       ? JP_BUTTON_R1 : 0) |
                 ((ds5_report.l2)       ? JP_BUTTON_L2 : 0) |
                 ((ds5_report.r2)       ? JP_BUTTON_R2 : 0) |
                 ((ds5_report.share)    ? JP_BUTTON_S1 : 0) |
                 ((ds5_report.option)   ? JP_BUTTON_S2 : 0) |
                 ((ds5_report.l3)       ? JP_BUTTON_L3 : 0) |
                 ((ds5_report.r3)       ? JP_BUTTON_R3 : 0) |
                 ((ds5_report.ps)       ? JP_BUTTON_A1 : 0) |
                 ((ds5_report.tpad)     ? JP_BUTTON_A2 : 0) |
                 ((ds5_report.mute)     ? JP_BUTTON_A3 : 0) |
                 ((tpad_left)           ? JP_BUTTON_L4 : 0) |
                 ((tpad_right)          ? JP_BUTTON_R4 : 0));

      // Touch Pad - provides mouse-like delta for horizontal swipes
      // Can be used for spinners, camera control, etc. (platform-agnostic)
      int8_t touchpad_delta_x = 0;
      if (!ds5_report.tpad_f1_down) {
        // Calculate horizontal swipe delta while finger is down
        if (tpadDragging) {
          int16_t delta = 0;
          if (tx >= tpadLastPos) delta = tx - tpadLastPos;
          else delta = (-1) * (tpadLastPos - tx);

          // Clamp delta to reasonable range
          if (delta > 12) delta = 12;
          if (delta < -12) delta = -12;

          touchpad_delta_x = (int8_t)delta;
        }

        tpadLastPos = tx;
        tpadDragging = true;
      } else {
        tpadDragging = false;
      }

      uint8_t analog_1x = ds5_report.x1;
      uint8_t analog_1y = ds5_report.y1;  // HID convention: 0=up, 255=down
      uint8_t analog_2x = ds5_report.x2;
      uint8_t analog_2y = ds5_report.y2;  // HID convention: 0=up, 255=down
      uint8_t analog_l = ds5_report.rx;
      uint8_t analog_r = ds5_report.ry;

      // keep analog within range [1-255]
      ensureAllNonZero(&analog_1x, &analog_1y, &analog_2x, &analog_2y);

      // add to accumulator and post to the state machine
      // if a scan from the host machine is ongoing, wait
      input_event_t event = {
        .dev_addr = dev_addr,
        .instance = instance,
        .type = INPUT_TYPE_GAMEPAD,
        .buttons = buttons,
        .button_count = 10,  // PS5: Cross, Circle, Square, Triangle, L1, R1, L2, R2, L3, R3
        .analog = {analog_1x, analog_1y, analog_2x, analog_2y, 128, analog_l, analog_r, 128},
        .delta_x = touchpad_delta_x,  // Touchpad horizontal swipe as mouse-like delta
        .keys = 0,
        // Motion data (DS5 has full 3-axis gyro and accel)
        .has_motion = true,
        .accel = {ds5_report.accel[0], ds5_report.accel[1], ds5_report.accel[2]},
        .gyro = {ds5_report.gyro[0], ds5_report.gyro[1], ds5_report.gyro[2]},
      };
      router_submit_input(&event);

      prev_report[dev_addr-1] = ds5_report;
    }
  }
}

// process usb hid output reports
void output_sony_ds5(uint8_t dev_addr, uint8_t instance, device_output_config_t* config) {
  ds5_feedback_t ds5_fb = {0};

  // set flags for trigger_r, trigger_l, lightbar, and player_led
  ds5_fb.flags |= (1 << 0 | 1 << 1); // haptics
  ds5_fb.flags |= (1 << 10); // lightbar
  ds5_fb.flags |= (1 << 12); // player_led

  // Adaptive trigger feedback - console-controlled via profile/config
  // Simulates analog trigger resistance for enhanced tactile feedback
  if (config->trigger_threshold > 0) {
    ds5_fb.flags |= (1 << 2); // trigger_r
    ds5_fb.flags |= (1 << 3); // trigger_l

    // Convert threshold from 0-255 to percentage (0-100)
    int32_t perc_threshold = (config->trigger_threshold * 100) / 255;

    // Calculate resistance values for analog trigger click simulation
    uint8_t l2_start_resistance_value = (perc_threshold * 255) / 100;
    uint8_t r2_start_resistance_value = (perc_threshold * 255) / 100;

    uint8_t l2_trigger_start_resistance = (uint8_t)(0x94 * (l2_start_resistance_value / 255.0));
    uint8_t l2_trigger_effect_force =
      (uint8_t)((0xb4 - l2_trigger_start_resistance) * (l2_start_resistance_value / 255.0) + l2_trigger_start_resistance);

    uint8_t r2_trigger_start_resistance = (uint8_t)(0x94 * (r2_start_resistance_value / 255.0));
    uint8_t r2_trigger_effect_force =
      (uint8_t)((0xb4 - r2_trigger_start_resistance) * (r2_start_resistance_value / 255.0) + r2_trigger_start_resistance);

    // Configure left trigger haptics
    ds5_fb.trigger_l.motor_mode = 0x02; // Resistance mode
    ds5_fb.trigger_l.start_resistance = l2_trigger_start_resistance;
    ds5_fb.trigger_l.effect_force = l2_trigger_effect_force;
    ds5_fb.trigger_l.range_force = 0xff;

    // Configure right trigger haptics
    ds5_fb.trigger_r.motor_mode = 0x02; // Resistance mode
    ds5_fb.trigger_r.start_resistance = r2_trigger_start_resistance;
    ds5_fb.trigger_r.effect_force = r2_trigger_effect_force;
    ds5_fb.trigger_r.range_force = 0xff;
  }

  // Console-specific LED colors from led_config.h
  switch (config->player_index+1)
  {
  case 1:
    ds5_fb.player_led = LED_P1_PATTERN;
    ds5_fb.lightbar_r = LED_P1_R;
    ds5_fb.lightbar_g = LED_P1_G;
    ds5_fb.lightbar_b = LED_P1_B;
    break;

  case 2:
    ds5_fb.player_led = LED_P2_PATTERN;
    ds5_fb.lightbar_r = LED_P2_R;
    ds5_fb.lightbar_g = LED_P2_G;
    ds5_fb.lightbar_b = LED_P2_B;
    break;

  case 3:
    ds5_fb.player_led = LED_P3_PATTERN;
    ds5_fb.lightbar_r = LED_P3_R;
    ds5_fb.lightbar_g = LED_P3_G;
    ds5_fb.lightbar_b = LED_P3_B;
    break;

  case 4:
    ds5_fb.player_led = LED_P4_PATTERN;
    ds5_fb.lightbar_r = LED_P4_R;
    ds5_fb.lightbar_g = LED_P4_G;
    ds5_fb.lightbar_b = LED_P4_B;
    break;

  case 5:
    ds5_fb.player_led = LED_P5_PATTERN;
    ds5_fb.lightbar_r = LED_P5_R;
    ds5_fb.lightbar_g = LED_P5_G;
    ds5_fb.lightbar_b = LED_P5_B;
    break;

  default:
    ds5_fb.player_led = LED_DEFAULT_PATTERN;
    ds5_fb.lightbar_r = LED_DEFAULT_R;
    ds5_fb.lightbar_g = LED_DEFAULT_G;
    ds5_fb.lightbar_b = LED_DEFAULT_B;
    break;
  }

  // test pattern
  if (config->player_index+1 && config->test) {
    ds5_fb.player_led = config->test;
    ds5_fb.lightbar_r = config->test;
    ds5_fb.lightbar_g = config->test+64;
    ds5_fb.lightbar_b = config->test+128;
  }

  if (config->rumble) {
    ds5_fb.rumble_l = 192;
    ds5_fb.rumble_r = 192;
  } else {
    ds5_fb.rumble_l = 0;
    ds5_fb.rumble_r = 0;
  }

  if (ds5_devices[dev_addr].instances[instance].rumble != config->rumble ||
      ds5_devices[dev_addr].instances[instance].player != ds5_fb.player_led ||
      config->test)
  {
    ds5_devices[dev_addr].instances[instance].rumble = config->rumble;
    ds5_devices[dev_addr].instances[instance].player = ds5_fb.player_led & 0xff;
    tuh_hid_send_report(dev_addr, instance, 5, &ds5_fb, sizeof(ds5_fb));
  }
}

// process usb hid output reports
void task_sony_ds5(uint8_t dev_addr, uint8_t instance, device_output_config_t* config) {
  const uint32_t interval_ms = 20;
  static uint32_t start_ms = 0;

  uint32_t current_time_ms = to_ms_since_boot(get_absolute_time());
  if (current_time_ms - start_ms >= interval_ms) {
    start_ms = current_time_ms;
    output_sony_ds5(dev_addr, instance, config);
  }
}

// resets default values in case devices are hotswapped
void unmount_sony_ds5(uint8_t dev_addr, uint8_t instance)
{
  ds5_devices[dev_addr].instances[instance].rumble = 0;
  ds5_devices[dev_addr].instances[instance].player = 0xff;
}

DeviceInterface sony_ds5_interface = {
  .name = "Sony DualSense",
  .is_device = is_sony_ds5,
  .process = input_sony_ds5,
  .task = task_sony_ds5,
  .unmount = unmount_sony_ds5,
};
