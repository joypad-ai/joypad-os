// elo_vagabond.c — ELO Vagabond V1 Interface (VID 0483 / PID A4DB)
//
// A standard USB HID gamepad, but its button order doesn't match the generic
// DInput remap (and it uses signed sticks + Simulation-Controls triggers), so
// it gets a dedicated driver. Report is 15 bytes, report ID 2:
//   [0]      report ID (0x02)
//   [1..8]   X, Y, Z, Rz  — int16 LE, SIGNED (-32767..32767, centered 0)
//   [9]      Brake        — L2 analog (0..255)
//   [10]     Accelerator  — R2 analog (0..255)
//   [11]     hat (low nibble: 0=N,2=E,4=S,6=W,8=released) + 4 bits pad
//   [12..13] buttons 1-16 (LE bitfield)
//   [14]     buttons 17,18 + consumer (bit7 = ELO/Home)
//
// Button order: A,B,R4,X,Y,L4,L1,R1,L2,R2,Select,Start,(13),L3,R3.
#include "elo_vagabond.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include <string.h>

#define ELO_REPORT_LEN 15

// ELO Vagabond V1
bool is_elo_vagabond(uint16_t vid, uint16_t pid) {
  return (vid == 0x0483 && pid == 0xA4DB);
}

// Signed int16 (-32767..32767, centered 0) -> 1..255 (128 center), HID convention.
static uint8_t scale_s16(int16_t v) {
  int32_t out = 128 + ((int32_t)v * 127) / 32767;
  return out < 1 ? 1 : (out > 255 ? 255 : (uint8_t)out);
}

// process usb hid input reports
void process_elo_vagabond(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  if (len < ELO_REPORT_LEN) return;

  static uint8_t prev[MAX_DEVICES][ELO_REPORT_LEN] = { 0 };
  if (!memcmp(prev[dev_addr-1], report, ELO_REPORT_LEN)) return;
  memcpy(prev[dev_addr-1], report, ELO_REPORT_LEN);

  int16_t lx = (int16_t)(report[1] | (report[2] << 8));
  int16_t ly = (int16_t)(report[3] | (report[4] << 8));
  int16_t rx = (int16_t)(report[5] | (report[6] << 8));
  int16_t ry = (int16_t)(report[7] | (report[8] << 8));
  uint8_t  l2  = report[9];   // Brake
  uint8_t  r2  = report[10];  // Accelerator
  uint8_t  hat = report[11] & 0x0F;
  uint16_t btn = (uint16_t)(report[12] | (report[13] << 8));
  uint8_t  btn2 = report[14];

  bool up    = (hat == 0 || hat == 1 || hat == 7);
  bool right = (hat >= 1 && hat <= 3);
  bool down  = (hat >= 3 && hat <= 5);
  bool left  = (hat >= 5 && hat <= 7);

  uint32_t buttons =
      (up    ? JP_BUTTON_DU : 0) |
      (down  ? JP_BUTTON_DD : 0) |
      (left  ? JP_BUTTON_DL : 0) |
      (right ? JP_BUTTON_DR : 0) |
      ((btn & (1u << 0))  ? JP_BUTTON_B1 : 0) |   //  1  A
      ((btn & (1u << 1))  ? JP_BUTTON_B2 : 0) |   //  2  B
      ((btn & (1u << 2))  ? JP_BUTTON_R4 : 0) |   //  3  R4 (back paddle)
      ((btn & (1u << 3))  ? JP_BUTTON_B3 : 0) |   //  4  X
      ((btn & (1u << 4))  ? JP_BUTTON_B4 : 0) |   //  5  Y
      ((btn & (1u << 5))  ? JP_BUTTON_L4 : 0) |   //  6  L4 (back paddle)
      ((btn & (1u << 6))  ? JP_BUTTON_L1 : 0) |   //  7  L1
      ((btn & (1u << 7))  ? JP_BUTTON_R1 : 0) |   //  8  R1
      ((btn & (1u << 8))  ? JP_BUTTON_L2 : 0) |   //  9  L2 (also analog Brake)
      ((btn & (1u << 9))  ? JP_BUTTON_R2 : 0) |   // 10  R2 (also analog Accelerator)
      ((btn & (1u << 10)) ? JP_BUTTON_S1 : 0) |   // 11  Select (three dots)
      ((btn & (1u << 11)) ? JP_BUTTON_S2 : 0) |   // 12  Start (three lines)
      ((btn & (1u << 13)) ? JP_BUTTON_L3 : 0) |   // 14  L3
      ((btn & (1u << 14)) ? JP_BUTTON_R3 : 0) |   // 15  R3
      ((btn2 & (1u << 7)) ? JP_BUTTON_A1 : 0);    // 19  ELO/Home

  input_event_t event = {
    .dev_addr = dev_addr,
    .instance = instance,
    .type = INPUT_TYPE_GAMEPAD,
    .transport = INPUT_TRANSPORT_USB,
    .buttons = buttons,
    .button_count = 14,
    .analog = { scale_s16(lx), scale_s16(ly), scale_s16(rx), scale_s16(ry), l2, r2 },
    .keys = 0,
  };
  router_submit_input(&event);
}

DeviceInterface elo_vagabond_interface = {
  .name = "ELO Vagabond V1",
  .is_device = is_elo_vagabond,
  .process = process_elo_vagabond,
  .task = NULL,
  .init = NULL
};
