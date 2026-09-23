// Host-side unit test for the Switch 2 BLE output protocol engine.
//
// Replays the real console's commands from ndeadly's decrypted Pro Controller 2
// captures (pairing + bonded reconnect) through switch2_proto and requires every
// reply to be byte-identical to what the real controller sent — including the
// pairing crypto (B1/B2), the flash/calibration reads and the late-init probes.
//
// Build (from repo root):
//   cc -std=c11 -Wall -Isrc -Isrc/bt/switch2_ble -Isrc/lib/pico-sdk/lib/btstack/3rd-party/rijndael \
//      tests/switch2_proto/test_switch2_proto.c src/bt/switch2_ble/switch2_proto.c \
//      src/lib/pico-sdk/lib/btstack/3rd-party/rijndael/rijndael.c -o /tmp/test_switch2_proto \
//      && /tmp/test_switch2_proto
// Pass criterion: prints "ALL PASS".

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>

#include "switch2_proto.h"
#include "core/buttons.h"
#include "capture_vectors.h"

static int failures;

static void hexdump(const char *tag, const uint8_t *b, int n)
{
    printf("  %s:", tag);
    for (int i = 0; i < n; i++) printf(" %02x", b[i]);
    printf("\n");
}

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); failures++; } } while (0)

static void test_golden_crypto(void)
{
    const uint8_t a1[16] = {0x35,0x03,0xe9,0x29,0x82,0x87,0x71,0x24,0xbe,0xa8,0x0c,0x66,0x46,0x15,0x83,0x4b};
    const uint8_t a2[16] = {0x6f,0xc6,0xdf,0x8a,0xd8,0xfe,0xdf,0x15,0xbb,0x8c,0x15,0xe9,0x1f,0x32,0x05,0x44};
    const uint8_t ltk_ref[16] = {0x69,0xf5,0x07,0x50,0xae,0x58,0x74,0xc5,0x04,0x83,0x6f,0x43,0x82,0x0f,0xdc,0x5b};
    const uint8_t b2_ref[16] = {0x13,0x4c,0x97,0xf5,0x11,0xb9,0xb6,0xdd,0x4d,0x86,0xfd,0x40,0xf5,0x36,0xe9,0xed};
    uint8_t ltk[16], b2[16];
    switch2_derive_ltk(a1, ltk);
    switch2_confirm(ltk, a2, b2);
    CHECK(memcmp(ltk, ltk_ref, 16) == 0, "LTK golden vector");
    CHECK(memcmp(b2, b2_ref, 16) == 0, "B2 golden vector");
}

static void replay(const char *name, const sw2_vector_t *v, size_t n, switch2_proto_t *s)
{
    int matched = 0;
    for (size_t i = 0; i < n; i++) {
        uint8_t rsp[SW2_MAX_RSP_LEN];
        uint8_t ev;
        uint16_t len = switch2_proto_handle_command(s, v[i].cmd, v[i].cmd_len, rsp, &ev);
        if (len == v[i].rsp_len && memcmp(rsp, v[i].rsp, len) == 0) { matched++; continue; }
        printf("FAIL: %s[%zu] cmd %02x/%02x reply mismatch (ours %u bytes, real %u)\n",
               name, i, v[i].cmd[0], v[i].cmd[3], len, v[i].rsp_len);
        hexdump("cmd ", v[i].cmd, v[i].cmd_len);
        hexdump("ours", rsp, len);
        hexdump("real", v[i].rsp, v[i].rsp_len);
        failures++;
    }
    printf("%s: %d/%zu replies byte-identical to the real controller\n", name, matched, n);
}

static void test_capture_replay(void)
{
    // The real controller's address (98:e2:55:c2:16:88), wire order, so 0x15/0x01 matches.
    const uint8_t pad_addr[6] = {0x88, 0x16, 0xc2, 0x55, 0xe2, 0x98};
    switch2_proto_t s;

    switch2_proto_init(&s, pad_addr);
    replay("pairing", pairing_vectors, sizeof(pairing_vectors) / sizeof(pairing_vectors[0]), &s);
    CHECK(s.paired, "pairing finalised");
    const uint8_t console[6] = {0x81, 0xeb, 0x3a, 0xeb, 0xf1, 0x48};
    CHECK(memcmp(s.host_addr, console, 6) == 0, "console identity address captured");
    CHECK(s.features == 0x2f, "features enabled after init (0x%02x)", s.features);
    CHECK(s.player_leds == 0x01, "player 1 LED");

    switch2_proto_init(&s, pad_addr);
    replay("reconnect", reconnect_vectors, sizeof(reconnect_vectors) / sizeof(reconnect_vectors[0]), &s);
    CHECK(s.features == 0x2f, "reconnect: set-mask enables features (0x%02x)", s.features);
}

static void test_out_of_order_pairing(void)
{
    switch2_proto_t s;
    switch2_proto_init(&s, NULL);
    uint8_t rsp[SW2_MAX_RSP_LEN], ev;
    const uint8_t finalise[9] = {0x15, 0x91, 0x01, 0x03, 0x00, 0x01, 0x00, 0x00, 0x00};
    CHECK(switch2_proto_handle_command(&s, finalise, sizeof(finalise), rsp, &ev) == 0,
          "finalise before confirm must not reply");
    CHECK(!s.paired && !(ev & SW2_EVT_PAIRED), "finalise before confirm must not bond");
}

static uint16_t unpack_x(const uint8_t *p) { return (uint16_t)(p[0] | ((p[1] & 0x0f) << 8)); }
static uint16_t unpack_y(const uint8_t *p) { return (uint16_t)((p[1] >> 4) | (p[2] << 4)); }

static void test_input_report(void)
{
    switch2_proto_t s;
    switch2_proto_init(&s, NULL);
    s.features = 0x2f;
    switch2_input_t in = { .buttons = JP_BUTTON_B2 | JP_BUTTON_A3 | JP_BUTTON_L4 | JP_BUTTON_DU,
                           .lx = 128, .ly = 128, .rx = 255, .ry = 0, .battery_pct = 100 };
    uint8_t r[SW2_INPUT_REPORT_LEN];
    switch2_proto_build_input(&s, &in, r);
    CHECK(r[0] == 0 && s.counter == 1, "counter");
    CHECK(r[1] == (9 << 2), "battery level 9");
    CHECK(r[2] == 0x02, "A (B2) -> byte2 0x02");
    CHECK(r[3] == 0x08, "D-up -> byte3 0x08");
    CHECK(r[4] == (0x10 | 0x08), "C + GL -> byte4 0x18 (got %02x)", r[4]);
    CHECK(unpack_x(&r[5]) == 0x7b3 && unpack_y(&r[5]) == 0x836, "left stick at factory neutral");
    CHECK(unpack_x(&r[8]) == 0x82c + 0x5d1, "right X full right = neutral + max");
    CHECK(unpack_y(&r[8]) == 0x848 + 0x636, "right Y full up (HID 0) = neutral + max");
    CHECK(r[0x0b] == 0x38 && r[0x0e] == 0x28, "rumble flag + motion length");

    in.ry = 255; in.lx = 0;
    switch2_proto_build_input(&s, &in, r);
    CHECK(unpack_y(&r[8]) == 0x848 - 0x622, "right Y full down = neutral - min (got %03x)", unpack_y(&r[8]));
    CHECK(unpack_x(&r[5]) == 0x7b3 - 0x63a, "left X full left = neutral - min (got %03x)", unpack_x(&r[5]));
}

static void test_rumble_and_adv(void)
{
    // Idle LRA frame from the pairing capture (frame 513): no amplitude.
    uint8_t idle[32] = {0x50, 0x81, 0x01, 0x10, 0x1e, 0x00};
    memcpy(&idle[16], idle, 6);
    uint8_t l, r;
    switch2_rumble_decode(idle, &l, &r);
    CHECK(l == 0 && r == 0, "idle rumble decodes to 0");

    uint8_t on[32] = {0};
    on[0] = 0x50;                                    // one op, enabled
    uint32_t w = (uint32_t)808 << 10;                // full-scale low-frequency amplitude
    memcpy(&on[1], &w, 4);                           // little-endian host
    switch2_rumble_decode(on, &l, &r);
    CHECK(l == 255 && r == 0, "full lf amplitude -> 255 left");

    const uint8_t host[6] = {0x81, 0xeb, 0x3a, 0xeb, 0xf1, 0x48};
    uint8_t m[SW2_MFR_DATA_LEN];
    switch2_build_mfr_data(SW2_ADV_WAKE, host, m);
    const uint8_t wake_ref[SW2_MFR_DATA_LEN] = {0x53,0x05,0x01,0x00,0x03,0x7e,0x05,0x69,0x20,0x00,0x01,0x81,
        0x81,0xeb,0x3a,0xeb,0xf1,0x48,0x0f,0,0,0,0,0,0,0};
    CHECK(memcmp(m, wake_ref, sizeof(m)) == 0, "wake advertisement");
}

int main(void)
{
    test_golden_crypto();
    test_capture_replay();
    test_out_of_order_pairing();
    test_input_report();
    test_rumble_and_adv();
    if (failures) { printf("%d FAILURE(S)\n", failures); return 1; }
    printf("ALL PASS\n");
    return 0;
}
