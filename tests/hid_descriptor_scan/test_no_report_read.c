// Host-side test: parsing a HID report descriptor must not read report data.
//
// While the generic USB and Bluetooth gamepad drivers walk a descriptor there
// is no report yet. They used to call USB_GetHIDReportItemInfo() with a 1-byte
// stack array anyway, which reads that array at each item's bit offset, so
// every item past the first byte read the stack. AddressSanitizer reports it.
//
// Build (from this directory):
//   gcc -std=gnu11 -g -fsanitize=address -Istub -I../../src -I../../src/lib/tinyusb/src \
//       -I../../src/usb/usbh/hid/devices/generic test_no_report_read.c \
//       ../../src/usb/usbh/hid/devices/generic/hid_parser.c ../../src/usb/usbh/hid/hid_utils.c \
//       -o test_no_report_read && ./test_no_report_read
// Pass criterion: prints "ALL PASS" and AddressSanitizer stays silent.

#include <stdio.h>
#include "core/router/router.h"

// The driver under test, compiled in so its parsed map can be checked.
#include "../../src/usb/usbh/hid/devices/generic/hid_gamepad.c"

void router_submit_input(const input_event_t* event) { (void)event; }

// 16 buttons, then 8-bit X and Y, then a 4-bit hat: items at byte 0 to 4.
static const uint8_t desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x10, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x26, 0xFF, 0x00,
    0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
    0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42,
    0x75, 0x04, 0x95, 0x01, 0x81, 0x03,
    0xC0,
};

int main(void)
{
    int fails = 0;
    if (!parse_hid_gamepad(1, 0, desc, sizeof(desc))) { printf("FAIL: not a gamepad\n"); fails++; }
    dinput_instance_t* in = &hid_devices[1].instances[0];
    if (in->buttonCnt != 16) { printf("FAIL: %d buttons\n", in->buttonCnt); fails++; }
    if (in->xLoc.byteIndex != 2 || in->yLoc.byteIndex != 3 || in->hatLoc.byteIndex != 4) {
        printf("FAIL: map x@%d y@%d hat@%d\n", in->xLoc.byteIndex, in->yLoc.byteIndex, in->hatLoc.byteIndex);
        fails++;
    }
    printf(fails ? "SOME FAILED\n" : "ALL PASS\n");
    return fails ? 1 : 0;
}
