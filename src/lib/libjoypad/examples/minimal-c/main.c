// minimal-c/main.c
// Smallest possible libjoypad consumer. Plain ANSI C + HIDAPI.
//
// Enumerates connected HID devices, opens the first Sony DualSense it finds,
// reads HID input reports in a loop, and prints the decoded input_event_t
// to stdout. Demonstrates that the same libjoypad parser that runs in
// joypad-os firmware also runs unchanged on host operating systems with no
// platform abstraction layer in between.
//
// Build:
//   cmake -B build && cmake --build build
// Run:
//   ./build/joypad_minimal_c           # uses default DS5 VID/PID
//   ./build/joypad_minimal_c --quiet   # only print on state change
//
// Requires libhidapi. On macOS/Linux: `brew install hidapi` or
// `apt install libhidapi-dev`. On Windows: vcpkg or upstream releases.

#if __has_include(<hidapi.h>)
#  include <hidapi.h>
#elif __has_include(<hidapi/hidapi.h>)
#  include <hidapi/hidapi.h>
#else
#  error "hidapi.h not found — install libhidapi-dev or brew install hidapi"
#endif

#include <joypad/devices/sony/ds5.h>
#include <joypad/input_event.h>
#include <joypad/buttons.h>
#include <joypad/feedback.h>
#include <joypad/capabilities.h>

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int _sig) { (void)_sig; g_stop = 1; }

static void print_caps(const joypad_caps_t* c) {
    printf("Device: %s %s (VID=%04x PID=%04x)\n",
           c->vendor_name  ? c->vendor_name  : "?",
           c->product_name ? c->product_name : "?",
           c->vendor_id, c->product_id);
    printf("  axes_mask=0x%02x  buttons=%u  dpad=%s\n",
           c->axes_mask, c->button_count, c->has_dpad ? "yes" : "no");
    printf("  motion=%s (gyro %u dps, accel %u milli-g)\n",
           c->has_motion ? "yes" : "no", c->gyro_range_dps, c->accel_range_milli_g);
    printf("  touchpad=%s (%u fingers, %ux%u)  click=%s\n",
           c->has_touchpad ? "yes" : "no", c->num_touchpoints,
           c->touch_max_x, c->touch_max_y,
           c->touchpad_has_click ? "yes" : "no");
    printf("  rumble=%s  dual=%s  triggers_rumble=%s  adaptive_triggers=%s\n",
           c->has_rumble ? "yes" : "no",
           c->has_dual_rumble ? "yes" : "no",
           c->has_trigger_rumble ? "yes" : "no",
           c->has_adaptive_triggers ? "yes" : "no");
    printf("  lightbar=%s  player_leds=%s  mic_led=%s  speaker=%s  headset_jack=%s\n",
           c->has_lightbar ? "yes" : "no",
           c->has_player_leds ? "yes" : "no",
           c->has_mic_led ? "yes" : "no",
           c->has_speaker ? "yes" : "no",
           c->has_headset_jack ? "yes" : "no");
}

static void print_event(const input_event_t* e) {
    printf("buttons=0x%08x  LX=%3u LY=%3u  RX=%3u RY=%3u  L2=%3u R2=%3u  ",
           e->buttons,
           e->analog[ANALOG_LX], e->analog[ANALOG_LY],
           e->analog[ANALOG_RX], e->analog[ANALOG_RY],
           e->analog[ANALOG_L2], e->analog[ANALOG_R2]);
    if (e->has_motion) {
        printf("gyro=(%6d,%6d,%6d) accel=(%6d,%6d,%6d)  ",
               e->gyro[0], e->gyro[1], e->gyro[2],
               e->accel[0], e->accel[1], e->accel[2]);
    }
    if (e->has_touch) {
        printf("tp0=(%4u,%4u%s) tp1=(%4u,%4u%s)  ",
               e->touch[0].x, e->touch[0].y, e->touch[0].active ? "*" : " ",
               e->touch[1].x, e->touch[1].y, e->touch[1].active ? "*" : " ");
    }
    if (e->battery_level) {
        printf("bat=%u%%%s", e->battery_level, e->battery_charging ? "+" : "");
    }
    printf("\n");
}

static bool send_startup_rumble(hid_device* dev) {
    joypad_feedback_t fb;
    joypad_feedback_init(&fb);
    fb.rumble_dirty = true;
    fb.rumble_low = 96;
    fb.rumble_high = 96;
    fb.lightbar_dirty = true;
    fb.lightbar = (joypad_rgb_t){0, 64, 255};
    fb.player_index_dirty = true;
    fb.player_index = 1;

    uint8_t buf[1 + JOYPAD_SONY_DS5_FEEDBACK_PAYLOAD_LEN];
    buf[0] = JOYPAD_SONY_DS5_FEEDBACK_REPORT_ID;
    uint16_t n = joypad_build_sony_ds5_feedback(&fb, buf + 1, JOYPAD_SONY_DS5_FEEDBACK_PAYLOAD_LEN);
    if (n == 0) return false;

    int wrote = hid_write(dev, buf, 1 + n);
    return wrote > 0;
}

int main(int argc, char** argv) {
    bool quiet = false;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--quiet") == 0) quiet = true;
    }

    signal(SIGINT, on_sigint);

    if (hid_init() != 0) {
        fprintf(stderr, "hid_init failed\n");
        return EXIT_FAILURE;
    }

    // Enumerate to find a DS5. We rely on libjoypad's is_sony_ds5 so the
    // VID/PID list stays in one place.
    struct hid_device_info* list = hid_enumerate(0, 0);
    if (!list) {
        fprintf(stderr, "No HID devices found.\n");
        hid_exit();
        return EXIT_FAILURE;
    }

    char path[512] = {0};
    uint16_t found_vid = 0, found_pid = 0;
    for (struct hid_device_info* it = list; it != NULL; it = it->next) {
        if (joypad_is_sony_ds5(it->vendor_id, it->product_id)) {
            strncpy(path, it->path, sizeof(path) - 1);
            found_vid = it->vendor_id;
            found_pid = it->product_id;
            break;
        }
    }
    hid_free_enumeration(list);

    if (!path[0]) {
        fprintf(stderr, "No DualSense (DS5) found. Connect a Sony DualSense or "
                        "Victrix Pro FS for PS5 over USB and re-run.\n");
        hid_exit();
        return EXIT_FAILURE;
    }

    hid_device* dev = hid_open_path(path);
    if (!dev) {
        fprintf(stderr, "hid_open_path failed for %s\n", path);
        hid_exit();
        return EXIT_FAILURE;
    }
    hid_set_nonblocking(dev, 1);

    joypad_caps_t caps;
    joypad_sony_ds5_caps(&caps);
    caps.vendor_id = found_vid;
    caps.product_id = found_pid;
    caps.vendor_name = "Sony";
    caps.product_name = "DualSense";
    caps.is_wireless = false;
    print_caps(&caps);
    printf("---- streaming (Ctrl-C to stop) ----\n");

    if (!send_startup_rumble(dev)) {
        // Not fatal — some devices reject output reports until configured.
        fprintf(stderr, "Startup rumble + lightbar set failed (continuing).\n");
    }

    uint8_t buf[256];
    input_event_t prev = {0};
    while (!g_stop) {
        int n = hid_read_timeout(dev, buf, sizeof(buf), 100);
        if (n <= 0) continue;

        input_event_t e;
        if (!joypad_parse_sony_ds5(buf, (uint16_t)n, &e)) continue;

        if (quiet) {
            if (memcmp(&e, &prev, sizeof(e)) == 0) continue;
            prev = e;
        }
        print_event(&e);
    }

    hid_close(dev);
    hid_exit();
    return EXIT_SUCCESS;
}
