// switch2_pro.c
// Nintendo Switch 2 Pro Controller driver
// Requires USB bulk initialization sequence before HID reports work
// Based on procon2tool by HandHeldLegend

#include "switch2_pro.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "pico/time.h"
#include "host/usbh.h"
#include "host/usbh_pvt.h"

// Switch 2 Pro initialization commands (from procon2tool/joypad-web)
// All commands follow format: [cmd, 0x91, 0x00, arg, ...]
static const uint8_t SWITCH2_CMD_INIT_HID[] = {
  0x03, 0x91, 0x00, 0x0d, 0x00, 0x08, 0x00, 0x00, 0x01, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
static const uint8_t SWITCH2_CMD_07[] = { 0x07, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t SWITCH2_CMD_16[] = { 0x16, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t SWITCH2_CMD_REQUEST_MAC[] = {
  0x15, 0x91, 0x00, 0x01, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x02,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
static const uint8_t SWITCH2_CMD_LTK[] = {
  0x15, 0x91, 0x00, 0x02, 0x00, 0x11, 0x00, 0x00, 0x00,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};
static const uint8_t SWITCH2_CMD_15_03[] = { 0x15, 0x91, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00 };
static const uint8_t SWITCH2_CMD_LED_INIT[] = {
  0x09, 0x91, 0x00, 0x07, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t SWITCH2_CMD_IMU_02[] = {
  0x0c, 0x91, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00
};
static const uint8_t SWITCH2_CMD_ENABLE_HAPTICS[] = {
  0x03, 0x91, 0x00, 0x0a, 0x00, 0x04, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00
};
static const uint8_t SWITCH2_CMD_11[] = { 0x11, 0x91, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t SWITCH2_CMD_0A_08[] = {
  0x0a, 0x91, 0x00, 0x08, 0x00, 0x14, 0x00, 0x00, 0x01,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0x35, 0x00, 0x46, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
static const uint8_t SWITCH2_CMD_IMU_04[] = {
  0x0c, 0x91, 0x00, 0x04, 0x00, 0x04, 0x00, 0x00, 0x27, 0x00, 0x00, 0x00
};
static const uint8_t SWITCH2_CMD_10[] = { 0x10, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t SWITCH2_CMD_01[] = { 0x01, 0x91, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00 };
static const uint8_t SWITCH2_CMD_03_ALT[] = { 0x03, 0x91, 0x00, 0x01, 0x00, 0x00, 0x00 };
static const uint8_t SWITCH2_CMD_0A_02[] = {
  0x0a, 0x91, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x03, 0x00, 0x00
};

// Player LED patterns (cumulative)
static const uint8_t SWITCH2_LED_PATTERNS[] = { 0x01, 0x03, 0x07, 0x0F };

#define SWITCH2_INIT_CMD_COUNT 17

// Stick calibration data
typedef struct {
  uint16_t center;      // Calibrated center value
  bool calibrated;      // Whether this axis has been calibrated
} stick_cal_t;

// Instance state
typedef struct {
  switch2_init_state_t state;
  uint8_t cmd_index;
  uint8_t ep_out;
  uint8_t itf_num;
  bool xfer_pending;
  uint8_t rumble_left;
  uint8_t rumble_right;
  uint8_t player_led;
  uint32_t last_haptic_ms;  // Timestamp of last haptic send
  // Stick calibration (captured on first reports assuming sticks at rest)
  stick_cal_t cal_lx, cal_ly, cal_rx, cal_ry;
  uint8_t cal_samples;  // Number of samples collected for calibration
} switch2_instance_t;

// Device state
typedef struct {
  switch2_instance_t instances[CFG_TUH_HID];
  uint8_t instance_count;
} switch2_device_t;

static switch2_device_t switch2_devices[MAX_DEVICES] = { 0 };

// Static buffers for USB operations
static uint8_t switch2_config_buf[256] CFG_TUH_MEM_ALIGN;
static uint8_t switch2_cmd_buf[32] CFG_TUH_MEM_ALIGN;
static uint8_t switch2_haptic_buf[64] CFG_TUH_MEM_ALIGN;

// Haptic output packet counter (0x50-0x5F)
static uint8_t haptic_counter = 0;

// Check if device is Switch 2 Pro controller
// TODO: Add bcdDevice check to distinguish from Switch 1 Pro
static bool is_switch2_pro(uint16_t vid, uint16_t pid) {
  return (vid == 0x057e && pid == SWITCH2_PRO_PID);
}

// Effective stick range from center (Switch sticks reach ~75-80% of theoretical max)
// Using 1600 instead of 2048 to reach full output range at physical limits
#define STICK_RANGE 1600
#define CAL_SAMPLES_NEEDED 4  // Number of samples to average for calibration

// Scale calibrated analog value to 8-bit (0-255, 128 = center)
// val: raw 12-bit value (0-4095)
// center: calibrated center value
// Returns: 0-255 with 128 as center
static uint8_t scale_analog_calibrated(uint16_t val, uint16_t center) {
  int32_t centered = (int32_t)val - (int32_t)center;

  // Scale to -128..+127 range using effective stick range
  int32_t scaled = (centered * 127) / STICK_RANGE;

  // Clamp to valid range
  if (scaled < -128) scaled = -128;
  if (scaled > 127) scaled = 127;

  // Convert to 0-255 with 128 as center
  return (uint8_t)(scaled + 128);
}

// Encode haptic data for one motor (5 bytes)
// Switch 2 Pro haptic format:
//   Byte 0: Amplitude (high band)
//   Byte 1: Frequency (high band) - 0x60 for felt rumble
//   Byte 2: Amplitude (low band)
//   Byte 3: Frequency (low band) - 0x60 for felt rumble
//   Byte 4: Flags/mode - 0x00
// intensity: 0 = off, 1-255 = rumble strength
static void encode_haptic(uint8_t intensity, uint8_t* out) {
  if (intensity == 0) {
    out[0] = 0x00;
    out[1] = 0x00;
    out[2] = 0x00;
    out[3] = 0x00;
    out[4] = 0x00;
    return;
  }

  // Scale intensity (1-255) to amplitude (0x40-0xFF)
  // amp = 0x40 + (intensity / 255) * 0xBF
  uint8_t amp = 0x40 + ((uint16_t)intensity * 0xBF) / 255;

  out[0] = amp;   // High band amplitude
  out[1] = 0x60;  // High band frequency (felt rumble)
  out[2] = amp;   // Low band amplitude
  out[3] = 0x60;  // Low band frequency (felt rumble)
  out[4] = 0x00;  // Flags
}

// Get initialization command by index
static const uint8_t* get_init_cmd(uint8_t index, uint8_t* len, uint8_t player_led) {
  static uint8_t player_led_cmd[16];

  switch (index) {
    case 0:  *len = sizeof(SWITCH2_CMD_INIT_HID); return SWITCH2_CMD_INIT_HID;
    case 1:  *len = sizeof(SWITCH2_CMD_07); return SWITCH2_CMD_07;
    case 2:  *len = sizeof(SWITCH2_CMD_16); return SWITCH2_CMD_16;
    case 3:  *len = sizeof(SWITCH2_CMD_REQUEST_MAC); return SWITCH2_CMD_REQUEST_MAC;
    case 4:  *len = sizeof(SWITCH2_CMD_LTK); return SWITCH2_CMD_LTK;
    case 5:  *len = sizeof(SWITCH2_CMD_15_03); return SWITCH2_CMD_15_03;
    case 6:  *len = sizeof(SWITCH2_CMD_LED_INIT); return SWITCH2_CMD_LED_INIT;
    case 7:  *len = sizeof(SWITCH2_CMD_IMU_02); return SWITCH2_CMD_IMU_02;
    case 8:  *len = sizeof(SWITCH2_CMD_ENABLE_HAPTICS); return SWITCH2_CMD_ENABLE_HAPTICS;
    case 9:  *len = sizeof(SWITCH2_CMD_11); return SWITCH2_CMD_11;
    case 10: *len = sizeof(SWITCH2_CMD_0A_08); return SWITCH2_CMD_0A_08;
    case 11: *len = sizeof(SWITCH2_CMD_IMU_04); return SWITCH2_CMD_IMU_04;
    case 12: *len = sizeof(SWITCH2_CMD_10); return SWITCH2_CMD_10;
    case 13: *len = sizeof(SWITCH2_CMD_01); return SWITCH2_CMD_01;
    case 14: *len = sizeof(SWITCH2_CMD_03_ALT); return SWITCH2_CMD_03_ALT;
    case 15: *len = sizeof(SWITCH2_CMD_0A_02); return SWITCH2_CMD_0A_02;
    case 16: // Player LED command
      player_led_cmd[0] = 0x09;
      player_led_cmd[1] = 0x91;
      player_led_cmd[2] = 0x00;
      player_led_cmd[3] = 0x07;
      player_led_cmd[4] = 0x00;
      player_led_cmd[5] = 0x08;
      player_led_cmd[6] = 0x00;
      player_led_cmd[7] = 0x00;
      player_led_cmd[8] = (player_led < 4) ? SWITCH2_LED_PATTERNS[player_led] : 0x01;
      memset(&player_led_cmd[9], 0, 7);
      *len = 16;
      return player_led_cmd;
    default:
      *len = 0;
      return NULL;
  }
}

// Find bulk OUT endpoint on interface 1
static bool find_bulk_endpoint(uint8_t dev_addr, uint8_t* ep_out, uint8_t* itf_num) {
  tusb_xfer_result_t result = tuh_descriptor_get_configuration_sync(
    dev_addr, 0, switch2_config_buf, sizeof(switch2_config_buf));

  if (result != XFER_RESULT_SUCCESS) {
    printf("[SWITCH2] Failed to get config descriptor\r\n");
    return false;
  }

  tusb_desc_configuration_t* cfg = (tusb_desc_configuration_t*)switch2_config_buf;
  uint8_t const* p_desc = switch2_config_buf;
  uint8_t const* end = switch2_config_buf + cfg->wTotalLength;

  bool found_interface = false;
  while (p_desc < end) {
    uint8_t desc_type = tu_desc_type(p_desc);

    if (desc_type == TUSB_DESC_INTERFACE) {
      tusb_desc_interface_t const* itf = (tusb_desc_interface_t const*)p_desc;
      if (itf->bInterfaceNumber == 1) {
        found_interface = true;
        *itf_num = itf->bInterfaceNumber;
        printf("[SWITCH2] Found interface 1: class=0x%02X endpoints=%d\r\n",
                itf->bInterfaceClass, itf->bNumEndpoints);
      } else {
        found_interface = false;
      }
    } else if (desc_type == TUSB_DESC_ENDPOINT && found_interface) {
      tusb_desc_endpoint_t const* ep = (tusb_desc_endpoint_t const*)p_desc;
      if (tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_OUT &&
          (ep->bmAttributes.xfer == TUSB_XFER_BULK)) {
        *ep_out = ep->bEndpointAddress;
        printf("[SWITCH2] Found bulk OUT endpoint: 0x%02X\r\n", *ep_out);
        return true;
      }
    }

    p_desc = tu_desc_next(p_desc);
  }

  printf("[SWITCH2] No bulk OUT endpoint found on interface 1\r\n");
  return false;
}

// Send command via bulk transfer
static bool send_command(uint8_t dev_addr, uint8_t ep_out, const uint8_t* cmd, uint8_t len) {
  if (!usbh_edpt_claim(dev_addr, ep_out)) {
    return false;
  }

  memcpy(switch2_cmd_buf, cmd, len);

  if (!usbh_edpt_xfer(dev_addr, ep_out, switch2_cmd_buf, len)) {
    usbh_edpt_release(dev_addr, ep_out);
    return false;
  }

  return true;
}

// Process input reports
void input_switch2_pro(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  if (len < 12) return;

  uint8_t report_id = report[0];

  // Only process Report ID 0x09
  if (report_id != 0x09) {
    printf("[SWITCH2] Unknown report ID: 0x%02X\r\n", report_id);
    return;
  }

  switch2_instance_t* inst = &switch2_devices[dev_addr].instances[instance];
  switch2_pro_report_t rpt;
  memcpy(&rpt, report, sizeof(rpt) < len ? sizeof(rpt) : len);

  // Unpack 12-bit analog values
  uint16_t left_x = (rpt.left_stick[0] & 0xFF) | ((rpt.left_stick[1] & 0x0F) << 8);
  uint16_t left_y = ((rpt.left_stick[1] & 0xF0) >> 4) | ((rpt.left_stick[2] & 0xFF) << 4);
  uint16_t right_x = (rpt.right_stick[0] & 0xFF) | ((rpt.right_stick[1] & 0x0F) << 8);
  uint16_t right_y = ((rpt.right_stick[1] & 0xF0) >> 4) | ((rpt.right_stick[2] & 0xFF) << 4);

  // Auto-calibrate center on first reports (assumes sticks at rest during connect)
  if (inst->cal_samples < CAL_SAMPLES_NEEDED) {
    // Accumulate samples (first sample initializes, subsequent samples average)
    if (inst->cal_samples == 0) {
      inst->cal_lx.center = left_x;
      inst->cal_ly.center = left_y;
      inst->cal_rx.center = right_x;
      inst->cal_ry.center = right_y;
    } else {
      // Simple averaging
      inst->cal_lx.center = (inst->cal_lx.center + left_x) / 2;
      inst->cal_ly.center = (inst->cal_ly.center + left_y) / 2;
      inst->cal_rx.center = (inst->cal_rx.center + right_x) / 2;
      inst->cal_ry.center = (inst->cal_ry.center + right_y) / 2;
    }
    inst->cal_samples++;

    if (inst->cal_samples >= CAL_SAMPLES_NEEDED) {
      printf("[SWITCH2] Calibrated centers: L(%u,%u) R(%u,%u)\r\n",
             inst->cal_lx.center, inst->cal_ly.center,
             inst->cal_rx.center, inst->cal_ry.center);
    }
    return;  // Skip input during calibration
  }

  // Scale analog sticks using calibrated centers
  // Invert Y: Nintendo uses up=high, HID uses up=low
  uint8_t lx = scale_analog_calibrated(left_x, inst->cal_lx.center);
  uint8_t ly = 255 - scale_analog_calibrated(left_y, inst->cal_ly.center);
  uint8_t rx = scale_analog_calibrated(right_x, inst->cal_rx.center);
  uint8_t ry = 255 - scale_analog_calibrated(right_y, inst->cal_ry.center);

  // Map buttons to JP_BUTTON format
  uint32_t buttons = 0;
  if (rpt.b1) buttons |= JP_BUTTON_B1;  // B (bottom)
  if (rpt.b2) buttons |= JP_BUTTON_B2;  // A (right)
  if (rpt.b3) buttons |= JP_BUTTON_B3;  // Y (left)
  if (rpt.b4) buttons |= JP_BUTTON_B4;  // X (top)
  if (rpt.l1) buttons |= JP_BUTTON_L1;  // L shoulder
  if (rpt.r1) buttons |= JP_BUTTON_R1;  // R shoulder
  if (rpt.l2) buttons |= JP_BUTTON_L2;  // ZL trigger
  if (rpt.r2) buttons |= JP_BUTTON_R2;  // ZR trigger
  if (rpt.s1) buttons |= JP_BUTTON_S1;  // - (select)
  if (rpt.s2) buttons |= JP_BUTTON_S2;  // + (start)
  if (rpt.l3) buttons |= JP_BUTTON_L3;  // Left stick press
  if (rpt.r3) buttons |= JP_BUTTON_R3;  // Right stick press
  if (rpt.du) buttons |= JP_BUTTON_DU;  // D-pad up
  if (rpt.dd) buttons |= JP_BUTTON_DD;  // D-pad down
  if (rpt.dl) buttons |= JP_BUTTON_DL;  // D-pad left
  if (rpt.dr) buttons |= JP_BUTTON_DR;  // D-pad right
  if (rpt.a1) buttons |= JP_BUTTON_A1;  // Home
  if (rpt.a2) buttons |= JP_BUTTON_A2;  // Capture
  if (rpt.a3) buttons |= JP_BUTTON_A3;  // Square button
  if (rpt.l4) buttons |= JP_BUTTON_L4;  // Rear left paddle
  if (rpt.r4) buttons |= JP_BUTTON_R4;  // Rear right paddle

  input_event_t event = {
    .dev_addr = dev_addr,
    .instance = instance,
    .type = INPUT_TYPE_GAMEPAD,
    .transport = INPUT_TRANSPORT_USB,
    .buttons = buttons,
    .button_count = 10,
    .analog = {lx, ly, rx, ry, 128, 0, 0, 128},
    .keys = 0,
  };
  router_submit_input(&event);
}

// Send haptic/rumble output to controller
// Haptic report format (Report ID 0x02, 64 bytes):
//   Byte 0: Report ID (0x02)
//   Byte 1: Counter (0x50-0x5F)
//   Bytes 2-6: Left haptic data (5 bytes)
//   Byte 17: Counter (duplicate)
//   Bytes 18-22: Right haptic data (5 bytes)
// Haptic update interval (ms) - send continuously while rumble active
#define HAPTIC_INTERVAL_MS 50

static void output_rumble(uint8_t dev_addr, uint8_t instance, uint8_t rumble_left, uint8_t rumble_right) {
  switch2_instance_t* inst = &switch2_devices[dev_addr].instances[instance];
  uint32_t now = to_ms_since_boot(get_absolute_time());

  // Check if we need to send:
  // - Always send on change
  // - Send periodically while rumble is active
  bool changed = (inst->rumble_left != rumble_left) || (inst->rumble_right != rumble_right);
  bool active = (rumble_left || rumble_right);
  bool periodic = (active && (now - inst->last_haptic_ms >= HAPTIC_INTERVAL_MS));

  if (!changed && !periodic) {
    return;
  }

  if (changed) {
    printf("[SWITCH2] Rumble: L %d->%d, R %d->%d\r\n",
           inst->rumble_left, rumble_left, inst->rumble_right, rumble_right);
  }
  inst->rumble_left = rumble_left;
  inst->rumble_right = rumble_right;
  inst->last_haptic_ms = now;

  // Build haptic report
  memset(switch2_haptic_buf, 0, sizeof(switch2_haptic_buf));

  switch2_haptic_buf[0] = 0x02;  // Report ID
  switch2_haptic_buf[1] = 0x50 | (haptic_counter & 0x0F);
  switch2_haptic_buf[17] = switch2_haptic_buf[1];  // Duplicate counter
  haptic_counter = (haptic_counter + 1) & 0x0F;

  // Apply minimum perceptible threshold to each motor
  uint8_t left_intensity = rumble_left;
  if (left_intensity > 0 && left_intensity < 64) {
    left_intensity = 64;
  }
  uint8_t right_intensity = rumble_right;
  if (right_intensity > 0 && right_intensity < 64) {
    right_intensity = 64;
  }

  encode_haptic(left_intensity, &switch2_haptic_buf[2]);   // Left motor: bytes 2-6
  encode_haptic(right_intensity, &switch2_haptic_buf[18]); // Right motor: bytes 18-22

  // Send via HID (Report ID 0x02)
  tuh_hid_send_report(dev_addr, instance, 0x02, switch2_haptic_buf + 1, 63);
}

// Send player LED update via bulk endpoint
// LED command format: [0x09, 0x91, 0x00, 0x07, 0x00, 0x08, 0x00, 0x00, pattern, ...]
static void output_player_led(uint8_t dev_addr, uint8_t instance, uint8_t player_index) {
  switch2_instance_t* inst = &switch2_devices[dev_addr].instances[instance];

  // Only send if player LED changed
  if (inst->player_led == player_index) {
    return;
  }

  // Check endpoint is valid
  if (inst->ep_out == 0) {
    printf("[SWITCH2] LED: No bulk endpoint!\r\n");
    return;
  }

  // Check if endpoint is busy from previous transfer
  if (usbh_edpt_busy(dev_addr, inst->ep_out)) {
    // Try again next task cycle
    return;
  }

  printf("[SWITCH2] Player LED: %d -> %d\r\n", inst->player_led, player_index);
  inst->player_led = player_index;

  // Build LED command
  static uint8_t led_cmd[16];
  led_cmd[0] = 0x09;
  led_cmd[1] = 0x91;
  led_cmd[2] = 0x00;
  led_cmd[3] = 0x07;
  led_cmd[4] = 0x00;
  led_cmd[5] = 0x08;
  led_cmd[6] = 0x00;
  led_cmd[7] = 0x00;
  led_cmd[8] = (player_index < 4) ? SWITCH2_LED_PATTERNS[player_index] : 0x01;
  memset(&led_cmd[9], 0, 7);

  // Send via bulk endpoint
  bool sent = send_command(dev_addr, inst->ep_out, led_cmd, 16);
  printf("[SWITCH2] LED send: %s (ep=0x%02X)\r\n", sent ? "OK" : "FAIL", inst->ep_out);
}

// Task function - handles initialization state machine and output
void task_switch2_pro(uint8_t dev_addr, uint8_t instance, device_output_config_t* config) {
  switch2_instance_t* inst = &switch2_devices[dev_addr].instances[instance];

  // Handle rumble and player LED when ready
  if (inst->state == SWITCH2_STATE_READY) {
    output_rumble(dev_addr, instance, config->rumble_left, config->rumble_right);
    output_player_led(dev_addr, instance, config->player_index);
    return;
  }

  // Not in init sequence, nothing to do
  if (inst->state != SWITCH2_STATE_INIT_SEQUENCE) {
    return;
  }

  // Wait for previous transfer
  if (inst->xfer_pending) {
    if (usbh_edpt_busy(dev_addr, inst->ep_out)) {
      return;
    }
    inst->xfer_pending = false;
    inst->cmd_index++;
  }

  // Check if done
  if (inst->cmd_index >= SWITCH2_INIT_CMD_COUNT) {
    printf("[SWITCH2] Initialization complete!\r\n");
    inst->state = SWITCH2_STATE_READY;
    return;
  }

  // Send next command
  uint8_t cmd_len = 0;
  // Use player_index from output config (set by USB output interface feedback)
  uint8_t player_led = (config->player_index >= 0 && config->player_index < 4) ? config->player_index : 0;
  const uint8_t* cmd = get_init_cmd(inst->cmd_index, &cmd_len, player_led);

  if (cmd && cmd_len > 0) {
    printf("[SWITCH2] Sending cmd %d/17: 0x%02X\r\n", inst->cmd_index + 1, cmd[0]);
    if (send_command(dev_addr, inst->ep_out, cmd, cmd_len)) {
      inst->xfer_pending = true;
    }
  } else {
    inst->cmd_index++;
  }
}

// Initialize device
static bool init_switch2_pro(uint8_t dev_addr, uint8_t instance) {
  printf("[SWITCH2] Init dev=%d instance=%d\r\n", dev_addr, instance);

  switch2_instance_t* inst = &switch2_devices[dev_addr].instances[instance];
  memset(inst, 0, sizeof(*inst));

  // Initialize to invalid values so first output triggers a send
  inst->rumble_left = 0xFF;
  inst->rumble_right = 0xFF;
  inst->player_led = 0xFF;

  switch2_devices[dev_addr].instance_count++;

  // Find bulk endpoint
  uint8_t ep_out = 0, itf_num = 0;
  if (!find_bulk_endpoint(dev_addr, &ep_out, &itf_num)) {
    printf("[SWITCH2] Failed to find bulk endpoint\r\n");
    inst->state = SWITCH2_STATE_FAILED;
    return false;
  }

  // Open endpoint
  tusb_desc_endpoint_t ep_desc = {
    .bLength = sizeof(tusb_desc_endpoint_t),
    .bDescriptorType = TUSB_DESC_ENDPOINT,
    .bEndpointAddress = ep_out,
    .bmAttributes = { .xfer = TUSB_XFER_BULK },
    .wMaxPacketSize = 64,
    .bInterval = 0
  };

  if (!tuh_edpt_open(dev_addr, &ep_desc)) {
    printf("[SWITCH2] Failed to open endpoint 0x%02X\r\n", ep_out);
    inst->state = SWITCH2_STATE_FAILED;
    return false;
  }

  printf("[SWITCH2] Opened bulk OUT endpoint 0x%02X\r\n", ep_out);

  inst->ep_out = ep_out;
  inst->itf_num = itf_num;
  inst->state = SWITCH2_STATE_INIT_SEQUENCE;

  return true;
}

// Unmount device
void unmount_switch2_pro(uint8_t dev_addr, uint8_t instance) {
  printf("[SWITCH2] Unmount dev=%d instance=%d\r\n", dev_addr, instance);

  memset(&switch2_devices[dev_addr].instances[instance], 0, sizeof(switch2_instance_t));

  if (switch2_devices[dev_addr].instance_count > 0) {
    switch2_devices[dev_addr].instance_count--;
  }
}

DeviceInterface switch2_pro_interface = {
  .name = "Switch 2 Pro",
  .is_device = is_switch2_pro,
  .init = init_switch2_pro,
  .process = input_switch2_pro,
  .task = task_switch2_pro,
  .unmount = unmount_switch2_pro,
};
