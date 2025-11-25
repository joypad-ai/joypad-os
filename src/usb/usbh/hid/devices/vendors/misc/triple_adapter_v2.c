// triple_adapter_v2.c
#include "triple_adapter_v2.h"
#include "core/globals.h"
#include "core/input_event.h"

// check if device is TripleController (Arduino based HID)
static inline bool is_triple_adapter_v2(uint16_t vid, uint16_t pid) {
  bool serial_match = false;
  bool vidpid_match = (vid == 0x2341 && pid == 0x8036); // Arduino Leonardo

  if (!vidpid_match) return false;

  // Compare the the fetched serial with "S-NES-GEN-V2" or "NES-NTT-GENESIS"
  // if(memcmp(devices[dev_addr].serial, tplctr_serial_v2, sizeof(tplctr_serial_v2)) == 0 ||
  //    memcmp(devices[dev_addr].serial, tplctr_serial_v2_1, sizeof(tplctr_serial_v2_1)) == 0)
  // {
  //   serial_match = true;
  // }

  return serial_match;
}

// check if 2 reports are different enough
bool diff_report_triple_adapter_v2(triple_adapter_v2_report_t const* rpt1, triple_adapter_v2_report_t const* rpt2) {
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
  result |= rpt1->ntt_0 != rpt2->ntt_0;

  return result;
}

// process usb hid input reports
void process_triple_adapter_v2(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  uint32_t buttons;
  // previous report used to compare for changes
  static triple_adapter_v2_report_t prev_report[5][5];

  triple_adapter_v2_report_t update_report;
  memcpy(&update_report, report, sizeof(update_report));

  if (diff_report_triple_adapter_v2(&prev_report[dev_addr-1][instance], &update_report) )
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

    buttons = (((dpad_up)              ? 0x00 : USBR_BUTTON_DU) |
               ((dpad_down)            ? 0x00 : USBR_BUTTON_DD) |
               ((dpad_left)            ? 0x00 : USBR_BUTTON_DL) |
               ((dpad_right)           ? 0x00 : USBR_BUTTON_DR) |
               ((update_report.b)      ? 0x00 : USBR_BUTTON_B1) |
               ((update_report.a)      ? 0x00 : USBR_BUTTON_B2) |
               ((update_report.y)      ? 0x00 : USBR_BUTTON_B3) |
               ((update_report.x)      ? 0x00 : USBR_BUTTON_B4) |
               ((update_report.select) ? 0x00 : USBR_BUTTON_S1) |
               ((update_report.start)  ? 0x00 : USBR_BUTTON_S2) |
               ((update_report.ntt_0)  ? 0x00 : USBR_BUTTON_L3) |
               ((update_report.ntt_1)  ? 0x00 : USBR_BUTTON_R3) |
               ((update_report.l)      ? 0x00 : USBR_BUTTON_L1) |
               ((update_report.r)      ? 0x00 : USBR_BUTTON_R1) |
               ((update_report.ntt_2)  ? 0x00 : USBR_BUTTON_L2) |
               ((update_report.ntt_3)  ? 0x00 : USBR_BUTTON_R2) |
               ((update_report.ntt_4)  ? 0x00 : USBR_BUTTON_A1) |
               ((update_report.ntt_5)  ? 0x00 : USBR_BUTTON_A2));

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

DeviceInterface triple_adapter_v2_interface = {
  .name = "TripleController Adapter v2",
  .is_device = is_triple_adapter_v2,
  .process = process_triple_adapter_v2,
  .task = NULL,
  .init = NULL
};
