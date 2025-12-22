// switch_pro.c
#include "switch_pro.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "pico/time.h"

// Stick calibration data
typedef struct {
  uint16_t center;      // Calibrated center value
} stick_cal_t;

// Switch instance state
typedef struct TU_ATTR_PACKED
{
  bool conn_ack;
  bool baud;
  bool baud_ack;
  bool handshake;
  bool handshake_ack;
  bool usb_enable;
  bool usb_enable_ack;
  bool home_led_set;
  bool full_report_enabled;
  bool imu_enabled;
  bool command_ack;
  uint8_t rumble_left;
  uint8_t rumble_right;
  uint8_t player_led_set;
  // Stick calibration (captured on first reports assuming sticks at rest)
  stick_cal_t cal_lx, cal_ly, cal_rx, cal_ry;
  uint8_t cal_samples;
} switch_instance_t;

// Cached device report properties on mount
typedef struct
{
  switch_instance_t instances[CFG_TUH_HID];
  uint8_t instance_count;
  uint8_t instance_root;
  bool is_pro;

  // Joy-Con Grip merging state (for instance_count > 1)
  input_event_t merged_event;        // Combined input from both Joy-Cons
  bool left_updated;                 // Left Joy-Con has reported
  bool right_updated;                // Right Joy-Con has reported
} switch_device_t;

static switch_device_t switch_devices[MAX_DEVICES] = { 0 };

// Encode HD Rumble data for one motor (4 bytes)
// Format from OGX-Mini (working implementation):
//   Byte 0: Amplitude (0x40-0xC0 range for active, 0x00 for off)
//   Byte 1: HF freq constant (0x88 for active, 0x01 for off)
//   Byte 2: Amplitude/2 (half of byte 0, 0x40 for off)
//   Byte 3: LF freq constant (0x61 for active, 0x40 for off)
// Neutral state: [00 01 40 40]
// intensity: 0 = off, 1-255 = rumble strength
static void encode_rumble(uint8_t intensity, uint8_t* out) {
  if (intensity == 0) {
    // Neutral/off state
    out[0] = 0x00;
    out[1] = 0x01;
    out[2] = 0x40;
    out[3] = 0x40;
    return;
  }

  // Scale intensity to amplitude (exact OGX-Mini formula)
  // ((intensity/255) * 0.8 + 0.5) * (0xC0 - 0x40) + 0x40
  // = ((intensity/255) * 0.8 + 0.5) * 128 + 64
  // Range: 0x80 (at intensity=1) to 0xE6 (at intensity=255)
  uint16_t scaled = ((uint16_t)intensity * 102) / 255 + 64;  // 0.8 * 128 = 102.4
  uint8_t amplitude = (uint8_t)(scaled + 64);  // +0.5*128 + 0x40

  out[0] = amplitude;       // Amplitude
  out[1] = 0x88;            // HF freq constant
  out[2] = amplitude / 2;   // Half amplitude
  out[3] = 0x61;            // LF freq constant
}

// check if device is Nintendo Switch
static inline bool is_switch_pro(uint16_t vid, uint16_t pid)
{
  return ((vid == 0x057e && (
           pid == 0x2009 || // Nintendo Switch Pro
           pid == 0x200e || // JoyCon Charge Grip
           pid == 0x2017    // SNES Controller (NSO)
  )));
}

// check if 2 reports are different enough
bool diff_report_switch_pro(switch_pro_report_t const* rpt1, switch_pro_report_t const* rpt2)
{
  bool result;

  // x, y, z, rz must different than 2 to be counted
  result = diff_than_n(rpt1->left_x, rpt2->left_x, 4) || diff_than_n(rpt1->left_y, rpt2->left_y, 4) ||
           diff_than_n(rpt1->right_x, rpt2->right_x, 4) || diff_than_n(rpt1->right_y, rpt2->right_y, 4);

  // check the reset with mem compare (everything but the sticks)
  result |= memcmp(&rpt1->battery_level_and_connection_info + 1, &rpt2->battery_level_and_connection_info + 1, 3);
  result |= memcmp(&rpt1->subcommand_ack, &rpt2->subcommand_ack, 36);

  return result;
}

// Effective stick range from center (Switch sticks reach ~75-80% of theoretical max)
#define STICK_RANGE 1600
#define CAL_SAMPLES_NEEDED 4

// Legacy scaling for Joy-Cons (uncalibrated, simple linear)
static uint8_t scale_analog_joycon(uint16_t switch_val) {
  if (switch_val == 0) return 1;
  return 1 + ((switch_val - 1) * 255) / 4095;
}

// Scale calibrated analog value to 8-bit (0-255, 128 = center)
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

// resets default values in case devices are hotswapped
void unmount_switch_pro(uint8_t dev_addr, uint8_t instance)
{
  TU_LOG1("SWITCH[%d|%d]: Unmount Reset\r\n", dev_addr, instance);
  switch_devices[dev_addr].instances[instance].conn_ack = false;
  switch_devices[dev_addr].instances[instance].baud = false;
  switch_devices[dev_addr].instances[instance].baud_ack = false;
  switch_devices[dev_addr].instances[instance].handshake = false;
  switch_devices[dev_addr].instances[instance].handshake_ack = false;
  switch_devices[dev_addr].instances[instance].usb_enable = false;
  switch_devices[dev_addr].instances[instance].usb_enable_ack = false;
  switch_devices[dev_addr].instances[instance].home_led_set = false;
  switch_devices[dev_addr].instances[instance].command_ack = true;
  switch_devices[dev_addr].instances[instance].full_report_enabled = false;
  switch_devices[dev_addr].instances[instance].imu_enabled = false;
  switch_devices[dev_addr].instances[instance].rumble_left = 0;
  switch_devices[dev_addr].instances[instance].rumble_right = 0;
  switch_devices[dev_addr].instances[instance].player_led_set = 0xff;
  switch_devices[dev_addr].is_pro = false;

  if (switch_devices[dev_addr].instance_count > 1) {
    switch_devices[dev_addr].instance_count--;
  } else {
    switch_devices[dev_addr].instance_count = 0;
  }

  // if (switch_devices[dev_addr].instance_count == 1 &&
  //     switch_devices[dev_addr].instance_root == instance) {
  //   if (switch_devices[dev_addr].instance_root == 1) {
  //     switch_devices[dev_addr].instance_root = 0;
  //   } else {
  //     switch_devices[dev_addr].instance_root = 1;
  //   }
  // }
}

// prints raw switch pro input report byte data
void print_report_switch_pro(switch_pro_report_01_t* report, uint32_t length)
{
    TU_LOG1("Bytes: ");
    for(uint32_t i = 0; i < length; i++) {
        TU_LOG1("%02X ", report->buf[i]);
    }
    TU_LOG1("\n");
}

// process usb hid input reports
void input_report_switch_pro(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len)
{
  uint32_t buttons;
  // previous report used to compare for changes
  static switch_pro_report_t prev_report[5][5];

  switch_pro_report_t update_report;
  memcpy(&update_report, report, sizeof(update_report));

  if (update_report.report_id == 0x30) // Switch Controller Report
  {
    switch_devices[dev_addr].instances[instance].usb_enable_ack = true;

    update_report.left_x = (update_report.left_stick[0] & 0xFF) | ((update_report.left_stick[1] & 0x0F) << 8);
    update_report.left_y = ((update_report.left_stick[1] & 0xF0) >> 4) | ((update_report.left_stick[2] & 0xFF) << 4);
    update_report.right_x = (update_report.right_stick[0] & 0xFF) | ((update_report.right_stick[1] & 0x0F) << 8);
    update_report.right_y = ((update_report.right_stick[1] & 0xF0) >> 4) | ((update_report.right_stick[2] & 0xFF) << 4);

    // Auto-calibrate center on first reports (Pro controllers only, assumes sticks at rest)
    switch_instance_t* inst = &switch_devices[dev_addr].instances[instance];
    if (switch_devices[dev_addr].is_pro && inst->cal_samples < CAL_SAMPLES_NEEDED) {
      if (inst->cal_samples == 0) {
        inst->cal_lx.center = update_report.left_x;
        inst->cal_ly.center = update_report.left_y;
        inst->cal_rx.center = update_report.right_x;
        inst->cal_ry.center = update_report.right_y;
      } else {
        inst->cal_lx.center = (inst->cal_lx.center + update_report.left_x) / 2;
        inst->cal_ly.center = (inst->cal_ly.center + update_report.left_y) / 2;
        inst->cal_rx.center = (inst->cal_rx.center + update_report.right_x) / 2;
        inst->cal_ry.center = (inst->cal_ry.center + update_report.right_y) / 2;
      }
      inst->cal_samples++;

      if (inst->cal_samples >= CAL_SAMPLES_NEEDED) {
        TU_LOG1("SWITCH[%d|%d]: Calibrated centers: L(%u,%u) R(%u,%u)\r\n",
               dev_addr, instance,
               inst->cal_lx.center, inst->cal_ly.center,
               inst->cal_rx.center, inst->cal_ry.center);
      }
      prev_report[dev_addr-1][instance] = update_report;
      return;  // Skip input during calibration
    }

    if (diff_report_switch_pro(&prev_report[dev_addr-1][instance], &update_report))
    {
      TU_LOG1("SWITCH[%d|%d]: Report ID = 0x%x\r\n", dev_addr, instance, update_report.report_id);
      TU_LOG1("(lx, ly, rx, ry) = (%u, %u, %u, %u)\r\n", update_report.left_x, update_report.left_y, update_report.right_x, update_report.right_y);
      TU_LOG1("DPad = ");

      if (update_report.down) TU_LOG1("Down ");
      if (update_report.up) TU_LOG1("Up ");
      if (update_report.right) TU_LOG1("Right ");
      if (update_report.left ) TU_LOG1("Left ");

      TU_LOG1("; Buttons = ");
      if (update_report.y) TU_LOG1("Y ");
      if (update_report.b) TU_LOG1("B ");
      if (update_report.a) TU_LOG1("A ");
      if (update_report.x) TU_LOG1("X ");
      if (update_report.l) TU_LOG1("L ");
      if (update_report.r) TU_LOG1("R ");
      if (update_report.zl) TU_LOG1("ZL ");
      if (update_report.zr) TU_LOG1("ZR ");
      if (update_report.lstick) TU_LOG1("LStick ");
      if (update_report.rstick) TU_LOG1("RStick ");
      if (update_report.select) TU_LOG1("Select ");
      if (update_report.start) TU_LOG1("Start ");
      if (update_report.home) TU_LOG1("Home ");
      if (update_report.cap) TU_LOG1("Cap ");
      if (update_report.sr_r) TU_LOG1("sr_r ");
      if (update_report.sl_l) TU_LOG1("sl_l ");
      TU_LOG1("\r\n");

      int threshold = 256;
      bool dpad_up    = update_report.up;
      bool dpad_right = update_report.right;
      bool dpad_down  = update_report.down;
      bool dpad_left  = update_report.left;
      bool bttn_b1 = update_report.b;
      bool bttn_b2 = update_report.a;
      bool bttn_b3 = update_report.y;
      bool bttn_b4 = update_report.x;
      bool bttn_l1 = update_report.l;
      bool bttn_r1 = update_report.r;
      bool bttn_s1 = update_report.select;
      bool bttn_s2 = update_report.start;
      bool bttn_a1 = update_report.home;
      bool bttn_a2 = update_report.cap;

      uint8_t leftX = 0;
      uint8_t leftY = 0;
      uint8_t rightX = 0;
      uint8_t rightY = 0;

      if (switch_devices[dev_addr].is_pro) {
        // Use calibrated scaling for Pro controllers
        leftX = scale_analog_calibrated(update_report.left_x, inst->cal_lx.center);
        leftY = 255 - scale_analog_calibrated(update_report.left_y, inst->cal_ly.center);   // Invert Y
        rightX = scale_analog_calibrated(update_report.right_x, inst->cal_rx.center);
        rightY = 255 - scale_analog_calibrated(update_report.right_y, inst->cal_ry.center); // Invert Y
      } else {
        bool is_left_joycon = (!update_report.right_x && !update_report.right_y);
        bool is_right_joycon = (!update_report.left_x && !update_report.left_y);
        if (is_left_joycon) {
          dpad_up    = update_report.up;
          dpad_right = update_report.right;
          dpad_down  = update_report.down;
          dpad_left  = update_report.left;
          bttn_l1 = update_report.l;
          bttn_s2 = false;

          leftX = scale_analog_joycon(update_report.left_x + 127);
          leftY = 255 - scale_analog_joycon(update_report.left_y - 127);  // Invert Y
        }
        else if (is_right_joycon)
        {
          dpad_up    = false; // (right_stick_y > (2048 + threshold));
          dpad_right = false; // (right_stick_x > (2048 + threshold));
          dpad_down  = false; // (right_stick_y < (2048 - threshold));
          dpad_left  = false; // (right_stick_x < (2048 - threshold));
          bttn_a1 = false;

          rightX = scale_analog_joycon(update_report.right_x);
          rightY = 255 - scale_analog_joycon(update_report.right_y + 127);  // Invert Y
        }
      }

      buttons = (((dpad_up)              ? JP_BUTTON_DU : 0) |
                 ((dpad_down)            ? JP_BUTTON_DD : 0) |
                 ((dpad_left)            ? JP_BUTTON_DL : 0) |
                 ((dpad_right)           ? JP_BUTTON_DR : 0) |
                 ((bttn_b1)              ? JP_BUTTON_B1 : 0) |
                 ((bttn_b2)              ? JP_BUTTON_B2 : 0) |
                 ((bttn_b3)              ? JP_BUTTON_B3 : 0) |
                 ((bttn_b4)              ? JP_BUTTON_B4 : 0) |
                 ((bttn_l1)              ? JP_BUTTON_L1 : 0) |
                 ((bttn_r1)              ? JP_BUTTON_R1 : 0) |
                 ((update_report.sr_l || update_report.zl) ? JP_BUTTON_L2 : 0) |
                 ((update_report.sr_r || update_report.zr) ? JP_BUTTON_R2 : 0) |
                 ((bttn_s1)              ? JP_BUTTON_S1 : 0) |
                 ((bttn_s2)              ? JP_BUTTON_S2 : 0) |
                 ((update_report.lstick) ? JP_BUTTON_L3 : 0) |
                 ((update_report.rstick) ? JP_BUTTON_R3 : 0) |
                 ((bttn_a1)              ? JP_BUTTON_A1 : 0) |
                 ((bttn_a2)              ? JP_BUTTON_A2 : 0));

      // Joy-Con Grip merging: combine both Joy-Con inputs into one controller
      if (switch_devices[dev_addr].instance_count > 1) {
        // Multi-instance device (Joy-Con Grip) - merge before submitting
        bool is_left_joycon = (!update_report.right_x && !update_report.right_y);
        bool is_right_joycon = (!update_report.left_x && !update_report.left_y);

        if (is_left_joycon) {
          // Update left Joy-Con portion of merged event
          switch_devices[dev_addr].merged_event.dev_addr = dev_addr;
          switch_devices[dev_addr].merged_event.instance = 0;  // Merged instance
          switch_devices[dev_addr].merged_event.type = INPUT_TYPE_GAMEPAD;

          // Left Joy-Con: D-pad, left stick, L buttons
          uint32_t left_buttons = (((dpad_up)   ? JP_BUTTON_DU : 0) |
                                   ((dpad_down) ? JP_BUTTON_DD : 0) |
                                   ((dpad_left) ? JP_BUTTON_DL : 0) |
                                   ((dpad_right)? JP_BUTTON_DR : 0) |
                                   ((bttn_l1)   ? JP_BUTTON_L1 : 0) |
                                   ((update_report.zl) ? JP_BUTTON_L2 : 0) |
                                   ((update_report.lstick) ? JP_BUTTON_L3 : 0) |
                                   ((bttn_s1)   ? JP_BUTTON_S1 : 0));  // Minus button

          switch_devices[dev_addr].merged_event.buttons |= left_buttons;
          switch_devices[dev_addr].merged_event.analog[0] = leftX;  // Left stick X
          switch_devices[dev_addr].merged_event.analog[1] = leftY;  // Left stick Y
          switch_devices[dev_addr].left_updated = true;
        }
        else if (is_right_joycon) {
          // Update right Joy-Con portion of merged event
          switch_devices[dev_addr].merged_event.dev_addr = dev_addr;
          switch_devices[dev_addr].merged_event.instance = 0;  // Merged instance
          switch_devices[dev_addr].merged_event.type = INPUT_TYPE_GAMEPAD;

          // Right Joy-Con: Face buttons, right stick, R buttons
          uint32_t right_buttons = (((bttn_b1) ? JP_BUTTON_B1 : 0) |
                                    ((bttn_b2) ? JP_BUTTON_B2 : 0) |
                                    ((bttn_b3) ? JP_BUTTON_B3 : 0) |
                                    ((bttn_b4) ? JP_BUTTON_B4 : 0) |
                                    ((bttn_r1) ? JP_BUTTON_R1 : 0) |
                                    ((update_report.zr) ? JP_BUTTON_R2 : 0) |
                                    ((update_report.rstick) ? JP_BUTTON_R3 : 0) |
                                    ((bttn_s2) ? JP_BUTTON_S2 : 0) |  // Plus button
                                    ((bttn_a1) ? JP_BUTTON_A1 : 0) |  // Home button
                                    ((bttn_a2) ? JP_BUTTON_A2 : 0));  // Capture button

          switch_devices[dev_addr].merged_event.buttons |= right_buttons;
          switch_devices[dev_addr].merged_event.analog[2] = rightX;  // Right stick X
          switch_devices[dev_addr].merged_event.analog[3] = rightY;  // Right stick Y
          switch_devices[dev_addr].right_updated = true;
        }

        // Submit merged event only when BOTH Joy-Cons have reported
        if (switch_devices[dev_addr].left_updated && switch_devices[dev_addr].right_updated) {
          router_submit_input(&switch_devices[dev_addr].merged_event);

          // Reset merge state for next frame
          switch_devices[dev_addr].left_updated = false;
          switch_devices[dev_addr].right_updated = false;
          switch_devices[dev_addr].merged_event.buttons = 0x00000000;  // All released
        }
      } else {
        // Single instance device (normal Switch Pro controller)
        input_event_t event = {
          .dev_addr = dev_addr,
          .instance = instance,
          .type = INPUT_TYPE_GAMEPAD,
        .transport = INPUT_TRANSPORT_USB,
        .transport = INPUT_TRANSPORT_USB,
        .transport = INPUT_TRANSPORT_USB,
        .transport = INPUT_TRANSPORT_USB,
          .buttons = buttons,
          .button_count = 10,  // B, A, Y, X, L, R, ZL, ZR, L3, R3
          .analog = {leftX, leftY, rightX, rightY, 128, 0, 0, 128},
          .keys = 0,
        };
        router_submit_input(&event);
      }

      prev_report[dev_addr-1][instance] = update_report;

    }
  }
  else // process input reports for events and command acknowledgments
  {
    switch_pro_report_01_t state_report;
    memcpy(&state_report, report, sizeof(state_report));

    // JC_INPUT_USB_RESPONSE (connection events & command acknowledgments)
    if (state_report.buf[0] == 0x81 && state_report.buf[1] == 0x01) { // JC_USB_CMD_CONN_STATUS
      if (state_report.buf[2] == 0x00) { // connect
        switch_devices[dev_addr].instances[instance].conn_ack = true;
      } else if (state_report.buf[2] == 0x03) { // disconnect
        unmount_switch_pro(dev_addr, instance);
        remove_players_by_address(dev_addr, instance);
      }
    }
    else if (state_report.buf[0] == 0x81 && state_report.buf[1] == 0x02) { // JC_USB_CMD_HANDSHAKE
      switch_devices[dev_addr].instances[instance].handshake_ack = true;
    }
    else if (state_report.buf[0] == 0x81 && state_report.buf[1] == 0x03) { // JC_USB_CMD_BAUDRATE_3M
      switch_devices[dev_addr].instances[instance].baud_ack = true;
    }
    else if (state_report.buf[0] == 0x81 && state_report.buf[1] == 0x92) { // command ack
      switch_devices[dev_addr].instances[instance].command_ack = true;
    }
    else if (state_report.buf[0] == 0x21) {
      switch_devices[dev_addr].instances[instance].command_ack = true;
    }

    TU_LOG1("SWITCH[%d|%d]: Report ID = 0x%x\r\n", dev_addr, instance, state_report.data.report_id);

    uint32_t length = sizeof(state_report.buf) / sizeof(state_report.buf[0]);
    print_report_switch_pro(&state_report, length);
  }

  if (update_report.report_id == 0x81)
  {
    tuh_hid_receive_report(dev_addr, instance);
  }
}

// process usb hid output reports
void output_switch_pro(uint8_t dev_addr, uint8_t instance, device_output_config_t* config)
{
  static uint8_t output_sequence_counter = 0;
  // Nintendo Switch Pro/JoyCons Charging Grip initialization and subcommands (config->rumble|config->leds)
  // See: https://github.com/Dan611/hid-procon/
  //      https://github.com/felis/USB_Host_Shield_2.0/
  //      https://github.com/nicman23/dkms-hid-nintendo/
  //      https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/USB-HID-Notes.md

  if (true/*switch_devices[dev_addr].instances[instance].conn_ack*/) // bug fix for 3rd-party ctrls?
  {
    // set the faster baud rate
    // if (!switch_devices[dev_addr].instances[instance].baud) {
    //   TU_LOG1("SWITCH[%d|%d]: CMD_HID, USB_BAUD\r\n", dev_addr, instance);

    //   uint8_t baud_command[2] = {CMD_HID, SUBCMD_USB_BAUD};

    //   switch_devices[dev_addr].instances[instance].baud = 
    //    tuh_hid_send_report(dev_addr, instance, 0, baud_command, sizeof(baud_command));

    // // wait for baud ask and then send init handshake
    // } else if (!switch_devices[dev_addr].instances[instance].handshake && switch_devices[dev_addr].instances[instance].baud_ack) {
    if (!switch_devices[dev_addr].instances[instance].handshake) {
      TU_LOG1("SWITCH[%d|%d]: CMD_HID, HANDSHAKE\r\n", dev_addr, instance);

      uint8_t handshake_command[2] = {CMD_HID, SUBCMD_HANDSHAKE};

      switch_devices[dev_addr].instances[instance].handshake =
        tuh_hid_send_report(dev_addr, instance, 0, handshake_command, sizeof(handshake_command));

      tuh_hid_receive_report(dev_addr, instance);

    // wait for handshake ack and then send USB enable mode
    } else if (!switch_devices[dev_addr].instances[instance].usb_enable && switch_devices[dev_addr].instances[instance].handshake_ack) {
      TU_LOG1("SWITCH[%d|%d]: CMD_HID, DISABLE_TIMEOUT\r\n", dev_addr, instance);

      uint8_t disable_timeout_cmd[2] = {CMD_HID, SUBCMD_DISABLE_TIMEOUT};

      switch_devices[dev_addr].instances[instance].usb_enable =
        tuh_hid_send_report(dev_addr, instance, 0, disable_timeout_cmd, sizeof(disable_timeout_cmd));

      sleep_ms(100);
      tuh_hid_receive_report(dev_addr, instance);

    // wait for usb enabled acknowledgment
    } else if (switch_devices[dev_addr].instances[instance].usb_enable) {

      uint8_t report[14] = { 0 };
      uint8_t report_size = 10;

      report[0x00] = CMD_RUMBLE_ONLY; // COMMAND
       // Lowest 4-bit is a sequence number, which needs to be increased for every report

      if (!switch_devices[dev_addr].instances[instance].home_led_set) {
        TU_LOG1("SWITCH[%d|%d]: CMD_AND_RUMBLE, CMD_LED_HOME \r\n", dev_addr, instance);
        
        report_size = 14;

        report[0x01] = output_sequence_counter++;
        report[0x00] = CMD_AND_RUMBLE;   // COMMAND
        report[0x0A + 0] = CMD_LED_HOME; // SUB_COMMAND
        
        // SUB_COMMAND ARGS
        report[0x0A + 1] = (0 /* Number of cycles */ << 4) | (true ? 0xF : 0) /* Global mini cycle duration */;
        report[0x0A + 2] = (0x1 /* LED start intensity */ << 4) | 0x0 /* Number of full cycles */;
        report[0x0A + 3] = (0x0 /* Mini Cycle 1 LED intensity */ << 4) | 0x1 /* Mini Cycle 2 LED intensity */;

        // It is possible set up to 15 mini cycles, but we simply just set the LED constantly on after momentary off.
        // See: https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering/blob/master/bluetooth_hid_subcommands_notes.md#subcommand-0x38-set-home-light

        switch_devices[dev_addr].instances[instance].home_led_set = true;
        tuh_hid_send_report(dev_addr, instance, 0, report, report_size);
        sleep_ms(100);

      } else if (!switch_devices[dev_addr].instances[instance].full_report_enabled) {
        TU_LOG1("SWITCH[%d|%d]: CMD_AND_RUMBLE, CMD_MODE, FULL_REPORT_MODE \r\n", dev_addr, instance);

        report_size = 14;

        report[0x01] = output_sequence_counter++;
        report[0x00] = CMD_AND_RUMBLE;              // COMMAND
        report[0x0A + 0] = CMD_MODE;                // SUB_COMMAND
        report[0x0A + 1] = SUBCMD_FULL_REPORT_MODE; // SUB_COMMAND ARGS

        switch_devices[dev_addr].instances[instance].full_report_enabled = true;
        tuh_hid_send_report(dev_addr, instance, 0, report, report_size);
        sleep_ms(100);

      // } else if (!switch_devices[dev_addr].instances[instance].imu_enabled) {
      //   TU_LOG1("SWITCH[%d|%d]: CMD_AND_RUMBLE, CMD_GYRO, 1 \r\n", dev_addr, instance);

      //   report_size = 12;

      //   report[0x00] = CMD_AND_RUMBLE; // COMMAND
      //   report[0x0A + 0] = CMD_GYRO;   // SUB_COMMAND
      //   report[0x0A + 1] = 1 ? 1 : 0;  // SUB_COMMAND ARGS

      //   switch_devices[dev_addr].instances[instance].imu_enabled = true;
      //   tuh_hid_send_report(dev_addr, instance, 0, report, report_size);
      //   sleep_ms(100);

      // } else if (switch_devices[dev_addr].instances[instance].imu_enabled) {
      } else if (switch_devices[dev_addr].instances[instance].full_report_enabled) {
        // Use player_index from USB output interface config
        int player_index = config->player_index;

        if (config->test ||
          switch_devices[dev_addr].instances[instance].player_led_set != player_index
        ) {
          TU_LOG1("SWITCH[%d|%d]: CMD_AND_RUMBLE, CMD_LED, %d (was %d)\r\n",
                  dev_addr, instance, player_index,
                  switch_devices[dev_addr].instances[instance].player_led_set);

          report_size = 12;

          report[0x00] = CMD_AND_RUMBLE; // COMMAND
          report[0x01] = output_sequence_counter++;

          // Include current rumble state in CMD_AND_RUMBLE
          encode_rumble(config->rumble_left, &report[0x02]);       // Left motor
          encode_rumble(config->rumble_right, &report[0x02 + 4]);  // Right motor

          report[0x0A + 0] = CMD_LED;    // SUB_COMMAND

          // SUB_COMMAND ARGS - use PLAYER_LEDS pattern based on player index
          if (player_index >= 0 && player_index < 5) {
            report[0x0A + 1] = PLAYER_LEDS[player_index + 1];
          } else {
            // unassigned - turn all leds on
            report[0x0A + 1] = 0x0f;
          }

          // test mode override
          if (config->test) {
            report[0x0A + 1] = (config->test & 0b00001111);
          }

          switch_devices[dev_addr].instances[instance].player_led_set = player_index;
          switch_devices[dev_addr].instances[instance].rumble_left = config->rumble_left;
          switch_devices[dev_addr].instances[instance].rumble_right = config->rumble_right;

          tuh_hid_send_report(dev_addr, instance, 0, report, report_size);
        }
        else if (switch_devices[dev_addr].instances[instance].rumble_left != config->rumble_left ||
                 switch_devices[dev_addr].instances[instance].rumble_right != config->rumble_right)
        {
          TU_LOG1("SWITCH[%d|%d]: CMD_RUMBLE_ONLY, L=%d R=%d\r\n", dev_addr, instance,
                  config->rumble_left, config->rumble_right);

          report_size = 10;

          report[0x01] = output_sequence_counter++;
          report[0x00] = CMD_RUMBLE_ONLY; // COMMAND

          // Encode rumble with intensity passthrough
          encode_rumble(config->rumble_left, &report[0x02]);       // Left motor
          encode_rumble(config->rumble_right, &report[0x02 + 4]);  // Right motor

          switch_devices[dev_addr].instances[instance].rumble_left = config->rumble_left;
          switch_devices[dev_addr].instances[instance].rumble_right = config->rumble_right;

          tuh_hid_send_report(dev_addr, instance, 0, report, report_size);
        }
      }
    }
  }
}

// process usb hid output reports
void task_switch_pro(uint8_t dev_addr, uint8_t instance, device_output_config_t* config)
{
  const uint32_t interval_ms = 20;
  static uint32_t start_ms = 0;

  uint32_t current_time_ms = to_ms_since_boot(get_absolute_time());
  if (current_time_ms - start_ms >= interval_ms)
  {
    start_ms = current_time_ms;
    output_switch_pro(dev_addr, instance, config);
  }
}

// initialize usb hid input
static inline bool init_switch_pro(uint8_t dev_addr, uint8_t instance)
{
  TU_LOG1("SWITCH[%d|%d]: Mounted\r\n", dev_addr, instance);

  switch_devices[dev_addr].instances[instance].command_ack = true;
  // Initialize to 0xFF so first config comparison triggers output
  switch_devices[dev_addr].instances[instance].rumble_left = 0xFF;
  switch_devices[dev_addr].instances[instance].rumble_right = 0xFF;
  switch_devices[dev_addr].instances[instance].player_led_set = 0xFF;

  if ((++switch_devices[dev_addr].instance_count) == 1) {
    switch_devices[dev_addr].instance_root = instance; // save initial root instance to merge extras into
  }

  uint16_t vid, pid;
  tuh_vid_pid_get(dev_addr, &vid, &pid);
  // Mark controllers with analog sticks as "Pro" for proper scaling
  if (pid == 0x2009) {  // Switch Pro
    switch_devices[dev_addr].is_pro = true;
  }

  return true;
}

DeviceInterface switch_pro_interface = {
  .name = "Switch Pro",
  .is_device = is_switch_pro,
  .process = input_report_switch_pro,
  .task = output_switch_pro,
  .unmount = unmount_switch_pro,
  .init = init_switch_pro,
};
