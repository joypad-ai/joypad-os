// sega_astrocity.c
#include "sega_astrocity.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/input_event.h"

// check if device is Sega Astro City mini controller
static inline bool is_sega_astrocity(uint16_t vid, uint16_t pid) {
  return ((vid == 0x0ca3 && (
           pid == 0x0028 || // Astro City mini joystick
           pid == 0x0027 || // Astro City mini controller
           pid == 0x0024    // 8BitDo M30 6-button controller (2.4g)
         )));
}

// check if 2 reports are different enough
bool diff_report_sega_astrocity(sega_astrocity_report_t const* rpt1, sega_astrocity_report_t const* rpt2) {
  bool result;

  result |= rpt1->x != rpt2->x;
  result |= rpt1->y != rpt2->y;
  result |= rpt1->a != rpt2->a;
  result |= rpt1->b != rpt2->b;
  result |= rpt1->c != rpt2->c;
  result |= rpt1->d != rpt2->d;
  result |= rpt1->e != rpt2->e;
  result |= rpt1->f != rpt2->f;
  result |= rpt1->l != rpt2->l;
  result |= rpt1->r != rpt2->r;
  result |= rpt1->credit != rpt2->credit;
  result |= rpt1->start != rpt2->start;

  return result;
}

// process usb hid input reports
void process_sega_astrocity(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  uint32_t buttons;
  // previous report used to compare for changes
  static sega_astrocity_report_t prev_report[5] = { 0 };

  sega_astrocity_report_t astro_report;
  memcpy(&astro_report, report, sizeof(astro_report));

  if (diff_report_sega_astrocity(&prev_report[dev_addr-1], &astro_report)) {
    TU_LOG1("DPad = x:%d, y:%d ", astro_report.x, astro_report.y);
    if (astro_report.a) TU_LOG1("A "); // X   <-M30 buttons
    if (astro_report.b) TU_LOG1("B "); // Y
    if (astro_report.c) TU_LOG1("C "); // Z
    if (astro_report.d) TU_LOG1("D "); // A
    if (astro_report.e) TU_LOG1("E "); // B
    if (astro_report.f) TU_LOG1("F "); // C
    if (astro_report.l) TU_LOG1("L ");
    if (astro_report.r) TU_LOG1("R ");
    if (astro_report.credit) TU_LOG1("Credit "); // Select
    if (astro_report.start) TU_LOG1("Start ");
    TU_LOG1("\r\n");

    bool dpad_up    = (astro_report.y < 127);
    bool dpad_right = (astro_report.x > 127);
    bool dpad_down  = (astro_report.y > 127);
    bool dpad_left  = (astro_report.x < 127);

    // Astrocity physical layout:
    //   Top row:    [A][B][C]  (left to right)
    //   Bottom row: [D][E][F]  (left to right)
    //
    // GP2040-CE canonical mapping (position-based):
    //   Top row:    [B3][B4][R1]
    //   Bottom row: [B1][B2][R2]
    //
    // Mapping: D→B1, E→B2, F→R2, A→B3, B→B4, C→R1
    buttons = (((dpad_up)             ? USBR_BUTTON_DU : 0) |
               ((dpad_down)           ? USBR_BUTTON_DD : 0) |
               ((dpad_left)           ? USBR_BUTTON_DL : 0) |
               ((dpad_right)          ? USBR_BUTTON_DR : 0) |
               ((astro_report.d)      ? USBR_BUTTON_B1 : 0) |  // D = left-bottom
               ((astro_report.e)      ? USBR_BUTTON_B2 : 0) |  // E = mid-bottom
               ((astro_report.a)      ? USBR_BUTTON_B3 : 0) |  // A = left-top
               ((astro_report.b)      ? USBR_BUTTON_B4 : 0) |  // B = mid-top
               ((astro_report.l)      ? USBR_BUTTON_L1 : 0) |  // L shoulder
               ((astro_report.c)      ? USBR_BUTTON_R1 : 0) |  // C = right-top
               ((0)                   ? USBR_BUTTON_L2 : 0) |  // No L2
               ((astro_report.f)      ? USBR_BUTTON_R2 : 0) |  // F = right-bottom
               ((astro_report.credit) ? USBR_BUTTON_S1 : 0) |
               ((astro_report.start)  ? USBR_BUTTON_S2 : 0) |
               ((0)                   ? USBR_BUTTON_L3 : 0) |
               ((0)                   ? USBR_BUTTON_R3 : 0) |
               ((0)                   ? USBR_BUTTON_A1 : 0));

    // add to accumulator and post to the state machine
    // if a scan from the host machine is ongoing, wait
    input_event_t event = {
      .dev_addr = dev_addr,
      .instance = instance,
      .type = INPUT_TYPE_GAMEPAD,
      .layout = LAYOUT_ASTROCITY,  // 6-button: Top [A][B][C], Bottom [D][E][F]
      .buttons = buttons,
      .button_count = 7,  // A, B, C, D, E, F, L (no R shoulder)
      .analog = {128, 128, 128, 128, 128, 0, 0, 128},
      .keys = 0,
    };
    router_submit_input(&event);

    prev_report[dev_addr-1] = astro_report;
  }
}

DeviceInterface sega_astrocity_interface = {
  .name = "Sega Astro City Mini",
  .is_device = is_sega_astrocity,
  .process = process_sega_astrocity,
  .task = NULL,
  .init = NULL
};
