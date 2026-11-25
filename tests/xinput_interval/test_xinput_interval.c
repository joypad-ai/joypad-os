// Host-side unit test for the XInput gamepad IN endpoint polling interval in
// src/usb/usbd/descriptors/xinput_descriptors.h. Walks the real configuration
// descriptor and checks endpoint 0x81.
//
// Build from the repo root (full-speed targets, expect interval 1):
//   gcc -std=gnu11 -Wall -Itests/xinput_interval/stub -Isrc/lib/tinyusb/src -Isrc/usb/usbd
//       tests/xinput_interval/test_xinput_interval.c -o test_xinput_interval && ./test_xinput_interval
// Build (CH32 high speed, expect interval 4, which is 1 ms there): add -DPLATFORM_CH32
// Pass criterion: prints "ALL PASS".

#include <stdio.h>
#include <stdint.h>
#include "descriptors/xinput_descriptors.h"

#ifdef PLATFORM_CH32
#define EXPECT_INTERVAL 4
#else
#define EXPECT_INTERVAL 1
#endif

int main(void)
{
    const uint8_t* d = xinput_config_descriptor;
    unsigned total = (unsigned)d[2] | ((unsigned)d[3] << 8);
    unsigned i = 0, found = 0, fails = 0;

    if (total != sizeof(xinput_config_descriptor)) {
        printf("FAIL wTotalLength %u, array %u\n", total, (unsigned)sizeof(xinput_config_descriptor));
        fails++;
    }
    while (i + 1 < total && d[i] != 0) {
        if (d[i + 1] == 0x05 && d[i] == 7 && d[i + 2] == 0x81) {
            found = 1;
            if (d[i + 6] != EXPECT_INTERVAL) {
                printf("FAIL EP 0x81 bInterval %u, expected %u\n", d[i + 6], EXPECT_INTERVAL);
                fails++;
            }
        }
        i += d[i];
    }
    if (i != total) { printf("FAIL descriptor walk ended at %u of %u\n", i, total); fails++; }
    if (!found) { printf("FAIL EP 0x81 not found\n"); fails++; }
    printf("%s\n", fails ? "SOME FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
