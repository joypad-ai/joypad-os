// Host-side test for xgip_parse() in src/usb/usbd/drivers/xgip_protocol.c.
// Compiles the real file. Run under AddressSanitizer so any read past the
// received bytes is reported.
//
// Build (one line):
//   gcc -std=gnu11 -Wall -g -fsanitize=address -Istub -I../../src/lib/tinyusb/src -I../../src/usb/usbd/drivers test_xgip_parse.c ../../src/usb/usbd/drivers/xgip_protocol.c -o test_xgip_parse && ./test_xgip_parse
// Pass criterion: prints "ALL PASS" and ASan reports nothing.

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "xgip_protocol.h"

static int fails = 0;
static void check(int cond, const char* what)
{
    printf("%-64s %s\n", what, cond ? "PASS" : "FAIL");
    if (!cond) fails++;
}

// Feed exactly `len` bytes from a heap buffer of exactly that size, so any
// read past the end is an ASan heap-buffer-overflow.
static bool parse_exact(xgip_t* x, const uint8_t* bytes, uint16_t len)
{
    uint8_t* buf = malloc(len ? len : 1);
    memcpy(buf, bytes, len);
    bool r = xgip_parse(x, buf, len);
    free(buf);
    return r;
}

int main(void)
{
    xgip_t x;
    xgip_init(&x);

    // Well-formed non-chunked packet: header + 3 data bytes.
    const uint8_t ok[] = { 0x06, 0x20, 0x01, 0x03, 0xAA, 0xBB, 0xCC };
    parse_exact(&x, ok, sizeof(ok));
    check(x.is_valid && x.data_length == 3 && x.data && x.data[2] == 0xCC,
          "non-chunked: well-formed packet still parses");

    // Header claims 200 data bytes, only 4 bytes arrived.
    const uint8_t lie[] = { 0x06, 0x20, 0x01, 200 };
    parse_exact(&x, lie, sizeof(lie));
    check(!x.is_valid, "non-chunked: length byte larger than the packet is rejected");

    // Chunked start: header says 0x3A bytes of data, packet is 8 bytes.
    const uint8_t ch[] = { 0x06, 0xF0, 0x01, 0x3A, 0x00, 0x02, 0x11, 0x22 };
    parse_exact(&x, ch, sizeof(ch));
    check(!x.is_valid, "chunked: data length larger than the packet is rejected");

    // End-of-chunk marker cut short (needs 6 bytes, has 4).
    const uint8_t eoc[] = { 0x06, 0xA0, 0x01, 0x00 };
    parse_exact(&x, eoc, sizeof(eoc));
    check(!x.is_valid, "chunked: end marker without its size bytes is rejected");

    // Well-formed chunk start that fits.
    const uint8_t ch_ok[] = { 0x06, 0xF0, 0x01, 0x02, 0x02, 0x00, 0x11, 0x22 };
    parse_exact(&x, ch_ok, sizeof(ch_ok));
    check(x.is_valid && x.actual_data_received == 2,
          "chunked: well-formed chunk still parses");

    xgip_reset(&x);
    printf("%s\n", fails ? "SOME FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
