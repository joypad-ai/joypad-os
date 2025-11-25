// 8bitdo_neo.c
#include "8bitdo_neo.h"
#include "core/globals.h"
#include "core/input_event.h"

// check if device is 8BitDo NeoGeo gamepad
bool is_8bitdo_neo(uint16_t vid, uint16_t pid) {
  return ((vid == 0x2dc8 && (pid == 0x9025 || pid == 0x9026))); // 8BitDo NeoGeo 2.4g Receiver
}

// check if 2 reports are different enough
bool diff_report_neo(bitdo_neo_report_t const* rpt1, bitdo_neo_report_t const* rpt2) {
  return true;
}

// process usb hid input reports
void process_8bitdo_neo(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  uint32_t buttons;
  // previous report used to compare for changes
  static bitdo_neo_report_t prev_report[5] = { 0 };

  bitdo_neo_report_t input_report;
  memcpy(&input_report, report, sizeof(input_report));

  if (diff_report_neo(&prev_report[dev_addr-1], &input_report)) {
    // TODO: Parse input_report and call router_submit_input() with INPUT_TYPE_GAMEPAD
    prev_report[dev_addr-1] = input_report;
  }
}

DeviceInterface bitdo_neo_interface = {
  .name = "8BitDo NeoGeo 2.4g",
  .is_device = is_8bitdo_neo,
  .process = process_8bitdo_neo,
  .task = NULL,
  .init = NULL
};
