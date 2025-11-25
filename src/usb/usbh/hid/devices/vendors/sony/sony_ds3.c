// sony_ds3.c
#include "sony_ds3.h"
#include "core/globals.h"
#include "core/input_event.h"
#include "pico/time.h"

// DualSense instance state
typedef struct TU_ATTR_PACKED
{
  uint8_t rumble;
  uint8_t player;
} ds3_instance_t;

// Cached device report properties on mount
typedef struct TU_ATTR_PACKED
{
  ds3_instance_t instances[CFG_TUH_HID];
} ds3_device_t;

static ds3_device_t ds3_devices[MAX_DEVICES] = { 0 };

// Special PS3 Controller enable commands
const uint8_t ds3_init_cmd_buf[4] = {0x42, 0x0c, 0x00, 0x00};

// check if device is Sony PlayStation 3 controllers
bool is_sony_ds3(uint16_t vid, uint16_t pid) {
  return ((vid == 0x054c && pid == 0x0268)); // Sony DualShock3
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

  uint8_t const report_id = report[0];
  report++;
  len--;

  // all buttons state is stored in ID 1
  if (report_id == 1)
  {
    sony_ds3_report_t ds3_report;
    memcpy(&ds3_report, report, sizeof(ds3_report));

    // counter is +1, assign to make it easier to compare 2 report
    prev_report[dev_addr-1].counter = ds3_report.counter;

    // only print if changes since it is polled ~ 5ms
    // Since count+1 after each report and  x, y, z, rz fluctuate within 1 or 2
    // We need more than memcmp to check if report is different enough
    if ( diff_report_ds3(&prev_report[dev_addr-1], &ds3_report) )
    {
      uint8_t analog_1x = ds3_report.lx;
      uint8_t analog_1y = 255 - ds3_report.ly;
      uint8_t analog_2x = ds3_report.rx;
      uint8_t analog_2y = 255 - ds3_report.ry;

      // Use L2/R2 pressure sensors for analog trigger values
      // DS3 has digital L2/R2 buttons, but pressure-sensitive sensors
      uint8_t analog_l = ds3_report.pressure[8];  // L2 pressure
      uint8_t analog_r = ds3_report.pressure[9];  // R2 pressure

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

      // All shoulder buttons passed as digital (platform-agnostic)
      // Consoles handle analog trigger thresholds in their router_submit_input()
      buttons = (((ds3_report.up)       ? 0x00 : USBR_BUTTON_DU) |
                 ((ds3_report.down)     ? 0x00 : USBR_BUTTON_DD) |
                 ((ds3_report.left)     ? 0x00 : USBR_BUTTON_DL) |
                 ((ds3_report.right)    ? 0x00 : USBR_BUTTON_DR) |
                 ((ds3_report.cross)    ? 0x00 : USBR_BUTTON_B1) |
                 ((ds3_report.circle)   ? 0x00 : USBR_BUTTON_B2) |
                 ((ds3_report.square)   ? 0x00 : USBR_BUTTON_B3) |
                 ((ds3_report.triangle) ? 0x00 : USBR_BUTTON_B4) |
                 ((ds3_report.l1)       ? 0x00 : USBR_BUTTON_L1) |
                 ((ds3_report.r1)       ? 0x00 : USBR_BUTTON_R1) |
                 ((ds3_report.l2)       ? 0x00 : USBR_BUTTON_L2) |
                 ((ds3_report.r2)       ? 0x00 : USBR_BUTTON_R2) |
                 ((ds3_report.select)   ? 0x00 : USBR_BUTTON_S1) |
                 ((ds3_report.start)    ? 0x00 : USBR_BUTTON_S2) |
                 ((ds3_report.l3)       ? 0x00 : USBR_BUTTON_L3) |
                 ((ds3_report.r3)       ? 0x00 : USBR_BUTTON_R3) |
                 ((ds3_report.ps)       ? 0x00 : USBR_BUTTON_A1));

      // keep analog within range [1-255]
      ensureAllNonZero(&analog_1x, &analog_1y, &analog_2x, &analog_2y);

      // add to accumulator and post to the state machine
      // if a scan from the host machine is ongoing, wait
      input_event_t event = {
        .dev_addr = dev_addr,
        .instance = instance,
        .type = INPUT_TYPE_GAMEPAD,
        .buttons = buttons,
        .button_count = 10,  // PS3: Cross, Circle, Square, Triangle, L1, R1, L2, R2, L3, R3
        .analog = {analog_1x, analog_1y, analog_2x, analog_2y, 128, analog_l, analog_r, 128},
        .keys = 0,
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

  // led player indicator
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
    // turn all config->leds on
    output_report.data.leds_bitmap = (PLAYER_LEDS[10] << 1);

    // make all config->leds dim
    for (int n = 0; n < 4; n++) {
      output_report.data.led[n].duty_length = 0;
      output_report.data.led[n].duty_on = 32;
      output_report.data.led[n].duty_off = 223;
    }
    break;
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
  TU_LOG1("PS3 Init..\n");

  // Send a Set Report request to the control endpoint
  return tuh_hid_set_report(dev_addr, instance, 0xF4, HID_REPORT_TYPE_FEATURE, (void *)(ds3_init_cmd_buf), sizeof(ds3_init_cmd_buf));
}

// process usb hid output reports
void task_sony_ds3(uint8_t dev_addr, uint8_t instance, device_output_config_t* config) {
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
}

DeviceInterface sony_ds3_interface = {
  .name = "Sony DualShock 3",
  .init = init_sony_ds3,
  .is_device = is_sony_ds3,
  .process = input_sony_ds3,
  .task = output_sony_ds3, // skips throttle task to resolve delayed responses
  .unmount = unmount_sony_ds3,
};
