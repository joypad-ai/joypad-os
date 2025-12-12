// hori_pokken.c
#include "hori_pokken.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/input_event.h"

// check if device is HORI Pokken controller for Wii U
static inline bool is_hori_pokken(uint16_t vid, uint16_t pid) {
  return ((vid == 0x0f0d && pid == 0x0092)); // Wii U Pokken
}

// check if 2 reports are different enough
bool diff_report_pokken(hori_pokken_report_t const* rpt1, hori_pokken_report_t const* rpt2) {
  bool result = memcmp(rpt1, rpt2, 3) != 0;

  // x, y, z, rz must different than 2 to be counted
  result |= diff_than_n(rpt1->x_axis, rpt2->x_axis, 2) ||
            diff_than_n(rpt1->y_axis, rpt2->y_axis, 2) ||
            diff_than_n(rpt1->z_axis, rpt2->z_axis, 2) ||
            diff_than_n(rpt1->rz_axis, rpt2->rz_axis, 2);

  return result;
}

// process usb hid input reports
void process_hori_pokken(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  uint32_t buttons;
  // previous report used to compare for changes
  static hori_pokken_report_t prev_report[5][5];

  hori_pokken_report_t update_report;
  memcpy(&update_report, report, sizeof(update_report));

  if (diff_report_pokken(&prev_report[dev_addr-1][instance], &update_report)) {
    uint32_t buttons;
    TU_LOG1("(x, y, z, rz) = (%u, %u %u, %u)\r\n", update_report.x_axis, update_report.y_axis, update_report.z_axis, update_report.rz_axis);
    TU_LOG1("DPad = %d ", update_report.dpad);
    if (update_report.y) TU_LOG1("Y ");
    if (update_report.b) TU_LOG1("B ");
    if (update_report.a) TU_LOG1("A ");
    if (update_report.x) TU_LOG1("X ");
    if (update_report.l) TU_LOG1("L ");
    if (update_report.r) TU_LOG1("R ");
    if (update_report.zl) TU_LOG1("ZL ");
    if (update_report.zr) TU_LOG1("ZR ");
    if (update_report.select) TU_LOG1("Select ");
    if (update_report.start) TU_LOG1("Start ");
    TU_LOG1("\r\n");

    bool dpad_up    = (update_report.dpad == 0 || update_report.dpad == 1 || update_report.dpad == 7);
    bool dpad_right = (update_report.dpad >= 1 && update_report.dpad <= 3);
    bool dpad_down  = (update_report.dpad >= 3 && update_report.dpad <= 5);
    bool dpad_left  = (update_report.dpad >= 5 && update_report.dpad <= 7);

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
               ((update_report.zl)     ? USBR_BUTTON_L2 : 0) |
               ((update_report.zr)     ? USBR_BUTTON_R2 : 0) |
               ((update_report.select) ? USBR_BUTTON_S1 : 0) |
               ((update_report.start)  ? USBR_BUTTON_S2 : 0) |
               ((0)                    ? USBR_BUTTON_L3 : 0) |
               ((0)                    ? USBR_BUTTON_R3 : 0) |
               ((0)                    ? USBR_BUTTON_A1 : 0));

    // HID convention: 0=up, 255=down (no inversion needed)
    // Add 1 to avoid 0 value (ensureAllNonZero handles edge cases)
    uint8_t axis_x = (update_report.x_axis == 255) ? 255 : update_report.x_axis + 1;
    uint8_t axis_y = (update_report.y_axis == 255) ? 255 : update_report.y_axis + 1;
    uint8_t axis_z = (update_report.z_axis == 255) ? 255 : update_report.z_axis + 1;
    uint8_t axis_rz = (update_report.rz_axis == 255) ? 255 : update_report.rz_axis + 1;

    // keep analog within range [1-255]
    ensureAllNonZero(&axis_x, &axis_y, &axis_z, &axis_rz);

    // add to accumulator and post to the state machine
    // if a scan from the host machine is ongoing, wait
    input_event_t event = {
      .dev_addr = dev_addr,
      .instance = instance,
      .type = INPUT_TYPE_GAMEPAD,
      .buttons = buttons,
      .button_count = 8,  // B, A, Y, X, L, R, ZL, ZR
      .analog = {axis_x, axis_y, axis_z, axis_rz, 128, 0, 0, 128},
      .keys = 0,
    };
    router_submit_input(&event);

    prev_report[dev_addr-1][instance] = update_report;
  }
}

DeviceInterface hori_pokken_interface = {
  .name = "HORI Pokken for Wii U",
  .is_device = is_hori_pokken,
  .process = process_hori_pokken,
  .task = NULL,
  .init = NULL
};
