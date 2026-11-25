// Host-side test for the KB/Mouse output mode (the real kbmouse_mode.c, not a
// copy): every report the mode sends must match the report descriptor the
// host was given for that interface (sinput_keyboard_report_descriptor and
// sinput_mouse_report_descriptor, served for both SInput and KB/Mouse modes).
//
// Build (from this directory):
//   gcc -std=gnu11 -Wall -DTUP_DCD_ENDPOINT_MAX=8 -Istub -I../../src/lib/tinyusb/src
//       -I../../src -I../../src/usb/usbd test_kbmouse_report.c -o test_kbmouse_report
//   ./test_kbmouse_report
// Pass criterion: prints "ALL PASS".

#include "../../src/usb/usbd/modes/kbmouse_mode.c"
#include <stdio.h>

// --- the TinyUSB calls the mode makes, captured instead of sent --------------
typedef struct { uint8_t itf; uint8_t bytes[64]; uint16_t len; } sent_t;
static sent_t sent[8];
static int n_sent;

bool tud_hid_n_ready(uint8_t instance) { (void)instance; return true; }

// On the wire a non-zero report_id is sent as the first byte (as TinyUSB does).
bool tud_hid_n_report(uint8_t instance, uint8_t report_id, void const* report, uint16_t len)
{
    sent_t* s = &sent[n_sent++];
    s->itf = instance;
    s->len = 0;
    if (report_id) s->bytes[s->len++] = report_id;
    memcpy(&s->bytes[s->len], report, len);
    s->len += len;
    return true;
}

// Same bodies as TinyUSB 0.19 hid_device.c: fixed 8-byte keyboard and
// 5-byte (8-bit X/Y) mouse reports.
bool tud_hid_n_keyboard_report(uint8_t instance, uint8_t report_id, uint8_t modifier, const uint8_t keycode[6])
{
    hid_keyboard_report_t r;
    r.modifier = modifier;
    r.reserved = 0;
    if (keycode) memcpy(r.keycode, keycode, 6); else memset(r.keycode, 0, 6);
    return tud_hid_n_report(instance, report_id, &r, sizeof(r));
}
bool tud_hid_n_mouse_report(uint8_t instance, uint8_t report_id,
                            uint8_t buttons, int8_t x, int8_t y, int8_t vertical, int8_t horizontal)
{
    hid_mouse_report_t r = { .buttons = buttons, .x = x, .y = y, .wheel = vertical, .pan = horizontal };
    return tud_hid_n_report(instance, report_id, &r, sizeof(r));
}

// --- the gamepad-to-KB/Mouse conversion, stubbed to known values -------------
void kbmouse_init(void) {}
void kbmouse_set_led_state(uint8_t leds) { (void)leds; }
void kbmouse_convert(uint32_t buttons, const profile_output_t* p,
                     kbmouse_keyboard_report_t* kb, kbmouse_mouse_report_t* m)
{
    (void)buttons; (void)p;
    memset(kb, 0, sizeof(*kb));
    kb->modifier = 0x02;                       // left shift
    kb->keycode[0] = 0x04; kb->keycode[1] = 0x16;  // a, s
    m->buttons = 0x01; m->x = -5; m->y = 7; m->wheel = 1; m->pan = -1;
}

// --- a minimal report descriptor walker: input bytes per report ID ----------
typedef struct { int uses_ids; int bits[256]; } layout_t;
static void walk(const uint8_t* d, size_t n, layout_t* L)
{
    int size = 0, count = 0, id = 0;
    memset(L, 0, sizeof(*L));
    for (size_t i = 0; i < n;) {
        uint8_t b = d[i];
        int len = (b & 3) == 3 ? 4 : (b & 3);
        uint32_t v = 0;
        for (int k = 0; k < len; k++) v |= (uint32_t)d[i + 1 + k] << (8 * k);
        switch (b & 0xFC) {
            case 0x74: size = (int)v; break;                 // Report Size
            case 0x94: count = (int)v; break;                // Report Count
            case 0x84: id = (int)v; L->uses_ids = 1; break;  // Report ID
            case 0x80: L->bits[id] += size * count; break;   // Input
        }
        i += 1 + len;
    }
}

static int fails;
static void ok(int c, const char* what) { printf("%-64s %s\n", what, c ? "PASS" : "FAIL"); if (!c) fails++; }

// Does this on-the-wire report match a report the descriptor declares?
static int matches(const sent_t* s, const uint8_t* d, size_t n)
{
    layout_t L; walk(d, n, &L);
    if (!L.uses_ids) return s->len == (L.bits[0] + 7) / 8;
    if (s->len < 1 || s->bytes[0] == 0 || !L.bits[s->bytes[0]]) return 0;
    return s->len == 1 + (L.bits[s->bytes[0]] + 7) / 8;
}

int main(void)
{
    input_event_t ev; profile_output_t po;
    memset(&ev, 0, sizeof(ev)); memset(&po, 0, sizeof(po));
    kbmouse_mode.init();
    n_sent = 0;
    kbmouse_mode.send_report(0, &ev, &po, 0);
    kbmouse_mode_send_idle_mouse();

    const sent_t *kb = NULL, *ms = NULL, *idle = NULL;
    for (int i = 0; i < n_sent; i++) {
        if (sent[i].itf == ITF_NUM_HID_KEYBOARD) kb = &sent[i];
        else if (sent[i].itf == ITF_NUM_HID_MOUSE) { if (!ms) ms = &sent[i]; else idle = &sent[i]; }
    }
    ok(kb && ms && idle, "mode sent a keyboard, a mouse and an idle mouse report");
    if (!(kb && ms && idle)) { printf("SOME FAILED\n"); return 1; }

    ok(matches(kb, sinput_keyboard_report_descriptor, sizeof(sinput_keyboard_report_descriptor)),
       "keyboard report: ID and length match the keyboard descriptor");
    ok(kb->len == 9 && kb->bytes[0] == SINPUT_KB_REPORT_ID_KEYBOARD && kb->bytes[1] == 0x02 &&
       kb->bytes[2] == 0 && kb->bytes[3] == 0x04 && kb->bytes[4] == 0x16,
       "keyboard report: [ID 1][modifier][reserved][keys] in that order");

    ok(matches(ms, sinput_mouse_report_descriptor, sizeof(sinput_mouse_report_descriptor)),
       "mouse report: length matches the 16-bit mouse descriptor");
    ok(matches(idle, sinput_mouse_report_descriptor, sizeof(sinput_mouse_report_descriptor)),
       "idle mouse report: length matches the 16-bit mouse descriptor");
    int16_t x = (int16_t)(ms->bytes[1] | (ms->bytes[2] << 8));
    int16_t y = (int16_t)(ms->bytes[3] | (ms->bytes[4] << 8));
    ok(ms->len == 7 && ms->bytes[0] == 0x01 && x == -5 && y == 7 &&
       (int8_t)ms->bytes[5] == 1 && (int8_t)ms->bytes[6] == -1,
       "mouse report: buttons, X:i16, Y:i16, wheel, pan read back as sent");
    ok(idle->len == ms->len && memcmp(idle->bytes, ms->bytes, ms->len) == 0,
       "idle mouse report has the same layout as the normal one");

    printf("%s\n", fails ? "SOME FAILED" : "ALL PASS");
    return fails ? 1 : 0;
}
