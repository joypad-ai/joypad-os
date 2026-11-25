// Host-side test for USB_ProcessHIDReport (the real hid_parser.c, not a copy):
// a report descriptor cut short at any length must never be read past its end.
// Each truncation is parsed from an exact-size heap block so AddressSanitizer
// reports the first byte read beyond it.
//
// Build (from this directory):
//   gcc -std=gnu11 -g -fsanitize=address -I../../src/usb/usbh/hid/devices/generic \
//       test_truncated_descriptor.c ../../src/usb/usbh/hid/devices/generic/hid_parser.c \
//       -o test_truncated_descriptor && ./test_truncated_descriptor
// Pass criterion: prints "ALL PASS" and AddressSanitizer stays silent.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hid_parser.h"

// A plain gamepad: 16 buttons, four 16-bit axes, and one 32-bit item so every
// item data size (1, 2 and 4 bytes) gets cut at every position.
static const uint8_t desc[] = {
    0x05, 0x01, 0x09, 0x05, 0xA1, 0x01,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x10, 0x81, 0x02,
    0x05, 0x01, 0x09, 0x30, 0x09, 0x31, 0x09, 0x32, 0x09, 0x35,
    0x16, 0x00, 0x80, 0x26, 0xFF, 0x7F,
    0x75, 0x10, 0x95, 0x04, 0x81, 0x02,
    0x27, 0xFF, 0xFF, 0x00, 0x00,
    0xC0,
};

int main(void)
{
    // Whole descriptor still parses, and finds the same 20 fields as before.
    HID_ReportInfo_t* info = NULL;
    uint8_t ret = USB_ProcessHIDReport(1, 0, desc, sizeof(desc), &info);
    if (ret != HID_PARSE_Successful || !info || info->TotalReportItems != 20) {
        printf("FAIL: full descriptor -> %d\n", ret);
        return 1;
    }
    USB_FreeReportInfo(info);

    // Every truncation: must return (any result) without reading past the end.
    for (size_t n = 1; n < sizeof(desc); n++) {
        uint8_t* cut = malloc(n);
        memcpy(cut, desc, n);
        info = NULL;
        ret = USB_ProcessHIDReport(1, 0, cut, (uint16_t)n, &info);
        if (ret == HID_PARSE_Successful && info) USB_FreeReportInfo(info);
        free(cut);
    }

    printf("ALL PASS\n");
    return 0;
}
