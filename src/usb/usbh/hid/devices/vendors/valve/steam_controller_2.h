// steam_controller_2.h - Valve Steam Controller 2 (codename "Triton" / "Roy")
// VID: 0x28DE  PIDs: 0x1302 (wired) / 0x1304 (puck dongle)
//
// USB report ID 0x45 (= 69) input layout decoded from
// jfedor2/hid-remapper firmware/src/quirks.cc @ commit 722ea05
// (originally 0x42 = 66 in commit e2d0a5e; Valve firmware update bumped it
// to 0x45). The device's published HID descriptor is unusable — the entire
// layout below is manual.
//
// Report layout (64-byte interrupt-IN, bit offsets from start of report):
//   bit 0   : report ID byte (0x45)
//   bit 8-37: 30 packed button/touch bits (see steam_controller_2.c)
//   bit 40  : L2 analog (uint16, 0..32767)
//   bit 56  : R2 analog (uint16, 0..32767)
//   bit 72  : LX stick (int16, normal)
//   bit 88  : LY stick (int16, Valve sends +up — INVERT to HID convention)
//   bit 104 : RX stick (int16, normal)
//   bit 120 : RY stick (int16, +up — INVERT)
//   bit 136 : Trackpad1 X/Y/pressure (int16 ×3)
//   bit 184 : Trackpad2 X/Y/pressure (int16 ×3)
//   bit 264 : Accel X/Y/Z (int16 ×3)
//   bit 312 : Gyro X/Y/Z (int16 ×3)
//   bit 360 : Vno/Vbrx/Vbry/Vbrz (int16 ×4, role TBD)
//
// SPDX-License-Identifier: Apache-2.0

#ifndef STEAM_CONTROLLER_2_H
#define STEAM_CONTROLLER_2_H

#include "../../../hid_device.h"

#define SC2_VID            0x28DE
#define SC2_PID_WIRED      0x1302
#define SC2_PID_PUCK       0x1304
#define SC2_INPUT_REPORT_ID 0x45

extern DeviceInterface steam_controller_2_interface;

#endif // STEAM_CONTROLLER_2_H
