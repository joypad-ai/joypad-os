// sinput_host.c - SInput USB Host Driver
// Reads SInput controllers for full-fidelity input passthrough.
#include "sinput_host.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "usb/usbh/hid/hid_utils.h"
#include "pico/time.h"
#include "app_config.h"
#include <stdlib.h>

// Per-instance cached output state
typedef struct TU_ATTR_PACKED {
  uint8_t rumble_left;
  uint8_t rumble_right;
  uint8_t player;
  uint8_t led_r, led_g, led_b;
} sinput_instance_t;

// Per-device state
typedef struct TU_ATTR_PACKED {
  sinput_instance_t instances[CFG_TUH_HID];
  bool has_motion;
} sinput_device_t;

static sinput_device_t sinput_devices[MAX_DEVICES] = { 0 };
static sinput_report_t prev_reports[MAX_DEVICES] = { 0 };

// Check if device is a Joypad SInput controller
bool is_sinput_host(uint16_t vid, uint16_t pid) {
  return (vid == SINPUT_VID && pid == SINPUT_PID);
}

// Send feature request to learn device capabilities
bool init_sinput_host(uint8_t dev_addr, uint8_t instance) {
  sinput_devices[dev_addr].has_motion = false;
  memset(&sinput_devices[dev_addr].instances[instance], 0, sizeof(sinput_instance_t));
  sinput_devices[dev_addr].instances[instance].player = 0xFF;
  memset(&prev_reports[dev_addr], 0, sizeof(sinput_report_t));

  // Send feature request (output report with cmd=SINPUT_CMD_FEATURES)
  sinput_output_t req = {0};
  req.report_id = SINPUT_REPORT_ID_OUTPUT;
  req.command = SINPUT_CMD_FEATURES;
  tuh_hid_send_report(dev_addr, instance, SINPUT_REPORT_ID_OUTPUT, &req.command, sizeof(req) - 1);
  return true;
}

// Check if two reports differ enough to warrant processing
static bool diff_sinput_report(const sinput_report_t* a, const sinput_report_t* b) {
  // Check buttons
  if (memcmp(a->buttons, b->buttons, 4) != 0) return true;

  // Check sticks with threshold
  if (abs(a->lx - b->lx) > 256 || abs(a->ly - b->ly) > 256 ||
      abs(a->rx - b->rx) > 256 || abs(a->ry - b->ry) > 256) return true;

  // Check triggers
  if (abs(a->lt - b->lt) > 256 || abs(a->rt - b->rt) > 256) return true;

  // Check motion (always pass through if present)
  if (a->accel_x != b->accel_x || a->accel_y != b->accel_y || a->accel_z != b->accel_z ||
      a->gyro_x != b->gyro_x || a->gyro_y != b->gyro_y || a->gyro_z != b->gyro_z) return true;

  return false;
}

// Process incoming SInput reports
void process_sinput_host(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  uint8_t const report_id = report[0];
  report++;
  len--;

  // Feature response (Report ID 0x02)
  if (report_id == SINPUT_REPORT_ID_FEATURES) {
    // Feature response data: byte 0 = capability flags
    // Bit 0: has_motion
    if (len >= 1) {
      sinput_devices[dev_addr].has_motion = (report[0] & 0x01) != 0;
      printf("SInput features: has_motion=%d\n", sinput_devices[dev_addr].has_motion);
    }
    return;
  }

  // Input report (Report ID 0x01)
  if (report_id != SINPUT_REPORT_ID_INPUT) return;
  if (len < sizeof(sinput_report_t) - 1) return;  // -1 for report_id already consumed

  sinput_report_t rpt;
  rpt.report_id = report_id;
  memcpy(&rpt.plug_status, report, sizeof(sinput_report_t) - 1);

  // Skip unchanged reports
  if (!diff_sinput_report(&prev_reports[dev_addr], &rpt)) return;
  prev_reports[dev_addr] = rpt;

  // Extract 32-bit button field from 4 bytes (little-endian)
  uint32_t sinput_buttons = rpt.buttons[0] | (rpt.buttons[1] << 8) |
                            (rpt.buttons[2] << 16) | (rpt.buttons[3] << 24);

  // Reverse-map SInput buttons to JP buttons
  uint32_t buttons = 0;
  if (sinput_buttons & SINPUT_MASK_SOUTH)     buttons |= JP_BUTTON_B1;
  if (sinput_buttons & SINPUT_MASK_EAST)      buttons |= JP_BUTTON_B2;
  if (sinput_buttons & SINPUT_MASK_WEST)      buttons |= JP_BUTTON_B3;
  if (sinput_buttons & SINPUT_MASK_NORTH)     buttons |= JP_BUTTON_B4;
  if (sinput_buttons & SINPUT_MASK_L1)        buttons |= JP_BUTTON_L1;
  if (sinput_buttons & SINPUT_MASK_R1)        buttons |= JP_BUTTON_R1;
  if (sinput_buttons & SINPUT_MASK_L2)        buttons |= JP_BUTTON_L2;
  if (sinput_buttons & SINPUT_MASK_R2)        buttons |= JP_BUTTON_R2;
  if (sinput_buttons & SINPUT_MASK_BACK)      buttons |= JP_BUTTON_S1;
  if (sinput_buttons & SINPUT_MASK_START)     buttons |= JP_BUTTON_S2;
  if (sinput_buttons & SINPUT_MASK_L3)        buttons |= JP_BUTTON_L3;
  if (sinput_buttons & SINPUT_MASK_R3)        buttons |= JP_BUTTON_R3;
  if (sinput_buttons & SINPUT_MASK_DU)        buttons |= JP_BUTTON_DU;
  if (sinput_buttons & SINPUT_MASK_DD)        buttons |= JP_BUTTON_DD;
  if (sinput_buttons & SINPUT_MASK_DL)        buttons |= JP_BUTTON_DL;
  if (sinput_buttons & SINPUT_MASK_DR)        buttons |= JP_BUTTON_DR;
  if (sinput_buttons & SINPUT_MASK_GUIDE)     buttons |= JP_BUTTON_A1;
  if (sinput_buttons & SINPUT_MASK_CAPTURE)   buttons |= JP_BUTTON_A2;
  if (sinput_buttons & SINPUT_MASK_L_PADDLE1) buttons |= JP_BUTTON_L4;
  if (sinput_buttons & SINPUT_MASK_R_PADDLE1) buttons |= JP_BUTTON_R4;

  // Convert 16-bit signed sticks to 8-bit unsigned centered at 128
  // SInput: -32768..32767, center=0
  // JP:     0..255, center=128
  uint8_t analog_lx = (uint8_t)((rpt.lx / 256) + 128);
  uint8_t analog_ly = (uint8_t)((rpt.ly / 256) + 128);
  uint8_t analog_rx = (uint8_t)((rpt.rx / 256) + 128);
  uint8_t analog_ry = (uint8_t)((rpt.ry / 256) + 128);

  // Convert 16-bit triggers (0..32767) to 8-bit (0..255)
  uint8_t analog_lt = (uint8_t)((rpt.lt * 255) / 32767);
  uint8_t analog_rt = (uint8_t)((rpt.rt * 255) / 32767);

  // Ensure non-zero analog values for stick axes
  ensureAllNonZero(&analog_lx, &analog_ly, &analog_rx, &analog_ry);

  // Build input event
  input_event_t event = {
    .dev_addr = dev_addr,
    .instance = instance,
    .type = INPUT_TYPE_GAMEPAD,
    .transport = INPUT_TRANSPORT_USB,
    .buttons = buttons,
    .button_count = 10,
    .analog = {analog_lx, analog_ly, analog_rx, analog_ry, analog_lt, analog_rt},
    .has_motion = sinput_devices[dev_addr].has_motion,
    .accel = {rpt.accel_x, rpt.accel_y, rpt.accel_z},
    .gyro = {rpt.gyro_x, rpt.gyro_y, rpt.gyro_z},
    .accel_range = 4000,
    .gyro_range = 2000,
  };

  router_submit_input(&event);
}

// Send feedback (rumble, player LED, RGB LED) to upstream SInput device
void task_sinput_host(uint8_t dev_addr, uint8_t instance, device_output_config_t* config) {
  const uint32_t interval_ms = 20;
  static uint32_t start_ms = 0;

  uint32_t current_time_ms = to_ms_since_boot(get_absolute_time());
  if (current_time_ms - start_ms < interval_ms) return;
  start_ms = current_time_ms;

  sinput_instance_t* inst = &sinput_devices[dev_addr].instances[instance];

  // Get per-player feedback state
  int8_t player_idx = find_player_index(dev_addr, instance);
  feedback_state_t* fb = (player_idx >= 0) ? feedback_get_state(player_idx) : NULL;

  uint8_t rumble_left = fb ? fb->rumble.left : 0;
  uint8_t rumble_right = fb ? fb->rumble.right : 0;

  // Derive player number from config
  uint8_t player_num = (config->player_index >= 0) ? (uint8_t)(config->player_index + 1) : 0;

  // Get RGB from feedback state
  uint8_t led_r = 0, led_g = 0, led_b = 0;
  if (fb && (fb->led.r || fb->led.g || fb->led.b)) {
    led_r = fb->led.r;
    led_g = fb->led.g;
    led_b = fb->led.b;
  } else {
    // Fallback to app-specific defaults
    switch (config->player_index + 1) {
    case 1:  led_r = LED_P1_R; led_g = LED_P1_G; led_b = LED_P1_B; break;
    case 2:  led_r = LED_P2_R; led_g = LED_P2_G; led_b = LED_P2_B; break;
    case 3:  led_r = LED_P3_R; led_g = LED_P3_G; led_b = LED_P3_B; break;
    case 4:  led_r = LED_P4_R; led_g = LED_P4_G; led_b = LED_P4_B; break;
    case 5:  led_r = LED_P5_R; led_g = LED_P5_G; led_b = LED_P5_B; break;
    default: led_r = LED_DEFAULT_R; led_g = LED_DEFAULT_G; led_b = LED_DEFAULT_B; break;
    }
  }

  // Test pattern override
  if (config->player_index + 1 && config->test) {
    player_num = config->test;
    led_r = config->test;
    led_g = config->test + 64;
    led_b = config->test + 128;
  }

  // Only send when values have changed
  bool changed = (inst->rumble_left != rumble_left ||
                  inst->rumble_right != rumble_right ||
                  inst->player != player_num ||
                  inst->led_r != led_r ||
                  inst->led_g != led_g ||
                  inst->led_b != led_b ||
                  config->test);

  if (!changed) return;

  // Send rumble if changed
  if (inst->rumble_left != rumble_left || inst->rumble_right != rumble_right || config->test) {
    sinput_output_t out = {0};
    out.report_id = SINPUT_REPORT_ID_OUTPUT;
    out.command = SINPUT_CMD_HAPTIC;
    sinput_haptic_t* haptic = (sinput_haptic_t*)out.data;
    haptic->type = 2;  // ERM
    haptic->left_amplitude = rumble_left;
    haptic->right_amplitude = rumble_right;
    tuh_hid_send_report(dev_addr, instance, SINPUT_REPORT_ID_OUTPUT, &out.command, sizeof(out) - 1);
    inst->rumble_left = rumble_left;
    inst->rumble_right = rumble_right;
  }

  // Send player LED if changed
  if (inst->player != player_num || config->test) {
    sinput_output_t out = {0};
    out.report_id = SINPUT_REPORT_ID_OUTPUT;
    out.command = SINPUT_CMD_PLAYER_LED;
    out.data[0] = player_num;
    tuh_hid_send_report(dev_addr, instance, SINPUT_REPORT_ID_OUTPUT, &out.command, sizeof(out) - 1);
    inst->player = player_num;
  }

  // Send RGB LED if changed
  if (inst->led_r != led_r || inst->led_g != led_g || inst->led_b != led_b || config->test) {
    sinput_output_t out = {0};
    out.report_id = SINPUT_REPORT_ID_OUTPUT;
    out.command = SINPUT_CMD_RGB_LED;
    out.data[0] = led_r;
    out.data[1] = led_g;
    out.data[2] = led_b;
    tuh_hid_send_report(dev_addr, instance, SINPUT_REPORT_ID_OUTPUT, &out.command, sizeof(out) - 1);
    inst->led_r = led_r;
    inst->led_g = led_g;
    inst->led_b = led_b;
  }
}

// Clear per-device state on disconnect
void unmount_sinput_host(uint8_t dev_addr, uint8_t instance) {
  memset(&sinput_devices[dev_addr].instances[instance], 0, sizeof(sinput_instance_t));
  sinput_devices[dev_addr].has_motion = false;
  memset(&prev_reports[dev_addr], 0, sizeof(sinput_report_t));
}

DeviceInterface sinput_host_interface = {
  .name = "Joypad SInput",
  .is_device = is_sinput_host,
  .init = init_sinput_host,
  .process = process_sinput_host,
  .task = task_sinput_host,
  .unmount = unmount_sinput_host,
};
