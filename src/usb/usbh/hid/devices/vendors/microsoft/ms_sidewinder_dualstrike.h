// ms_sidewinder_dualstrike.h - Microsoft SideWinder Dual Strike
// VID: 0x045E  PID: 0x0028
// 5-byte report with 10-bit tilt axes, 4-bit twist, 9 buttons, hat switch
// SPDX-License-Identifier: Apache-2.0

#ifndef MS_SIDEWINDER_DUALSTRIKE_H
#define MS_SIDEWINDER_DUALSTRIKE_H

#include "../../../hid_device.h"
#include "../../../hid_utils.h"
#include "tusb.h"

// Report is 5 bytes with bit-packed fields spanning byte boundaries.
// Parsed manually in the .c file rather than using a packed struct.
#define DUALSTRIKE_REPORT_SIZE 5

extern DeviceInterface ms_sidewinder_dualstrike_interface;

#endif
