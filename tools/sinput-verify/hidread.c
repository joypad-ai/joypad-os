// hidread.c - minimal hidapi reader for the SInput gamepad interface.
// Opens VID 0x2E8A PID 0x10C6, usage page 0x01 / usage 0x05 (Game Pad),
// and prints any input report whose bytes differ from the previous one.
// Build: see Makefile target `hidread`.

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <hidapi.h>

#define VID 0x2E8A
#define PID 0x10C6

static volatile sig_atomic_t stop = 0;
static void on_sigint(int s) { (void)s; stop = 1; }

int main(void) {
    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    if (hid_init() != 0) { fprintf(stderr, "hid_init failed\n"); return 1; }

    // Find the gamepad interface (usage page 1, usage 5) on our device.
    struct hid_device_info *devs = hid_enumerate(VID, PID);
    char path[512] = {0};
    for (struct hid_device_info *d = devs; d; d = d->next) {
        printf("iface: path=%s usage_page=0x%04hx usage=0x%04hx\n",
               d->path ? d->path : "(null)", d->usage_page, d->usage);
        if (d->usage_page == 0x01 && d->usage == 0x05 && d->path) {
            strncpy(path, d->path, sizeof(path) - 1);
        }
    }
    hid_free_enumeration(devs);

    if (!path[0]) { fprintf(stderr, "no gamepad interface (usage 1/5) found\n"); hid_exit(); return 2; }

    hid_device *h = hid_open_path(path);
    if (!h) { fprintf(stderr, "hid_open_path failed: %ls\n", hid_error(NULL)); hid_exit(); return 3; }

    printf("opened %s — reading reports (Ctrl-C to stop). Press buttons now.\n", path);
    hid_set_nonblocking(h, 1);

    // SInput button bit names (matches descriptors/sinput_descriptors.h)
    static const char *btn[32] = {
        "EAST","SOUTH","NORTH","WEST","DU","DD","DL","DR",
        "L3","R3","L1","R1","L2","R2","LPADDLE1","RPADDLE1",
        "START","BACK","GUIDE","CAPTURE","LPADDLE2","RPADDLE2","TP1","TP2",
        "POWER","MISC4","MISC5","MISC6","MISC7","MISC8","MISC9","MISC10"
    };

    uint8_t buf[64], prev[64] = {0};
    int prev_len = 0;
    while (!stop) {
        int n = hid_read_timeout(h, buf, sizeof(buf), 200);
        if (n <= 0) continue;
        if (n < 19 || buf[0] != 0x01) continue;  // only decode input report 0x01

        // Mask the IMU timestamp (bytes 19-22) so it doesn't count as a change.
        uint8_t cmp[64];
        memcpy(cmp, buf, n);
        if (n >= 23) memset(cmp + 19, 0, 4);
        if (n == prev_len && memcmp(cmp, prev, n) == 0) continue;
        memcpy(prev, cmp, n);
        prev_len = n;

        uint32_t b = (uint32_t)buf[3] | ((uint32_t)buf[4] << 8) |
                     ((uint32_t)buf[5] << 16) | ((uint32_t)buf[6] << 24);
        int16_t lx = (int16_t)(buf[7]  | (buf[8]  << 8));
        int16_t ly = (int16_t)(buf[9]  | (buf[10] << 8));
        int16_t rx = (int16_t)(buf[11] | (buf[12] << 8));
        int16_t ry = (int16_t)(buf[13] | (buf[14] << 8));
        int16_t lt = (int16_t)(buf[15] | (buf[16] << 8));
        int16_t rt = (int16_t)(buf[17] | (buf[18] << 8));

        printf("buttons:");
        if (!b) printf(" (none)");
        for (int i = 0; i < 32; i++) if (b & (1u << i)) printf(" %s", btn[i]);
        printf("  |  LX=%6d LY=%6d RX=%6d RY=%6d LT=%6d RT=%6d\n",
               lx, ly, rx, ry, lt, rt);
    }

    hid_close(h);
    hid_exit();
    return 0;
}
