// sony_psc.c
#include "sony_psc.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/input_event.h"

// check if device is PlayStation Classic controller
bool is_sony_psc(uint16_t vid, uint16_t pid) {
  return ((vid == 0x054c && pid == 0x0cda)); // Sony PSClassic
}

// check if 2 reports are different enough
bool diff_report_psc(sony_psc_report_t const* rpt1, sony_psc_report_t const* rpt2) {
    // Compare the first 2 bytes of the reports
    return memcmp(rpt1, rpt2, sizeof(sony_psc_report_t)-1) != 0;
}

// process usb hid input reports
void process_sony_psc(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  uint32_t buttons;
  // previous report used to compare for changes
  static sony_psc_report_t prev_report[5] = { 0 };

  sony_psc_report_t psc_report;
  memcpy(&psc_report, report, sizeof(psc_report));

  // counter is +1, assign to make it easier to compare 2 report
  prev_report[dev_addr-1].counter = psc_report.counter;

  if (diff_report_psc(&prev_report[dev_addr-1], &psc_report)) {
    TU_LOG1("DPad = %d ", psc_report.dpad);

    if (psc_report.square   ) TU_LOG1("Square ");
    if (psc_report.cross    ) TU_LOG1("Cross ");
    if (psc_report.circle   ) TU_LOG1("Circle ");
    if (psc_report.triangle ) TU_LOG1("Triangle ");
    if (psc_report.l1       ) TU_LOG1("L1 ");
    if (psc_report.r1       ) TU_LOG1("R1 ");
    if (psc_report.l2       ) TU_LOG1("L2 ");
    if (psc_report.r2       ) TU_LOG1("R2 ");
    if (psc_report.share    ) TU_LOG1("Share ");
    if (psc_report.option   ) TU_LOG1("Option ");

    TU_LOG1("\r\n");

    bool dpad_up    = (psc_report.dpad >= 0 && psc_report.dpad <= 2);
    bool dpad_right = (psc_report.dpad == 2 || psc_report.dpad == 6 || psc_report.dpad == 10);
    bool dpad_down  = (psc_report.dpad >= 8 && psc_report.dpad <= 10);
    bool dpad_left  = (psc_report.dpad == 0 || psc_report.dpad == 4 || psc_report.dpad == 8);

    buttons = (((dpad_up)             ? JP_BUTTON_DU : 0) |
               ((dpad_down)           ? JP_BUTTON_DD : 0) |
               ((dpad_left)           ? JP_BUTTON_DL : 0) |
               ((dpad_right)          ? JP_BUTTON_DR : 0) |
               ((psc_report.cross)    ? JP_BUTTON_B1 : 0) |
               ((psc_report.circle)   ? JP_BUTTON_B2 : 0) |
               ((psc_report.square)   ? JP_BUTTON_B3 : 0) |
               ((psc_report.triangle) ? JP_BUTTON_B4 : 0) |
               ((psc_report.l1)       ? JP_BUTTON_L1 : 0) |
               ((psc_report.r1)       ? JP_BUTTON_R1 : 0) |
               ((psc_report.l2)       ? JP_BUTTON_L2 : 0) |
               ((psc_report.r2)       ? JP_BUTTON_R2 : 0) |
               ((psc_report.share)    ? JP_BUTTON_S1 : 0) |
               ((psc_report.option)   ? JP_BUTTON_S2 : 0));

    // add to accumulator and post to the state machine
    // if a scan from the host machine is ongoing, wait
    input_event_t event = {
      .dev_addr = dev_addr,
      .instance = instance,
      .type = INPUT_TYPE_GAMEPAD,
      .buttons = buttons,
      .button_count = 8,  // PSC: Cross, Circle, Square, Triangle, L1, R1, L2, R2
      .analog = {128, 128, 128, 128, 128, 0, 0, 128},
      .keys = 0,
    };
    router_submit_input(&event);

    prev_report[dev_addr-1] = psc_report;
  }
}

DeviceInterface sony_psc_interface = {
  .name = "Sony PlayStation Classic",
  .is_device = is_sony_psc,
  .process = process_sony_psc,
  .task = NULL,
  .init = NULL
};
