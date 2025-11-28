// 8bitdo_m30.c
#include "8bitdo_m30.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/input_event.h"

// check if device is 8BitDo Bluetooth gamepad (D-input)
bool is_8bitdo_m30(uint16_t vid, uint16_t pid) {
  return ((vid == 0x2dc8 && (
    pid == 0x5006 || // 8BitDo M30 Bluetooth
    pid == 0x3104    // 8BitDo Bluetooth Adapter (Gray)
  )));
}

// check if 2 reports are different enough
bool diff_report_m30(bitdo_m30_report_t const* rpt1, bitdo_m30_report_t const* rpt2) {
  return memcmp(rpt1, rpt2, 7) != 0;
}

// process usb hid input reports
void process_8bitdo_m30(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  uint32_t buttons;
  // previous report used to compare for changes
  static bitdo_m30_report_t prev_report[5] = { 0 };

  bitdo_m30_report_t input_report;
  memcpy(&input_report, report, sizeof(input_report));

  if (diff_report_m30(&prev_report[dev_addr-1], &input_report)) {
    TU_LOG1("(x1, y1, x2, y2) = (%u, %u, %u, %u)\r\n", input_report.x1, input_report.y1, input_report.x2, input_report.y2);
    TU_LOG1("DPad = %d ", input_report.dpad);

    if (input_report.a) TU_LOG1("A ");
    if (input_report.b) TU_LOG1("B ");
    if (input_report.r) TU_LOG1("R (C) ");
    if (input_report.x) TU_LOG1("X ");
    if (input_report.y) TU_LOG1("Y ");
    if (input_report.l) TU_LOG1("L (Z) ");
    if (input_report.l2) TU_LOG1("L2 ");
    if (input_report.r2) TU_LOG1("R2 ");
    if (input_report.l3) TU_LOG1("L3 ");
    if (input_report.r3) TU_LOG1("R3 ");
    if (input_report.cap) TU_LOG1("Capture ");
    if (input_report.select) TU_LOG1("Select ");
    if (input_report.start) TU_LOG1("Start ");
    if (input_report.home) TU_LOG1("Home ");

    TU_LOG1("\r\n");
    bool dpad_up    = (input_report.dpad == 0 || input_report.dpad == 1 || input_report.dpad == 7);
    bool dpad_right = (input_report.dpad >= 1 && input_report.dpad <= 3);
    bool dpad_down  = (input_report.dpad >= 3 && input_report.dpad <= 5);
    bool dpad_left  = (input_report.dpad >= 5 && input_report.dpad <= 7);

    // M30 physical layout (Genesis/Saturn style):
    //   Top row:    [X][Y][Z(L)]  (left to right)
    //   Bottom row: [A][B][C(R)]  (left to right)
    //
    // GP2040-CE canonical mapping (position-based):
    //   Top row:    [B3][B4][R1]
    //   Bottom row: [B1][B2][R2]
    //
    // Mapping: A→B1, B→B2, C(R)→R2, X→B3, Y→B4, Z(L)→R1
    // Physical layout: 6 face + L2/R2 shoulders + L3/R3 sticks + Capture
    buttons = (((dpad_up)             ? USBR_BUTTON_DU : 0) |
               ((dpad_down)           ? USBR_BUTTON_DD : 0) |
               ((dpad_left)           ? USBR_BUTTON_DL : 0) |
               ((dpad_right)          ? USBR_BUTTON_DR : 0) |
               ((input_report.a)      ? USBR_BUTTON_B1 : 0) |  // A = left-bottom
               ((input_report.b)      ? USBR_BUTTON_B2 : 0) |  // B = mid-bottom
               ((input_report.x)      ? USBR_BUTTON_B3 : 0) |  // X = left-top
               ((input_report.y)      ? USBR_BUTTON_B4 : 0) |  // Y = mid-top
               ((input_report.l)      ? USBR_BUTTON_L1 : 0) |  // Z(L) = right-top (L1 position)
               ((input_report.r)      ? USBR_BUTTON_R1 : 0) |  // C(R) = right-bottom (R1 position)
               ((input_report.l2)     ? USBR_BUTTON_L2 : 0) |  // L2 shoulder
               ((input_report.r2)     ? USBR_BUTTON_R2 : 0) |  // R2 shoulder
               ((input_report.select) ? USBR_BUTTON_S1 : 0) |
               ((input_report.start)  ? USBR_BUTTON_S2 : 0) |
               ((input_report.l3)     ? USBR_BUTTON_L3 : 0) |  // L3 stick click
               ((input_report.r3)     ? USBR_BUTTON_R3 : 0) |  // R3 stick click
               ((input_report.home)   ? USBR_BUTTON_A1 : 0) |
               ((input_report.cap)    ? USBR_BUTTON_A2 : 0));  // Capture button

    uint8_t analog_1x = input_report.x1;
    uint8_t analog_1y = (input_report.y1 == 0) ? 255 : 256 - input_report.y1;
    uint8_t analog_2x = input_report.x2;
    uint8_t analog_2y = (input_report.y2 == 0) ? 255 : 256 - input_report.y2;

    // keep analog within range [1-255]
    ensureAllNonZero(&analog_1x, &analog_1y, &analog_2x, &analog_2y);

    // add to accumulator and post to the state machine
    // if a scan from the host machine is ongoing, wait
    input_event_t event = {
      .dev_addr = dev_addr,
      .instance = instance,
      .type = INPUT_TYPE_GAMEPAD,
      .layout = LAYOUT_SEGA_6BUTTON,  // Genesis/Saturn: Top [X][Y][Z], Bottom [A][B][C]
      .buttons = buttons,
      .button_count = 10,  // A, B, C, X, Y, Z (6 face) + L2, R2, L3, R3
      .analog = {analog_1x, analog_1y, analog_2x, analog_2y, 128, 0, 0, 128},
      .keys = 0,
    };
    router_submit_input(&event);

    prev_report[dev_addr-1] = input_report;
  }
}

DeviceInterface bitdo_m30_interface = {
  .name = "8BitDo M30 Bluetooth",
  .is_device = is_8bitdo_m30,
  .process = process_8bitdo_m30,
  .task = NULL,
  .init = NULL
};
