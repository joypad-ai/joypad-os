// triple_adapter_v1.c
#include "triple_adapter_v1.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/input_event.h"

// check if device is TripleController (Arduino based HID)
static inline bool is_triple_adapter_v1(uint16_t vid, uint16_t pid) {
  bool serial_match = false;
  bool vidpid_match = (vid == 0x2341 && pid == 0x8036); // Arduino Leonardo

  if (!vidpid_match) return false;

  // Compare the the fetched serial with "NES-SNES-GENESIS"
  // if(memcmp(devices[dev_addr].serial, tplctr_serial_v1, sizeof(tplctr_serial_v1)) == 0)
  // {
  //   serial_match = true;
  // }

  return serial_match;
}

// check if 2 reports are different enough
bool diff_report_triple_adapter_v1(triple_adapter_v1_report_t const* rpt1, triple_adapter_v1_report_t const* rpt2) {
  bool result;

  result |= rpt1->axis_x != rpt2->axis_x;
  result |= rpt1->axis_y != rpt2->axis_y;
  result |= rpt1->b != rpt2->b;
  result |= rpt1->a != rpt2->a;
  result |= rpt1->y != rpt2->y;
  result |= rpt1->x != rpt2->x;
  result |= rpt1->l != rpt2->l;
  result |= rpt1->r != rpt2->r;
  result |= rpt1->select != rpt2->select;
  result |= rpt1->start != rpt2->start;
  result |= rpt1->home != rpt2->home;

  return result;
}

// process usb hid input reports
void process_triple_adapter_v1(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  uint32_t buttons;
  // previous report used to compare for changes
  static triple_adapter_v1_report_t prev_report[5][5];

  triple_adapter_v1_report_t update_report;
  memcpy(&update_report, report, sizeof(update_report));

  if (diff_report_triple_adapter_v1(&prev_report[dev_addr-1][instance], &update_report) )
  {
    TU_LOG1("(x, y) = (%u, %u)\r\n", update_report.axis_x, update_report.axis_y);
    if (update_report.b) TU_LOG1("B ");
    if (update_report.a) TU_LOG1("A ");
    if (update_report.y) TU_LOG1("Y ");
    if (update_report.x) TU_LOG1("X ");
    if (update_report.l) TU_LOG1("L ");
    if (update_report.r) TU_LOG1("R ");
    if (update_report.select) TU_LOG1("Select ");
    if (update_report.start) TU_LOG1("Start ");
    TU_LOG1("\r\n");

    int threshold = 28;
    bool dpad_up    = update_report.axis_y ? (update_report.axis_y > (128 - threshold)) : 0;
    bool dpad_right = update_report.axis_x ? (update_report.axis_x < (128 + threshold)) : 0;
    bool dpad_down  = update_report.axis_y ? (update_report.axis_y < (128 + threshold)) : 0;
    bool dpad_left  = update_report.axis_x ? (update_report.axis_x > (128 - threshold)) : 0;

    buttons = (((dpad_up)              ? USBR_BUTTON_DU : 0) |
               ((dpad_down)            ? USBR_BUTTON_DD : 0) |
               ((dpad_left)            ? USBR_BUTTON_DL : 0) |
               ((dpad_right)           ? USBR_BUTTON_DR : 0) |
               ((update_report.b)      ? USBR_BUTTON_B1 : 0) |
               ((update_report.a)      ? USBR_BUTTON_B2 : 0) |
               ((update_report.y)      ? USBR_BUTTON_B3 : 0) |
               ((update_report.x)      ? USBR_BUTTON_B4 : 0) |
               ((update_report.l)      ? USBR_BUTTON_L1 : 0) |
               ((update_report.r)      ? USBR_BUTTON_R1 : 0) |
               ((0)                    ? USBR_BUTTON_L2 : 0) |
               ((0)                    ? USBR_BUTTON_R2 : 0) |
               ((update_report.select) ? USBR_BUTTON_S1 : 0) |
               ((update_report.start)  ? USBR_BUTTON_S2 : 0) |
               ((0)                    ? USBR_BUTTON_L3 : 0) |
               ((0)                    ? USBR_BUTTON_R3 : 0) |
               ((0)                    ? USBR_BUTTON_A1 : 0));

    // add to accumulator and post to the state machine
    // if a scan from the host machine is ongoing, wait
    input_event_t event = {
      .dev_addr = dev_addr,
      .instance = instance,
      .type = INPUT_TYPE_GAMEPAD,
      .buttons = buttons,
      .button_count = 6,  // B, A, Y, X, L, R (SNES-style)
      .analog = {128, 128, 128, 128, 128, 0, 0, 128},
      .keys = 0,
    };
    router_submit_input(&event);

    prev_report[dev_addr-1][instance] = update_report;
  }
}

DeviceInterface triple_adapter_v1_interface = {
  .name = "TripleController Adapter v1",
  .is_device = is_triple_adapter_v1,
  .process = process_triple_adapter_v1,
  .task = NULL,
  .init = NULL
};
