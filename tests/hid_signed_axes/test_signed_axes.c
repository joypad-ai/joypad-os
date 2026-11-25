// Host-side test: signed axes whose Logical Minimum fits in one byte.
//
// A pad that declares its sticks as Logical Minimum -127 (sent as the single
// byte 0x81), Logical Maximum 127 and rests at 0 must read as centred. The
// parser used to store that minimum as 129, so the USB driver took the axis
// for unsigned and a stick at rest read as hard up-left.
//
// Build (from this directory):
//   gcc -std=gnu11 -g -Istub -I../../src -I../../src/lib/tinyusb/src \
//       -I../../src/usb/usbh/hid/devices/generic test_signed_axes.c \
//       ../../src/usb/usbh/hid/devices/generic/hid_parser.c ../../src/usb/usbh/hid/hid_utils.c \
//       -o test_signed_axes && ./test_signed_axes
// Pass criterion: prints "ALL PASS".

#include <stdio.h>
#include "core/router/router.h"
#include "../../src/usb/usbh/hid/devices/generic/hid_gamepad.c"

static input_event_t last;
static int events;
void router_submit_input(const input_event_t* event) { last = *event; events++; }

// 8 buttons, then X and Y as signed bytes: Logical Min 0x81 (-127), Max 0x7F.
static const uint8_t desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F,
    0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
    0xC0,
};

static int fails;
static void ok(int c, const char* what) { printf("%-58s %s\n", what, c ? "PASS" : "FAIL"); if (!c) fails++; }

int main(void)
{
    HID_ReportInfo_t* pi = NULL;
    USB_ProcessHIDReport(1, 0, desc, sizeof(desc), &pi);
    HID_ReportItem_t* it = pi ? pi->FirstReportItem : NULL;
    while (it && !(it->Attributes.Usage.Page == 1 && it->Attributes.Usage.Usage == 0x30)) it = it->Next;
    ok(it && (int32_t)it->Attributes.Logical.Minimum == -127, "parser: one-byte Logical Minimum 0x81 is -127");
    ok(it && (int32_t)it->Attributes.Logical.Maximum == 127, "parser: Logical Maximum 0x7F stays 127");
    if (pi) USB_FreeReportInfo(pi);

    ok(parse_hid_gamepad(1, 0, desc, sizeof(desc)), "usb: descriptor accepted as a gamepad");
    uint8_t rest[3] = { 0x00, 0x00, 0x00 };           // no buttons, X = 0, Y = 0
    process_hid_gamepad(1, 0, rest, sizeof(rest));
    ok(events == 1 && last.analog[0] >= 127 && last.analog[0] <= 129 &&
       last.analog[1] >= 127 && last.analog[1] <= 129, "usb: stick at rest (0, 0) reads centred");
    uint8_t right[3] = { 0x00, 0x7F, 0x81 };          // X = +127, Y = -127
    process_hid_gamepad(1, 0, right, sizeof(right));
    ok(last.analog[0] == 255 && last.analog[1] == 1, "usb: +127 is full right, -127 is full up");

    // An unsigned axis sent with a one-byte maximum of 0xFF must still be 0..255.
    static const uint8_t udesc[] = {
        0x05, 0x01, 0x09, 0x05, 0xA1, 0x01, 0x09, 0x30, 0x15, 0x00, 0x25, 0xFF,
        0x75, 0x08, 0x95, 0x01, 0x81, 0x02, 0xC0,
    };
    pi = NULL;
    USB_ProcessHIDReport(1, 0, udesc, sizeof(udesc), &pi);
    ok(pi && pi->FirstReportItem && pi->FirstReportItem->Attributes.Logical.Maximum == 255,
       "parser: unsigned field keeps a one-byte maximum of 0xFF as 255");
    if (pi) USB_FreeReportInfo(pi);

    printf(fails ? "SOME FAILED\n" : "ALL PASS\n");
    return fails ? 1 : 0;
}
