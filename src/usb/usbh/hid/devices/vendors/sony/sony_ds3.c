// sony_ds3.c
#include "sony_ds3.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "pico/time.h"

// TODO: Get these from BTstack when BT dongle is connected
static const uint8_t* btd_get_local_bd_addr(void) {
    static uint8_t dummy_addr[6] = {0};
    return dummy_addr;
}
static bool btd_is_available(void) {
    return false;  // TODO: Check if BTstack has a dongle connected
}
#include "tusb.h"
#include "host/usbh.h"

// DS3 initialization states
typedef enum {
  DS3_STATE_IDLE,           // Not initialized
  DS3_STATE_ACTIVATING,     // Sent activation report 0xF4, waiting for completion
  DS3_STATE_WAIT_ACTIVE,    // Wait for DS3 to become active (receive first input)
  DS3_STATE_SET_BT_ADDR,    // Need to send BT host address report 0xF5
  DS3_STATE_VERIFY_BT_ADDR, // Read back BT address to verify
  DS3_STATE_READY           // Fully initialized
} ds3_state_t;

// DualShock3 instance state
typedef struct TU_ATTR_PACKED
{
  uint8_t rumble;
  uint8_t player;
  ds3_state_t init_state;
  bool bt_addr_sent;        // Have we successfully sent BT address?
  bool input_received;      // Have we received input (DS3 is active)?
  bool button_pressed;      // Has user pressed any button? (for BT pairing trigger)
  bool verify_pending;      // Waiting for GET_REPORT callback?
  uint32_t delay_start;     // For timing delays
} ds3_instance_t;

// Cached device report properties on mount
typedef struct TU_ATTR_PACKED
{
  ds3_instance_t instances[CFG_TUH_HID];
} ds3_device_t;

static ds3_device_t ds3_devices[MAX_DEVICES] = { 0 };

// Special PS3 Controller enable commands
static const uint8_t ds3_init_cmd_buf[4] = {0x42, 0x0c, 0x00, 0x00};

// Report IDs
#define DS3_REPORT_ACTIVATE     0xF4
#define DS3_REPORT_BT_HOST_ADDR 0xF5

// Called from sony_ds4.c when GET_REPORT 0xF5 completes
void ds3_on_get_report_complete(uint8_t dev_addr, uint8_t instance) {
  if (dev_addr < MAX_DEVICES && instance < CFG_TUH_HID) {
    ds3_devices[dev_addr].instances[instance].verify_pending = false;
  }
}

// check if device is Sony PlayStation 3 controllers
bool is_sony_ds3(uint16_t vid, uint16_t pid) {
  return (
    (vid == 0x054c && pid == 0x0268)    // Sony DualShock3
    // HORI
    || (vid == 0x0f0d && pid == 0x0010) // HORI Fighting Stick 3
    || (vid == 0x0f0d && pid == 0x0011) // HORI Real Arcade Pro 3 (RAP 3 SA/SE)
    || (vid == 0x0f0d && pid == 0x0026) // HORI Real Arcade Pro 3 Premium VLX
    || (vid == 0x0f0d && pid == 0x0027) // HORI Fighting Stick V3
    || (vid == 0x0f0d && pid == 0x008b) // HORI RAP V HAYABUSA Controller (PS3 Mode)
    // Mad Catz
    || (vid == 0x0738 && pid == 0x3180) // Mad Catz Fight Stick Alpha (PS3 Mode)
    || (vid == 0x0738 && pid == 0x8818) // Mad Catz SFIV Tournament Edition Round 1 (PS3)
    || (vid == 0x0738 && pid == 0x8838) // Mad Catz SFIV Tournament Edition Round 2 (PS3)
    // Quanba
    || (vid == 0x2c22 && pid == 0x2302) // Qanba Obsidian (PS3 Mode)
    || (vid == 0x2c22 && pid == 0x2500) // Qanba Dragon (PS3 Mode)
    // Other
    || (vid == 0x146b && pid == 0x0904) // Nacon Daija Arcade Stick (PS3 Mode)
    || (vid == 0x1292 && pid == 0x4e47) // Fire NEOGEOX Arcade Stick (PS3 HID Mode)
    || (vid == 0x0079 && pid == 0x0006) // Generic Zero Delay USB Encoder (Standard PC/PS3)
    || (vid == 0x046d && pid == 0xc216) // Logitech F310 (DirectInput / PS3 Mode)
  ); 
}

// check if 2 reports are different enough
bool diff_report_ds3(sony_ds3_report_t const* rpt1, sony_ds3_report_t const* rpt2)
{
  // x, y, z, rz must different than 2 to be counted
  if (diff_than_n(rpt1->lx, rpt2->lx, 2) || diff_than_n(rpt1->ly, rpt2->ly, 2) ||
      diff_than_n(rpt1->rx, rpt2->rx, 2) || diff_than_n(rpt1->ry, rpt2->ry, 2) ||
      diff_than_n(rpt1->pressure[8], rpt2->pressure[8], 2) ||
      diff_than_n(rpt1->pressure[9], rpt2->pressure[9], 2))
  {
    return true;
  }

  // check the rest with mem compare
  if (memcmp(&rpt1->reportId + 1, &rpt2->reportId + 1, 3))
  {
    return true;
  }

  return false;
}

// process input input reports
void input_sony_ds3(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  uint32_t buttons;
  // previous report used to compare for changes
  static sony_ds3_report_t prev_report[5] = { 0 };

  // Mark that we've received input (DS3 is active and ready)
  ds3_devices[dev_addr].instances[instance].input_received = true;

  // Check if any button is pressed (bytes 1 and 2 are button bytes, byte 3 is PS)
  // report[0] is reportId, report[1-3] are button bytes
  if (len >= 4 && (report[1] != 0 || report[2] != 0 || report[3] != 0)) {
    ds3_devices[dev_addr].instances[instance].button_pressed = true;
  }

  uint8_t const report_id = report[0];
  report++;
  len--;

  // all buttons state is stored in ID 1
  if (report_id == 1)
  {
    sony_ds3_report_t ds3_report;
    // Only copy actual report size (48 bytes after report ID stripped), not full struct size
    memcpy(&ds3_report, report, len < sizeof(ds3_report) ? len : sizeof(ds3_report));

    // counter is +1, assign to make it easier to compare 2 report
    prev_report[dev_addr-1].counter = ds3_report.counter;

    // Check if buttons/sticks changed (for debug logging)
    bool buttons_changed = diff_report_ds3(&prev_report[dev_addr-1], &ds3_report);

    // Parse motion data (SIXAXIS)
    // DS3 motion is at bytes 41-48 in original report (1-indexed with report ID at byte 0)
    // After report++ strips report ID, motion is at indices 40-47
    int16_t accel_x = 0, accel_y = 0, accel_z = 0, gyro_z = 0;
    bool has_motion = false;
    if (len >= 48) {
      // DS3 accelerometer: big-endian 16-bit values centered at ~512
      accel_x = (int16_t)((report[40] << 8) | report[41]);
      accel_y = (int16_t)((report[42] << 8) | report[43]);
      accel_z = (int16_t)((report[44] << 8) | report[45]);
      gyro_z  = (int16_t)((report[46] << 8) | report[47]);
      has_motion = true;
    }

    // Always submit events when motion is available, or when buttons/sticks changed
    if (has_motion || buttons_changed)
    {
      uint8_t analog_1x = ds3_report.lx;
      uint8_t analog_1y = ds3_report.ly;  // HID convention: 0=up, 255=down
      uint8_t analog_2x = ds3_report.rx;
      uint8_t analog_2y = ds3_report.ry;  // HID convention: 0=up, 255=down

      // Use L2/R2 pressure sensors for analog trigger values
      // DS3 has digital L2/R2 buttons, but pressure-sensitive sensors
      uint8_t analog_l = ds3_report.pressure[8];  // L2 pressure
      uint8_t analog_r = ds3_report.pressure[9];  // R2 pressure

      if (buttons_changed) {
        TU_LOG1("(lx, ly, rx, ry, l, r) = (%u, %u, %u, %u, %u, %u)\r\n", analog_1x, analog_1y, analog_2x, analog_2y, analog_l, analog_r);
        TU_LOG1("DPad = ");

        if (ds3_report.up       ) TU_LOG1("Up ");
        if (ds3_report.down     ) TU_LOG1("Down ");
        if (ds3_report.left     ) TU_LOG1("Left ");
        if (ds3_report.right    ) TU_LOG1("Right ");

        if (ds3_report.square   ) TU_LOG1("Square ");
        if (ds3_report.cross    ) TU_LOG1("Cross ");
        if (ds3_report.circle   ) TU_LOG1("Circle ");
        if (ds3_report.triangle ) TU_LOG1("Triangle ");

        if (ds3_report.l1       ) TU_LOG1("L1 ");
        if (ds3_report.r1       ) TU_LOG1("R1 ");
        if (ds3_report.l2       ) TU_LOG1("L2 ");
        if (ds3_report.r2       ) TU_LOG1("R2 ");

        if (ds3_report.select   ) TU_LOG1("Select ");
        if (ds3_report.start    ) TU_LOG1("Start ");
        if (ds3_report.l3       ) TU_LOG1("L3 ");
        if (ds3_report.r3       ) TU_LOG1("R3 ");

        if (ds3_report.ps       ) TU_LOG1("PS ");

        TU_LOG1("\r\n");
      }

      // All shoulder buttons passed as digital (platform-agnostic)
      // Consoles handle analog trigger thresholds in their router_submit_input()
      buttons = (((ds3_report.up)       ? JP_BUTTON_DU : 0) |
                 ((ds3_report.down)     ? JP_BUTTON_DD : 0) |
                 ((ds3_report.left)     ? JP_BUTTON_DL : 0) |
                 ((ds3_report.right)    ? JP_BUTTON_DR : 0) |
                 ((ds3_report.cross)    ? JP_BUTTON_B1 : 0) |
                 ((ds3_report.circle)   ? JP_BUTTON_B2 : 0) |
                 ((ds3_report.square)   ? JP_BUTTON_B3 : 0) |
                 ((ds3_report.triangle) ? JP_BUTTON_B4 : 0) |
                 ((ds3_report.l1)       ? JP_BUTTON_L1 : 0) |
                 ((ds3_report.r1)       ? JP_BUTTON_R1 : 0) |
                 ((ds3_report.l2)       ? JP_BUTTON_L2 : 0) |
                 ((ds3_report.r2)       ? JP_BUTTON_R2 : 0) |
                 ((ds3_report.select)   ? JP_BUTTON_S1 : 0) |
                 ((ds3_report.start)    ? JP_BUTTON_S2 : 0) |
                 ((ds3_report.l3)       ? JP_BUTTON_L3 : 0) |
                 ((ds3_report.r3)       ? JP_BUTTON_R3 : 0) |
                 ((ds3_report.ps)       ? JP_BUTTON_A1 : 0));

      // keep analog within range [1-255]
      ensureAllNonZero(&analog_1x, &analog_1y, &analog_2x, &analog_2y);

      // add to accumulator and post to the state machine
      // if a scan from the host machine is ongoing, wait
      input_event_t event = {
        .dev_addr = dev_addr,
        .instance = instance,
        .type = INPUT_TYPE_GAMEPAD,
        .transport = INPUT_TRANSPORT_USB,
        .transport = INPUT_TRANSPORT_USB,
        .transport = INPUT_TRANSPORT_USB,
        .transport = INPUT_TRANSPORT_USB,
        .buttons = buttons,
        .button_count = 10,  // PS3: Cross, Circle, Square, Triangle, L1, R1, L2, R2, L3, R3
        .analog = {analog_1x, analog_1y, analog_2x, analog_2y, analog_l, analog_r},
        .keys = 0,
        .has_motion = has_motion,
        .accel = {accel_x, accel_y, accel_z},
        .gyro = {0, 0, gyro_z},  // DS3 only has Z-axis gyro
        .has_pressure = true,
        // DS3 pressure mapping: struct indices are shifted due to report ID stripping
        // D-pad: up, right, down, left at pressure[4-7]
        // Triggers: L2, R2, L1, R1 at pressure[8-11]
        // Face buttons: triangle, circle, cross, square at unused[0-3]
        .pressure = {
          ds3_report.pressure[4],  // up
          ds3_report.pressure[5],  // right
          ds3_report.pressure[6],  // down
          ds3_report.pressure[7],  // left
          ds3_report.pressure[8],  // L2
          ds3_report.pressure[9],  // R2
          ds3_report.pressure[10], // L1
          ds3_report.pressure[11], // R1
          ds3_report.unused[0],    // triangle
          ds3_report.unused[1],    // circle
          ds3_report.unused[2],    // cross
          ds3_report.unused[3],    // square
        },
      };
      router_submit_input(&event);

      prev_report[dev_addr-1] = ds3_report;
    }
  }
}

// process output report for rumble and player LED assignment
void output_sony_ds3(uint8_t dev_addr, uint8_t instance, device_output_config_t* config) {
  sony_ds3_output_report_01_t output_report = {
    .buf = {
      0x01,
      0x00, 0xff, 0x00, 0xff, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00,
      0xff, 0x27, 0x10, 0x00, 0x32,
      0xff, 0x27, 0x10, 0x00, 0x32,
      0xff, 0x27, 0x10, 0x00, 0x32,
      0xff, 0x27, 0x10, 0x00, 0x32,
      0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    }
  };

  // LED player indicator
  // config->leds contains fb->led.pattern from feedback system (0x01-0x08 for players 1-4)
  // DS3 LED bitmap is shifted left by 1 (0x02, 0x04, 0x08, 0x10 for LEDs 1-4)
  if (config->leds != 0) {
    // Use LED from feedback system (e.g., from host output report)
    output_report.data.leds_bitmap = config->leds << 1;
  } else {
    // Fall back to player index based LED
    switch (config->player_index+1)
    {
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
      output_report.data.leds_bitmap = (PLAYER_LEDS[config->player_index+1] << 1);
      break;

    default: // unassigned
      // turn all LEDs on
      output_report.data.leds_bitmap = (PLAYER_LEDS[10] << 1);

      // make all LEDs dim
      for (int n = 0; n < 4; n++) {
        output_report.data.led[n].duty_length = 0;
        output_report.data.led[n].duty_on = 32;
        output_report.data.led[n].duty_off = 223;
      }
      break;
    }
  }

  // fun
  if (config->player_index+1 && config->test) {
    output_report.data.leds_bitmap = (config->test & 0b00011110);

    // led brightness
    for (int n = 0; n < 4; n++) {
      output_report.data.led[n].duty_length = (config->test & 0x07);
      output_report.data.led[n].duty_on = config->test;
      output_report.data.led[n].duty_off = 255 - config->test;
    }
  }

  if (config->rumble) {
    output_report.data.rumble.right_motor_on = 1;
    output_report.data.rumble.left_motor_force = 128;
    output_report.data.rumble.left_duration = 128;
    output_report.data.rumble.right_duration = 128;
  }

  if (ds3_devices[dev_addr].instances[instance].rumble != config->rumble ||
      ds3_devices[dev_addr].instances[instance].player != output_report.data.leds_bitmap ||
      config->test)
  {
    ds3_devices[dev_addr].instances[instance].rumble = config->rumble;
    ds3_devices[dev_addr].instances[instance].player = output_report.data.leds_bitmap;

    // Send report without the report ID, start at index 1 instead of 0
    tuh_hid_send_report(dev_addr, instance, output_report.data.report_id, &(output_report.buf[1]), sizeof(output_report) - 1);

  }
}

// Static buffer for control transfer (must persist until transfer completes)
static uint8_t ds3_ctrl_buf[16];  // Extra space for report ID prefix
static tusb_control_request_t ds3_ctrl_request;
static volatile bool ds3_xfer_complete = false;
static volatile bool ds3_xfer_success = false;

// Callback for async control transfer
static void ds3_ctrl_xfer_cb(tuh_xfer_t* xfer) {
  printf("[DS3] Control transfer callback: result=%d xferred=%lu\n",
         xfer->result, (unsigned long)xfer->actual_len);
  ds3_xfer_success = (xfer->result == XFER_RESULT_SUCCESS);
  ds3_xfer_complete = true;
}

// Raw USB control transfer for DS3 SET_REPORT (bypasses TinyUSB HID layer)
// USB Host Shield uses: ctrlReq(addr, ep0, 0x21, 0x09, 0xF5, 0x03, 0x00, 8, 8, buf)
static bool ds3_set_report_raw(uint8_t dev_addr, uint8_t instance, uint8_t report_id, uint8_t* data, uint16_t len) {
  printf("[DS3] ds3_set_report_raw called: dev=%d inst=%d report=0x%02X len=%d\n",
         dev_addr, instance, report_id, len);

  // Get interface number from TinyUSB HID layer
  tuh_itf_info_t itf_info;
  bool got_info = tuh_hid_itf_get_info(dev_addr, instance, &itf_info);
  printf("[DS3] tuh_hid_itf_get_info returned: %d\n", got_info);

  uint8_t itf_num = 0;  // Default to interface 0
  if (got_info) {
    itf_num = itf_info.desc.bInterfaceNumber;
  }
  printf("[DS3] Using interface %d for SET_REPORT\n", itf_num);

  // Copy to static buffer
  memcpy(ds3_ctrl_buf, data, len);

  ds3_ctrl_request = (tusb_control_request_t){
    .bmRequestType_bit = {
      .recipient = TUSB_REQ_RCPT_INTERFACE,
      .type = TUSB_REQ_TYPE_CLASS,
      .direction = TUSB_DIR_OUT
    },
    .bRequest = 0x09,  // HID_REQUEST_SET_REPORT
    .wValue = tu_htole16((0x03 << 8) | report_id),  // Feature report (0x03) | report_id
    .wIndex = tu_htole16(itf_num),
    .wLength = len
  };

  ds3_xfer_complete = false;
  ds3_xfer_success = false;

  tuh_xfer_t xfer = {
    .daddr = dev_addr,
    .ep_addr = 0,
    .setup = &ds3_ctrl_request,
    .buffer = ds3_ctrl_buf,
    .buflen = len,
    .complete_cb = ds3_ctrl_xfer_cb,
    .user_data = 0
  };

  printf("[DS3] Control xfer: wValue=0x%04X wIndex=0x%04X wLength=%d\n",
         ds3_ctrl_request.wValue, ds3_ctrl_request.wIndex, ds3_ctrl_request.wLength);

  bool queued = tuh_control_xfer(&xfer);
  printf("[DS3] tuh_control_xfer queued: %d\n", queued);

  if (!queued) {
    return false;
  }

  // Wait for completion with timeout
  uint32_t start = to_ms_since_boot(get_absolute_time());
  while (!ds3_xfer_complete) {
    tuh_task();  // Process USB events
    if (to_ms_since_boot(get_absolute_time()) - start > 1000) {
      printf("[DS3] Control transfer timeout!\n");
      return false;
    }
  }

  printf("[DS3] Control transfer complete: success=%d\n", ds3_xfer_success);
  return ds3_xfer_success;
}

// Static buffer for SET_REPORT - must persist until async transfer completes!
static uint8_t ds3_bt_addr_buf[8];

// Send BT host address to DS3
// This programs the DS3 to connect to our BT dongle when unplugged
static bool ds3_send_bt_host_address(uint8_t dev_addr, uint8_t instance) {
  const uint8_t* bt_addr = btd_get_local_bd_addr();
  if (!bt_addr) {
    printf("[DS3] No BT dongle address available\n");
    return false;
  }

  // Format: [0x01, 0x00, MAC[0-5]] - 8 bytes total
  // MUST use static buffer - tuh_hid_set_report is async!
  // BD_ADDR from btd is in HCI order (little-endian), DS3 needs network order (reversed)
  ds3_bt_addr_buf[0] = 0x01;
  ds3_bt_addr_buf[1] = 0x00;
  ds3_bt_addr_buf[2] = bt_addr[5];
  ds3_bt_addr_buf[3] = bt_addr[4];
  ds3_bt_addr_buf[4] = bt_addr[3];
  ds3_bt_addr_buf[5] = bt_addr[2];
  ds3_bt_addr_buf[6] = bt_addr[1];
  ds3_bt_addr_buf[7] = bt_addr[0];

  printf("[DS3] Programming BT host: %02X:%02X:%02X:%02X:%02X:%02X\n",
         bt_addr[5], bt_addr[4], bt_addr[3],
         bt_addr[2], bt_addr[1], bt_addr[0]);

  return tuh_hid_set_report(dev_addr, instance, DS3_REPORT_BT_HOST_ADDR,
                            HID_REPORT_TYPE_FEATURE, ds3_bt_addr_buf, sizeof(ds3_bt_addr_buf));
}

// initialize usb hid input
static inline bool init_sony_ds3(uint8_t dev_addr, uint8_t instance) {
  /*
  * The Sony Sixaxis does not handle HID Output Reports on the
  * Interrupt EP like it could, so we need to force HID Output
  * Reports to use tuh_hid_set_report on the Control EP.
  *
  * There is also another issue about HID Output Reports via USB,
  * the Sixaxis does not want the report_id as part of the data
  * packet, so we have to discard buf[0] when sending the actual
  * control message, even for numbered reports, humpf!
  */
  printf("[DS3] Init..\n");

  // Initialize state
  ds3_devices[dev_addr].instances[instance].init_state = DS3_STATE_ACTIVATING;
  ds3_devices[dev_addr].instances[instance].bt_addr_sent = false;
  ds3_devices[dev_addr].instances[instance].input_received = false;
  ds3_devices[dev_addr].instances[instance].button_pressed = false;

  // Send activation report (0xF4) to enable input streaming
  // BT address will be set after first output report is sent
  return tuh_hid_set_report(dev_addr, instance, DS3_REPORT_ACTIVATE,
                            HID_REPORT_TYPE_FEATURE, (void *)ds3_init_cmd_buf, sizeof(ds3_init_cmd_buf));
}

// process usb hid output reports
void task_sony_ds3(uint8_t dev_addr, uint8_t instance, device_output_config_t* config) {
  ds3_instance_t* inst = &ds3_devices[dev_addr].instances[instance];

  // Handle init state machine
  switch (inst->init_state) {
    case DS3_STATE_ACTIVATING:
      // Once we get input, prompt user and wait for button press
      if (inst->input_received) {
        // Only prompt if BT dongle address is available
        if (btd_is_available()) {
          printf("[DS3] Press any button to pair with BT dongle...\n");
          inst->init_state = DS3_STATE_WAIT_ACTIVE;
        } else {
          // No BT dongle, skip pairing
          inst->init_state = DS3_STATE_READY;
        }
      }
      break;

    case DS3_STATE_WAIT_ACTIVE:
      // Wait until user presses a button
      if (inst->button_pressed) {
        // Read current address before setting
        extern uint8_t* ds3_get_verify_buffer(void);
        if (tuh_hid_get_report(dev_addr, instance, DS3_REPORT_BT_HOST_ADDR,
                               HID_REPORT_TYPE_FEATURE, ds3_get_verify_buffer(), 8)) {
          inst->verify_pending = true;
        }
        inst->init_state = DS3_STATE_SET_BT_ADDR;
      }
      break;

    case DS3_STATE_SET_BT_ADDR:
      // Wait for GET_REPORT to complete before sending SET_REPORT
      if (inst->verify_pending) {
        break;
      }

      // Set BT address (only once)
      if (!inst->bt_addr_sent) {
        if (ds3_send_bt_host_address(dev_addr, instance)) {
          inst->bt_addr_sent = true;
        }
      }
      inst->init_state = DS3_STATE_READY;
      break;

    case DS3_STATE_VERIFY_BT_ADDR:
      // Skip verification for now - it conflicts with SET_REPORT
      inst->init_state = DS3_STATE_READY;
      break;

    case DS3_STATE_READY:
      // Normal operation - handle output reports
      break;

    default:
      break;
  }

  // Throttle output reports
  const uint32_t interval_ms = 20;
  static uint32_t start_ms = 0;

  uint32_t current_time_ms = to_ms_since_boot(get_absolute_time());
  if (current_time_ms - start_ms >= interval_ms) {
    start_ms = current_time_ms;
    output_sony_ds3(dev_addr, instance, config);
  }
}

// resets default values in case devices are hotswapped
void unmount_sony_ds3(uint8_t dev_addr, uint8_t instance)
{
  ds3_devices[dev_addr].instances[instance].rumble = 0;
  ds3_devices[dev_addr].instances[instance].player = 0xff;
  ds3_devices[dev_addr].instances[instance].init_state = DS3_STATE_IDLE;
  ds3_devices[dev_addr].instances[instance].bt_addr_sent = false;
  ds3_devices[dev_addr].instances[instance].input_received = false;
  ds3_devices[dev_addr].instances[instance].button_pressed = false;
}

DeviceInterface sony_ds3_interface = {
  .name = "Sony DualShock 3",
  .init = init_sony_ds3,
  .is_device = is_sony_ds3,
  .process = input_sony_ds3,
  .task = task_sony_ds3,
  .unmount = unmount_sony_ds3,
};
