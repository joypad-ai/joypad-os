// logitech_wingman.c
#include "logitech_wingman.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/input_event.h"

// check if device is Logitech WingMan Action controller
static inline bool is_logitech_wingman(uint16_t vid, uint16_t pid) {
  return ((vid == 0x046d && pid == 0xc20b)); // Logitech WingMan Action controller
}

// check if 2 reports are different enough
bool diff_report_logitech_wingman(logitech_wingman_report_t const* rpt1, logitech_wingman_report_t const* rpt2) {
  bool result;

  result |= rpt1->analog_x != rpt2->analog_x;
  result |= rpt1->analog_y != rpt2->analog_y;
  result |= rpt1->analog_z != rpt2->analog_z;
  result |= rpt1->dpad != rpt2->dpad;
  result |= rpt1->a != rpt2->a;
  result |= rpt1->b != rpt2->b;
  result |= rpt1->c != rpt2->c;
  result |= rpt1->x != rpt2->x;
  result |= rpt1->y != rpt2->y;
  result |= rpt1->z != rpt2->z;
  result |= rpt1->l != rpt2->l;
  result |= rpt1->r != rpt2->r;
  result |= rpt1->mode != rpt2->mode;
  result |= rpt1->s != rpt2->s;

  return result;
}

// process usb hid input reports
void process_logitech_wingman(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  uint32_t buttons;
  // previous report used to compare for changes
  static logitech_wingman_report_t prev_report[5] = { 0 };

  logitech_wingman_report_t wingman_report;
  memcpy(&wingman_report, report, sizeof(wingman_report));

  if (diff_report_logitech_wingman(&prev_report[dev_addr-1], &wingman_report)) {
    TU_LOG1("(x, y, z) = (%u, %u, %u)\r\n", wingman_report.analog_x, wingman_report.analog_y, wingman_report.analog_z);
    TU_LOG1("DPad = %d ", wingman_report.dpad);
    if (wingman_report.a) TU_LOG1("A ");
    if (wingman_report.b) TU_LOG1("B ");
    if (wingman_report.c) TU_LOG1("C ");
    if (wingman_report.x) TU_LOG1("X ");
    if (wingman_report.y) TU_LOG1("Y ");
    if (wingman_report.z) TU_LOG1("Z ");
    if (wingman_report.l) TU_LOG1("L ");
    if (wingman_report.r) TU_LOG1("R ");
    if (wingman_report.mode) TU_LOG1("Mode ");
    if (wingman_report.s) TU_LOG1("S ");
    TU_LOG1("\r\n");

    uint8_t analog_x1 = (wingman_report.analog_x == 255) ? 255 : wingman_report.analog_x + 1;
    uint8_t analog_y1 = (wingman_report.analog_y == 0) ? 255 : 255 - wingman_report.analog_y;
    uint8_t analog_x2 = ~wingman_report.analog_z;
    uint8_t analog_y2 = 128;

    bool dpad_up    = (wingman_report.dpad == 0 || wingman_report.dpad == 1 || wingman_report.dpad == 7);
    bool dpad_right = ((wingman_report.dpad >= 1 && wingman_report.dpad <= 3));
    bool dpad_down  = ((wingman_report.dpad >= 3 && wingman_report.dpad <= 5));
    bool dpad_left  = ((wingman_report.dpad >= 5 && wingman_report.dpad <= 7));

    // WingMan Action physical layout (Genesis/Saturn style):
    //   Top row:    [X][Y][Z]  (left to right)
    //   Bottom row: [A][B][C]  (left to right)
    //
    // GP2040-CE canonical mapping (position-based):
    //   Top row:    [B3][B4][R1]
    //   Bottom row: [B1][B2][R2]
    //
    // Mapping: A→B1, B→B2, C→R2, X→B3, Y→B4, Z→R1
    buttons = (((dpad_up)          ? USBR_BUTTON_DU : 0) |
               ((dpad_down)        ? USBR_BUTTON_DD : 0) |
               ((dpad_left)        ? USBR_BUTTON_DL : 0) |
               ((dpad_right)       ? USBR_BUTTON_DR : 0) |
               ((wingman_report.a) ? USBR_BUTTON_B1 : 0) |  // A = left-bottom
               ((wingman_report.b) ? USBR_BUTTON_B2 : 0) |  // B = mid-bottom
               ((wingman_report.x) ? USBR_BUTTON_B3 : 0) |  // X = left-top
               ((wingman_report.y) ? USBR_BUTTON_B4 : 0) |  // Y = mid-top
               ((wingman_report.l) ? USBR_BUTTON_L1 : 0) |  // L shoulder
               ((wingman_report.z) ? USBR_BUTTON_R1 : 0) |  // Z = right-top
               ((0)                ? USBR_BUTTON_L2 : 0) |
               ((wingman_report.c) ? USBR_BUTTON_R2 : 0) |  // C = right-bottom
               ((wingman_report.r) ? USBR_BUTTON_S1 : 0) |
               ((wingman_report.s) ? USBR_BUTTON_S2 : 0) |
               ((0)                ? USBR_BUTTON_L3 : 0) |
               ((0)                ? USBR_BUTTON_R3 : 0) |
               ((0)                ? USBR_BUTTON_A1 : 0));

    // keep analog within range [1-255]
    ensureAllNonZero(&analog_x1, &analog_y1, &analog_x2, &analog_y2);

    // add to accumulator and post to the state machine
    // if a scan from the host machine is ongoing, wait
    input_event_t event = {
      .dev_addr = dev_addr,
      .instance = instance,
      .type = INPUT_TYPE_GAMEPAD,
      .layout = LAYOUT_SEGA_6BUTTON,  // Genesis style: Top [X][Y][Z], Bottom [A][B][C]
      .buttons = buttons,
      .button_count = 7,  // A, B, C, X, Y, Z, L (no R shoulder)
      .analog = {analog_x1, analog_y1, analog_x2, analog_y2, 128, 0, 0, 128},
      .keys = 0,
    };
    router_submit_input(&event);

    prev_report[dev_addr-1] = wingman_report;
  }
}

DeviceInterface logitech_wingman_interface = {
  .name = "Logitech WingMan Action",
  .is_device = is_logitech_wingman,
  .process = process_logitech_wingman,
  .task = NULL,
  .init = NULL
};
