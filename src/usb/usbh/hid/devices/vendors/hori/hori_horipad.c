// hori_horipad.c
#include "hori_horipad.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/input_event.h"

// check if device is HORIPAD for Nintendo Switch 
//                (or Sega Genesis mini controllers)
bool is_hori_horipad(uint16_t vid, uint16_t pid)
{
  return (
    (vid == 0x0f0d && pid == 0x00c1)    // Switch HORI HORIPAD
    // HORI
    || (vid == 0x0f0d && pid == 0x0011) // HORI Real Arcade Pro 3 (RAP 3 SA/SE)
    || (vid == 0x0f0d && pid == 0x0010) // HORI Fighting Stick 3
    || (vid == 0x0f0d && pid == 0x0026) // HORI Real Arcade Pro 3 Premium VLX
    || (vid == 0x0f0d && pid == 0x0027) // HORI Fighting Stick V3
    || (vid == 0x0f0d && pid == 0x008b) // HORI RAP V HAYABUSA Controller (PS3 Mode)
    // Mad Catz
    || (vid == 0x0738 && pid == 0x3180) // Mad Catz Fight Stick Alpha (PS3 Mode)
    || (vid == 0x0738 && pid == 0x8818) // Mad Catz SFIV Tournament Edition Round 1 (PS3)
    || (vid == 0x0738 && pid == 0x8838) // Mad Catz SFIV Tournament Edition Round 2 (PS3)
    // Quanba
    || (vid == 0x2c22 && pid == 0x2302) // Qanba Obsidian (PS3 Mode)
    || (vid == 0x2c22 && pid == 0x2500) // Qanba Dragon (PS3 Mode)
    // Other
    || (vid == 0x146b && pid == 0x0904) // Nacon Daija Arcade Stick (PS3 Mode)
    || (vid == 0x1292 && pid == 0x4e47) // Fire NEOGEOX Arcade Stick (PS3 HID Mode)
    || (vid == 0x0079 && pid == 0x0006) // Generic Zero Delay USB Encoder (Standard PC/PS3)
    || (vid == 0x046d && pid == 0xc216) // Logitech F310 (DirectInput / PS3 Mode)
    || (vid == 0x10c4 && pid == 0x82c0) // VIRTUA STICK High Grade (PS3)
  );
}

// check if 2 reports are different enough
bool diff_report_horipad(hori_horipad_report_t const* rpt1, hori_horipad_report_t const* rpt2) {
  bool result = memcmp(rpt1, rpt2, 3) != 0;

  // x, y, z, rz must different than 2 to be counted
  result |= diff_than_n(rpt1->axis_x, rpt2->axis_x, 2) ||
            diff_than_n(rpt1->axis_y, rpt2->axis_y, 2) ||
            diff_than_n(rpt1->axis_z, rpt2->axis_z, 2) ||
            diff_than_n(rpt1->axis_rz, rpt2->axis_rz, 2);

  return result;
}

// process usb hid input reports
void process_hori_horipad(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  uint32_t buttons;
  // previous report used to compare for changes
  static hori_horipad_report_t prev_report[5] = { 0 };

  hori_horipad_report_t input_report;
  memcpy(&input_report, report, sizeof(input_report));

  if (diff_report_horipad(&prev_report[dev_addr-1], &input_report)) {
    TU_LOG1("(x, y, z, rz) = (%d, %d, %d, %d) ", input_report.axis_x, input_report.axis_y, input_report.axis_z, input_report.axis_rz);
    TU_LOG1("DPad = %d ", input_report.dpad);

    if (input_report.b) TU_LOG1("B ");
    if (input_report.a) TU_LOG1("A ");
    if (input_report.y) TU_LOG1("Y ");
    if (input_report.x) TU_LOG1("X ");
    if (input_report.l1) TU_LOG1("L1 ");
    if (input_report.r1) TU_LOG1("R1 ");
    if (input_report.l2) TU_LOG1("L2(Z) ");
    if (input_report.r2) TU_LOG1("R2(C) ");
    if (input_report.l3) TU_LOG1("L3 ");
    if (input_report.r3) TU_LOG1("R3 ");
    if (input_report.s1) TU_LOG1("Select ");
    if (input_report.s2) TU_LOG1("Start ");
    if (input_report.a1) TU_LOG1("Home ");
    if (input_report.a2) TU_LOG1("Capture ");
    TU_LOG1("\r\n");

    bool dpad_up    = (input_report.dpad == 0 || input_report.dpad == 1 || input_report.dpad == 7);
    bool dpad_right = (input_report.dpad >= 1 && input_report.dpad <= 3);
    bool dpad_down  = (input_report.dpad >= 3 && input_report.dpad <= 5);
    bool dpad_left  = (input_report.dpad >= 5 && input_report.dpad <= 7);

    // HORI Fighting Commander physical layout (6-button for Switch):
    //   Top row:    [Y][X][R]   (left to right)
    //   Bottom row: [B][A][ZR]  (left to right)
    //
    // GP2040-CE canonical mapping (position-based):
    //   Top row:    [B3][B4][R1]
    //   Bottom row: [B1][B2][R2]
    //
    // Mapping: B→B1, A→B2, ZR(r2)→R2, Y→B3, X→B4, R(r1)→R1
    // L1/L2 are shoulder buttons (not part of 6-button face layout)
    buttons = (((dpad_up)         ? JP_BUTTON_DU : 0) |
               ((dpad_down)       ? JP_BUTTON_DD : 0) |
               ((dpad_left)       ? JP_BUTTON_DL : 0) |
               ((dpad_right)      ? JP_BUTTON_DR : 0) |
               ((input_report.b)  ? JP_BUTTON_B1 : 0) |  // B = left-bottom
               ((input_report.a)  ? JP_BUTTON_B2 : 0) |  // A = mid-bottom
               ((input_report.y)  ? JP_BUTTON_B3 : 0) |  // Y = left-top
               ((input_report.x)  ? JP_BUTTON_B4 : 0) |  // X = mid-top
               ((input_report.l1) ? JP_BUTTON_L1 : 0) |  // L shoulder
               ((input_report.r1) ? JP_BUTTON_R1 : 0) |  // R = right-top
               ((input_report.l2) ? JP_BUTTON_L2 : 0) |  // ZL shoulder
               ((input_report.r2) ? JP_BUTTON_R2 : 0) |  // ZR = right-bottom
               ((input_report.s1) ? JP_BUTTON_S1 : 0) |
               ((input_report.s2) ? JP_BUTTON_S2 : 0) |
               ((input_report.l3) ? JP_BUTTON_L3 : 0) |
               ((input_report.r3) ? JP_BUTTON_R3 : 0) |
               ((input_report.a1) ? JP_BUTTON_A1 : 0) |
               ((input_report.a2) ? JP_BUTTON_A2 : 0));
    // HID convention: 0=up, 255=down (no inversion needed)
    uint8_t axis_x = input_report.axis_x;
    uint8_t axis_y = input_report.axis_y;
    uint8_t axis_z = input_report.axis_z;
    uint8_t axis_rz = input_report.axis_rz;

    // keep analog within range [1-255]
    ensureAllNonZero(&axis_x, &axis_y, &axis_z, &axis_rz);

    // add to accumulator and post to the state machine
    // if a scan from the host machine is ongoing, wait
    input_event_t event = {
      .dev_addr = dev_addr,
      .instance = instance,
      .type = INPUT_TYPE_GAMEPAD,
        .transport = INPUT_TRANSPORT_USB,
        .transport = INPUT_TRANSPORT_USB,
        .transport = INPUT_TRANSPORT_USB,
        .transport = INPUT_TRANSPORT_USB,
      .layout = LAYOUT_SEGA_6BUTTON,  // Switch 6-btn layout matches Genesis: Top [Y][X][R], Bottom [B][A][ZR]
      .buttons = buttons,
      .button_count = 10,  // B, A, Y, X, L, R, ZL, ZR, L3, R3
      .analog = {axis_x, axis_y, axis_z, axis_rz, 0, 0},
      .keys = 0,
    };
    router_submit_input(&event);

    prev_report[dev_addr-1] = input_report;
  }
}

DeviceInterface hori_horipad_interface = {
  .name = "HORI HORIPAD (or Genesis/MD Mini)",
  .is_device = is_hori_horipad,
  .process = process_hori_horipad,
  .task = NULL,
  .init = NULL
};
