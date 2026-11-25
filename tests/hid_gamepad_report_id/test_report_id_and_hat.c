// Host-side test for the generic USB HID gamepad driver: report IDs and hats.
//
// 1. A pad with two report IDs (gamepad on ID 1, something else on ID 2) must
//    only treat ID 1 reports as gamepad input.
// 2. A hat whose Logical Minimum is 1 (1=N, 0=centre) must decode N as N.
// 3. A hat in the upper nibble of its byte must decode at all.
// The Bluetooth twin (bthid_gamepad.c) already handles all three.
//
// Build (from this directory):
//   gcc -std=gnu11 -g -Istub -I../../src -I../../src/lib/tinyusb/src \
//       -I../../src/usb/usbh/hid/devices/generic test_report_id_and_hat.c \
//       ../../src/usb/usbh/hid/devices/generic/hid_parser.c ../../src/usb/usbh/hid/hid_utils.c \
//       -o test_report_id_and_hat && ./test_report_id_and_hat
// Pass criterion: prints "ALL PASS".

#include <stdio.h>
#include "core/router/router.h"
#include "core/buttons.h"
#include "../../src/usb/usbh/hid/devices/generic/hid_gamepad.c"

static input_event_t last;
static int events;
void router_submit_input(const input_event_t* event) { last = *event; events++; }

#define DPAD (JP_BUTTON_DU | JP_BUTTON_DD | JP_BUTTON_DL | JP_BUTTON_DR)

// Report 1: 8 buttons, X, Y, a 1-based hat in the low nibble, 4 bits padding.
// Report 2: 8 more buttons (a second button report some pads send).
static const uint8_t desc_ids[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x85, 0x01,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x15, 0x00, 0x26, 0xFF, 0x00,
    0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
    0x09, 0x39, 0x15, 0x01, 0x25, 0x08, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42,
    0x75, 0x04, 0x95, 0x01, 0x81, 0x03,
    0x85, 0x02,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0xC0,
};

// No report IDs: 8 buttons, 4 bits padding, then a 0-based hat in the UPPER nibble.
static const uint8_t desc_hi_hat[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x08, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x75, 0x04, 0x95, 0x01, 0x81, 0x03,
    0x05, 0x01, 0x09, 0x39, 0x15, 0x00, 0x25, 0x07, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42,
    0xC0,
};

static int fails;
static void ok(int c, const char* what) { printf("%-60s %s\n", what, c ? "PASS" : "FAIL"); if (!c) fails++; }

int main(void)
{
    ok(parse_hid_gamepad(1, 0, desc_ids, sizeof(desc_ids)), "ids: descriptor accepted as a gamepad");

    uint8_t north[5] = { 0x01, 0x00, 0x80, 0x80, 0x01 };   // ID 1, hat = 1 (north)
    events = 0;
    process_hid_gamepad(1, 0, north, sizeof(north));
    ok(events == 1 && (last.buttons & DPAD) == JP_BUTTON_DU, "ids: 1-based hat value 1 reads as up only");

    uint8_t other[2] = { 0x02, 0xFF };                        // ID 2, all its buttons down
    events = 0;
    process_hid_gamepad(1, 0, other, sizeof(other));
    ok(events == 0, "ids: a report with another ID is not gamepad input");

    uint8_t centre[5] = { 0x01, 0x00, 0x80, 0x80, 0x00 };  // ID 1, hat = 0 (centre)
    process_hid_gamepad(1, 0, centre, sizeof(centre));
    ok((last.buttons & DPAD) == 0, "ids: 1-based hat value 0 reads as centred");

    ok(parse_hid_gamepad(2, 0, desc_hi_hat, sizeof(desc_hi_hat)), "upper-nibble hat: descriptor accepted");
    uint8_t east[2] = { 0x00, 0x20 };                         // hat = 2 (east) in bits 12..15
    events = 0;
    process_hid_gamepad(2, 0, east, sizeof(east));
    ok(events == 1 && (last.buttons & DPAD) == JP_BUTTON_DR, "upper-nibble hat: value 2 reads as right");

    printf(fails ? "SOME FAILED\n" : "ALL PASS\n");
    return fails ? 1 : 0;
}
