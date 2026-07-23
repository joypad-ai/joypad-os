// steam_controller_2_ble.h - Valve Steam Controller 2 ("Triton") over BLE
//
// The SC2 in Bluetooth mode speaks Valve's proprietary GATT service
// (100F6C32-…), NOT HID-over-GATT. The custom GATT client that discovers the
// service, subscribes to the input characteristic, and keeps the controller in
// gamepad ("no lizard") mode lives in bt/btstack/btstack_host.c
// (register_valve_hid_listener / valve_periodic). That client prepends the
// implied report-id byte and routes each notification through the normal bthid
// path, so this file only has to PARSE the assembled report.
//
// Assembled input report (byte 0 = report id, prepended by the GATT client):
//   0     report id  (0x45 TritonMTUNoQuat, or 0x47 TritonMTUNoQuat32TS "Ibex")
//   1     seq_num
//   2-5   buttons    (u32 LE — see steam_controller_2_ble.c bit map)
//   6-7   left  trigger (u16 LE, 0..32767)
//   8-9   right trigger (u16 LE, 0..32767)
//   10-11 LX (s16), 12-13 LY (s16, +up — INVERT), 14-15 RX (s16), 16-17 RY (s16, INVERT)
//   ...   trackpads/pressure (layout differs between 0x45 and 0x47 — unused here)
//   34-39 accel X/Y/Z (s16, ±2 g)
//   40-45 gyro  X/Y/Z (s16, ±2000 dps)
// accel/gyro land at the SAME offsets for both report types, so parsing is
// identical apart from the trackpad region we ignore.
//
// Report layout from SDL src/joystick/hidapi/SDL_hidapi_steam_triton.c and
// steam/controller_structs.h; cross-checked against safijari/openpuck RF docs.
//
// SPDX-License-Identifier: Apache-2.0

#ifndef STEAM_CONTROLLER_2_BLE_H
#define STEAM_CONTROLLER_2_BLE_H

#include "bt/bthid/bthid.h"

// Valve VID + synthetic SC2-BLE PID (assigned by the GATT client; the SC2 does
// not advertise a USB VID/PID over the air). Mirrors SDL's mobile glue.
#define SC2_BLE_VID     0x28DE
#define SC2_BLE_PID     0x1303

// Report ids carried on the two possible Valve input characteristics.
#define SC2_BLE_REPORT_45  0x45
#define SC2_BLE_REPORT_47  0x47

extern const bthid_driver_t steam_controller_2_ble_driver;

void steam_controller_2_ble_register(void);

#endif // STEAM_CONTROLLER_2_BLE_H
