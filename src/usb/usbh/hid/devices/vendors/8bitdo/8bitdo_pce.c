// 8bitdo_pce.c
#include "8bitdo_pce.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/input_event.h"

// check if device is 8BitDo PCE 2.4g controllers
bool is_8bitdo_pce(uint16_t vid, uint16_t pid) {
  return ((vid == 0x0f0d && pid == 0x0138)); // 8BitDo PCE 2.4g
}

// check if 2 reports are different enough
bool diff_report_pce(bitdo_pce_report_t const* rpt1, bitdo_pce_report_t const* rpt2) {
  bool result;

  // x1, y1, x2, y2 must different than 2 to be counted
  result = diff_than_n(rpt1->x1, rpt2->x1, 2) || diff_than_n(rpt1->y1, rpt2->y1, 2) ||
           diff_than_n(rpt1->x2, rpt2->x2, 2) || diff_than_n(rpt1->y2, rpt2->y2, 2);

  // Compare the first 3 bytes of the reports
  result |= memcmp(rpt1, rpt2, 3) != 0;

  return result;
}

// process usb hid input reports
void process_8bitdo_pce(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  uint32_t buttons;
  // previous report used to compare for changes
  static bitdo_pce_report_t prev_report[5] = { 0 };

  bitdo_pce_report_t pce_report;
  memcpy(&pce_report, report, sizeof(pce_report));

  if (diff_report_pce(&prev_report[dev_addr-1], &pce_report)) {
    TU_LOG1("(x1, y1, x2, y2) = (%u, %u, %u, %u)\r\n", pce_report.x1, pce_report.y1, pce_report.x2, pce_report.y2);
    TU_LOG1("DPad = %d ", pce_report.dpad);

    if (pce_report.sel) TU_LOG1("Select ");
    if (pce_report.run) TU_LOG1("Run ");
    if (pce_report.one) TU_LOG1("I ");
    if (pce_report.two) TU_LOG1("II ");

    TU_LOG1("\r\n");

    bool dpad_up    = (pce_report.dpad == 0 || pce_report.dpad == 1 || pce_report.dpad == 7);
    bool dpad_right = (pce_report.dpad >= 1 && pce_report.dpad <= 3);
    bool dpad_down  = (pce_report.dpad >= 3 && pce_report.dpad <= 5);
    bool dpad_left  = (pce_report.dpad >= 5 && pce_report.dpad <= 7);

    buttons = (((dpad_up)        ? USBR_BUTTON_DU : 0) |
               ((dpad_down)      ? USBR_BUTTON_DD : 0) |
               ((dpad_left)      ? USBR_BUTTON_DL : 0) |
               ((dpad_right)     ? USBR_BUTTON_DR : 0) |
               ((pce_report.two) ? USBR_BUTTON_B1 : 0) |
               ((pce_report.one) ? USBR_BUTTON_B2 : 0) |
               ((0)              ? USBR_BUTTON_B3 : 0) |
               ((0)              ? USBR_BUTTON_B4 : 0) |
               ((0)              ? USBR_BUTTON_L1 : 0) |
               ((0)              ? USBR_BUTTON_R1 : 0) |
               ((0)              ? USBR_BUTTON_L2 : 0) |
               ((0)              ? USBR_BUTTON_R2 : 0) |
               ((pce_report.sel) ? USBR_BUTTON_S1 : 0) |
               ((pce_report.run) ? USBR_BUTTON_S2 : 0) |
               ((0)              ? USBR_BUTTON_R3 : 0) |
               ((0)              ? USBR_BUTTON_L3 : 0) |
               ((0)              ? USBR_BUTTON_A1 : 0));

    // add to accumulator and post to the state machine
    // if a scan from the host machine is ongoing, wait
    input_event_t event = {
      .dev_addr = dev_addr,
      .instance = instance,
      .type = INPUT_TYPE_GAMEPAD,
      .buttons = buttons,
      .button_count = 2,  // PCEngine 2-button: I, II
      .analog = {128, 128, 128, 128, 128, 0, 0, 128},
      .keys = 0,
    };
    router_submit_input(&event);

    prev_report[dev_addr-1] = pce_report;
  }
}

DeviceInterface bitdo_pce_interface = {
  .name = "8BitDo PCE 2.4g",
  .is_device = is_8bitdo_pce,
  .process = process_8bitdo_pce,
  .task = NULL,
  .init = NULL
};
