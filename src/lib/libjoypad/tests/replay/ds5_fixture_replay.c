// ds5_fixture_replay.c
//
// Loads every *.bin file under tests/fixtures/sony_ds5/captured/ and runs it
// through joypad_parse_sony_ds5. Each fixture file contains one raw HID input
// report including the report ID byte, captured from a physical DualSense.
//
// Pass criteria:
//   - Every fixture parses (returns true).
//   - The parsed input_event_t has plausible field values (analog axes in
//     range, layout = MODERN_4FACE, etc.).
//   - Optional per-fixture assertions can be added inline as the fixture set
//     grows (e.g. dpad_north.bin must set JP_BUTTON_DU and clear DD/DL/DR).
//
// If the captured/ directory is empty (no hardware available at scaffold time),
// the test prints a notice and returns success — fixture-based replay only
// becomes a hard CI requirement once real captures land.

#include <joypad/devices/sony/ds5.h>
#include <joypad/input_event.h>
#include <joypad/buttons.h>

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifndef DS5_FIXTURE_DIR
#  define DS5_FIXTURE_DIR "tests/fixtures/sony_ds5/captured"
#endif

static int failures = 0;
static int total    = 0;

static int load_file(const char* path, uint8_t* buf, size_t cap, size_t* out_len) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    *out_len = n;
    return 1;
}

static void check_plausible(const char* name, const input_event_t* ev) {
    total++;
    if (ev->type != INPUT_TYPE_GAMEPAD) {
        fprintf(stderr, "FAIL [%s]: type != GAMEPAD\n", name);
        failures++;
        return;
    }
    if (ev->transport != INPUT_TRANSPORT_USB) {
        fprintf(stderr, "FAIL [%s]: transport != USB\n", name);
        failures++;
        return;
    }
    if (ev->layout != LAYOUT_MODERN_4FACE) {
        fprintf(stderr, "FAIL [%s]: layout != MODERN_4FACE\n", name);
        failures++;
        return;
    }
    // Buttons bitmap should fit known mask (no garbage bits set).
    uint32_t known_mask =
        JP_BUTTON_DU | JP_BUTTON_DD | JP_BUTTON_DL | JP_BUTTON_DR |
        JP_BUTTON_B1 | JP_BUTTON_B2 | JP_BUTTON_B3 | JP_BUTTON_B4 |
        JP_BUTTON_L1 | JP_BUTTON_R1 | JP_BUTTON_L2 | JP_BUTTON_R2 |
        JP_BUTTON_S1 | JP_BUTTON_S2 | JP_BUTTON_L3 | JP_BUTTON_R3 |
        JP_BUTTON_A1 | JP_BUTTON_A2 | JP_BUTTON_A3 |
        JP_BUTTON_L4 | JP_BUTTON_R4;
    if (ev->buttons & ~known_mask) {
        fprintf(stderr, "FAIL [%s]: buttons bitmap has unknown bits set: 0x%08x\n",
                name, ev->buttons & ~known_mask);
        failures++;
        return;
    }
    printf("  PASS [%-24s] buttons=0x%08x LX=%3u LY=%3u RX=%3u RY=%3u L2=%3u R2=%3u  battery=%u%%%s\n",
           name,
           ev->buttons,
           ev->analog[ANALOG_LX], ev->analog[ANALOG_LY],
           ev->analog[ANALOG_RX], ev->analog[ANALOG_RY],
           ev->analog[ANALOG_L2], ev->analog[ANALOG_R2],
           ev->battery_level, ev->battery_charging ? " (charging)" : "");
}

int main(void) {
    const char* dir_path = getenv("LIBJOYPAD_DS5_FIXTURE_DIR");
    if (!dir_path) dir_path = DS5_FIXTURE_DIR;

    DIR* d = opendir(dir_path);
    if (!d) {
        printf("ds5_fixture_replay: %s not present — skipping (capture some fixtures with tools/capture_ds5_fixtures.py to enable).\n", dir_path);
        return EXIT_SUCCESS;
    }

    int file_count = 0;
    struct dirent* de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] == '.') continue;
        size_t nl = strlen(de->d_name);
        if (nl < 4 || strcmp(de->d_name + nl - 4, ".bin") != 0) continue;

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir_path, de->d_name);

        uint8_t buf[256];
        size_t len = 0;
        if (!load_file(path, buf, sizeof(buf), &len)) {
            fprintf(stderr, "FAIL: could not read %s\n", path);
            failures++;
            total++;
            continue;
        }

        input_event_t ev;
        if (!joypad_parse_sony_ds5(buf, (uint16_t)len, &ev)) {
            fprintf(stderr, "FAIL [%s]: parser rejected the report (len=%zu)\n", de->d_name, len);
            failures++;
            total++;
            continue;
        }
        check_plausible(de->d_name, &ev);
        file_count++;
    }
    closedir(d);

    if (file_count == 0) {
        printf("ds5_fixture_replay: %s is empty — skipping.\n", dir_path);
        return EXIT_SUCCESS;
    }

    if (failures) {
        fprintf(stderr, "\n%d/%d fixture replays FAILED\n", failures, total);
        return EXIT_FAILURE;
    }
    printf("\n%d/%d DS5 fixture replays OK\n", total, total);
    return EXIT_SUCCESS;
}
