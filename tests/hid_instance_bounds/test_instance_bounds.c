// Host-side test: HID instance numbers up to CFG_TUH_HID - 1 stay in bounds.
//
// TinyUSB hands the HID callbacks an instance that indexes its shared table of
// CFG_TUH_HID (8) interfaces, so it can be 0..7 once a few devices are plugged
// in. Several drivers kept per-instance state in arrays only 5 wide. This runs
// the generic gamepad driver on instance 7 with the bounds sanitizer on.
//
// Build (from this directory):
//   gcc -std=gnu11 -g -fsanitize=bounds -fno-sanitize-recover=bounds -Istub -I../../src \
//       -I../../src/lib/tinyusb/src -I../../src/usb/usbh/hid/devices/generic test_instance_bounds.c \
//       ../../src/usb/usbh/hid/devices/generic/hid_parser.c ../../src/usb/usbh/hid/hid_utils.c \
//       -o test_instance_bounds && ./test_instance_bounds
// Pass criterion: prints "ALL PASS" (an out-of-bounds index aborts the run).

#include <stdio.h>
#include "core/router/router.h"
#include "../../src/usb/usbh/hid/devices/generic/hid_gamepad.c"

static int events;
void router_submit_input(const input_event_t* event) { (void)event; events++; }

static const uint8_t desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x26, 0xFF, 0x00,
    0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
    0xC0,
};

int main(void)
{
    uint8_t inst = CFG_TUH_HID - 1;
    parse_hid_gamepad(2, inst, desc, sizeof(desc));
    uint8_t report[3] = { 0x01, 0x80, 0x80 };
    process_hid_gamepad(2, inst, report, sizeof(report));
    printf(events == 1 ? "ALL PASS\n" : "FAIL: no event\n");
    return events == 1 ? 0 : 1;
}
