// switch2_pro.h
// Nintendo Switch 2 Pro Controller driver
// Requires USB bulk initialization sequence before HID reports work
#ifndef SWITCH2_PRO_HEADER_H
#define SWITCH2_PRO_HEADER_H

#include "../../../hid_device.h"
#include "../../../hid_utils.h"
#include "tusb.h"

// Switch 2 Pro Controller PID
#define SWITCH2_PRO_PID 0x2069

// Switch 2 Pro initialization states
typedef enum {
  SWITCH2_STATE_IDLE = 0,
  SWITCH2_STATE_FIND_ENDPOINT,
  SWITCH2_STATE_INIT_SEQUENCE,
  SWITCH2_STATE_READY,
  SWITCH2_STATE_FAILED
} switch2_init_state_t;

extern DeviceInterface switch2_pro_interface;

// Switch 2 Pro Input Report (Report ID 0x09)
// Based on WebHID parser in joypad-web/src/lib/usbr/webhid/parsers/nintendo.ts
// Format:
//   Byte 0: Report ID (0x09)
//   Byte 1: Counter
//   Byte 2: Fixed vendor byte
//   Byte 3: Buttons - B1, B2, B3, B4, R1, R2, S2, R3
//   Byte 4: Buttons - DD, DR, DL, DU, L1, L2, S1, L3
//   Byte 5: Buttons - A1, A2, R4, L4, A3, pad[3]
//   Bytes 6-8: Left stick (12-bit X, 12-bit Y packed)
//   Bytes 9-11: Right stick (12-bit X, 12-bit Y packed)
//   Bytes 12+: IMU/motion data
typedef struct {
  uint8_t  report_id;   // 0x09
  uint8_t  counter;     // Sequence counter
  uint8_t  fixed;       // Vendor byte (usually 0x00)

  // Byte 3: B1, B2, B3, B4, R1, R2, S2, R3
  struct {
    uint8_t b1 : 1;     // B (bottom face)
    uint8_t b2 : 1;     // A (right face)
    uint8_t b3 : 1;     // Y (left face)
    uint8_t b4 : 1;     // X (top face)
    uint8_t r1 : 1;     // R shoulder
    uint8_t r2 : 1;     // ZR trigger
    uint8_t s2 : 1;     // + (start)
    uint8_t r3 : 1;     // Right stick press
  };

  // Byte 4: DD, DR, DL, DU, L1, L2, S1, L3
  struct {
    uint8_t dd : 1;     // D-pad down
    uint8_t dr : 1;     // D-pad right
    uint8_t dl : 1;     // D-pad left
    uint8_t du : 1;     // D-pad up
    uint8_t l1 : 1;     // L shoulder
    uint8_t l2 : 1;     // ZL trigger
    uint8_t s1 : 1;     // - (select)
    uint8_t l3 : 1;     // Left stick press
  };

  // Byte 5: A1, A2, R4, L4, A3, unused[3]
  struct {
    uint8_t a1 : 1;     // Home
    uint8_t a2 : 1;     // Capture
    uint8_t r4 : 1;     // Rear right paddle
    uint8_t l4 : 1;     // Rear left paddle
    uint8_t a3 : 1;     // Square button
    uint8_t unused : 3;
  };

  uint8_t  left_stick[3];   // Packed 12-bit X/Y
  uint8_t  right_stick[3];  // Packed 12-bit X/Y
  uint8_t  imu_data[52];    // IMU/motion data

  // Unpacked analog values (calculated after parsing)
  uint16_t left_x, left_y, right_x, right_y;
} switch2_pro_report_t;

#endif
