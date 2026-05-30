// wasm_bridge.c
// Thin Emscripten-exported interface around libjoypad for JavaScript consumers.
//
// Design: JS allocates one HID-report buffer in WASM heap and passes its
// pointer + length on each WebHID 'inputreport' event. WASM writes the parsed
// state into a single static input_event_t, and JS reads individual fields
// via dedicated getter functions (cheap WASM-to-JS calls).
//
// This keeps the JS<->WASM marshaling simple and decouples JS from the
// in-memory layout of input_event_t (which evolves).

#include <joypad/devices/sony/ds5.h>
#include <joypad/input_event.h>
#include <joypad/feedback.h>
#include <joypad/capabilities.h>

#include <stdint.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define JOYPAD_WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define JOYPAD_WASM_EXPORT
#endif

// ----------------------------------------------------------------------------
// State (single-controller demo)
// ----------------------------------------------------------------------------

static input_event_t      g_event;
static joypad_feedback_t  g_feedback;
static uint8_t            g_feedback_buf[JOYPAD_SONY_DS5_FEEDBACK_PAYLOAD_LEN];

// ----------------------------------------------------------------------------
// VID/PID + capability discovery
// ----------------------------------------------------------------------------

JOYPAD_WASM_EXPORT
int joypad_wasm_is_sony_ds5(uint16_t vid, uint16_t pid) {
    return joypad_is_sony_ds5(vid, pid) ? 1 : 0;
}

// Returns DS5 feedback report ID (for JS to use as report number on the
// outgoing WebHID send).
JOYPAD_WASM_EXPORT
int joypad_wasm_ds5_feedback_report_id(void) {
    return JOYPAD_SONY_DS5_FEEDBACK_REPORT_ID;
}

JOYPAD_WASM_EXPORT
int joypad_wasm_ds5_feedback_payload_len(void) {
    return JOYPAD_SONY_DS5_FEEDBACK_PAYLOAD_LEN;
}

// ----------------------------------------------------------------------------
// Input parsing
// ----------------------------------------------------------------------------
// JS passes the full HID report including the report ID byte.

JOYPAD_WASM_EXPORT
int joypad_wasm_parse_ds5(const uint8_t* buf, int len) {
    if (len <= 0) return 0;
    return joypad_parse_sony_ds5(buf, (uint16_t)len, &g_event) ? 1 : 0;
}

// Field accessors — read the most-recently-parsed event.

JOYPAD_WASM_EXPORT uint32_t joypad_wasm_buttons(void)        { return g_event.buttons; }
JOYPAD_WASM_EXPORT int      joypad_wasm_analog(int i)        { return (i >= 0 && i < ANALOG_COUNT) ? g_event.analog[i] : 0; }
JOYPAD_WASM_EXPORT int      joypad_wasm_gyro(int i)          { return (i >= 0 && i < 3) ? g_event.gyro[i]  : 0; }
JOYPAD_WASM_EXPORT int      joypad_wasm_accel(int i)         { return (i >= 0 && i < 3) ? g_event.accel[i] : 0; }
JOYPAD_WASM_EXPORT int      joypad_wasm_touch_x(int i)       { return (i >= 0 && i < 2) ? g_event.touch[i].x : 0; }
JOYPAD_WASM_EXPORT int      joypad_wasm_touch_y(int i)       { return (i >= 0 && i < 2) ? g_event.touch[i].y : 0; }
JOYPAD_WASM_EXPORT int      joypad_wasm_touch_active(int i)  { return (i >= 0 && i < 2 && g_event.touch[i].active) ? 1 : 0; }
JOYPAD_WASM_EXPORT int      joypad_wasm_battery_level(void)  { return g_event.battery_level; }
JOYPAD_WASM_EXPORT int      joypad_wasm_battery_charging(void) { return g_event.battery_charging ? 1 : 0; }
JOYPAD_WASM_EXPORT int      joypad_wasm_has_motion(void)     { return g_event.has_motion ? 1 : 0; }
JOYPAD_WASM_EXPORT int      joypad_wasm_has_touch(void)      { return g_event.has_touch  ? 1 : 0; }

// ----------------------------------------------------------------------------
// Feedback builder
// ----------------------------------------------------------------------------

// Reset the staged feedback to zero (no fields dirty).
JOYPAD_WASM_EXPORT
void joypad_wasm_feedback_reset(void) {
    joypad_feedback_init(&g_feedback);
}

JOYPAD_WASM_EXPORT
void joypad_wasm_feedback_set_rumble(int low, int high) {
    g_feedback.rumble_dirty = 1;
    g_feedback.rumble_low  = (uint8_t)(low  & 0xff);
    g_feedback.rumble_high = (uint8_t)(high & 0xff);
}

JOYPAD_WASM_EXPORT
void joypad_wasm_feedback_set_lightbar(int r, int g, int b) {
    g_feedback.lightbar_dirty = 1;
    g_feedback.lightbar.r = (uint8_t)(r & 0xff);
    g_feedback.lightbar.g = (uint8_t)(g & 0xff);
    g_feedback.lightbar.b = (uint8_t)(b & 0xff);
}

JOYPAD_WASM_EXPORT
void joypad_wasm_feedback_set_player_index(int idx) {
    g_feedback.player_index_dirty = 1;
    g_feedback.player_index = (uint8_t)(idx & 0xff);
}

JOYPAD_WASM_EXPORT
void joypad_wasm_feedback_set_adaptive_right(int mode, int p0, int p1, int p2) {
    g_feedback.adaptive_right_dirty = 1;
    g_feedback.adaptive_right.mode = (joypad_trigger_mode_t)mode;
    g_feedback.adaptive_right.params[0] = (uint8_t)(p0 & 0xff);
    g_feedback.adaptive_right.params[1] = (uint8_t)(p1 & 0xff);
    g_feedback.adaptive_right.params[2] = (uint8_t)(p2 & 0xff);
}

JOYPAD_WASM_EXPORT
void joypad_wasm_feedback_set_adaptive_left(int mode, int p0, int p1, int p2) {
    g_feedback.adaptive_left_dirty = 1;
    g_feedback.adaptive_left.mode = (joypad_trigger_mode_t)mode;
    g_feedback.adaptive_left.params[0] = (uint8_t)(p0 & 0xff);
    g_feedback.adaptive_left.params[1] = (uint8_t)(p1 & 0xff);
    g_feedback.adaptive_left.params[2] = (uint8_t)(p2 & 0xff);
}

// Build the wire-format feedback payload into the internal buffer.
// Returns a pointer to the buffer (length is fixed at
// JOYPAD_SONY_DS5_FEEDBACK_PAYLOAD_LEN, see joypad_wasm_ds5_feedback_payload_len).
JOYPAD_WASM_EXPORT
const uint8_t* joypad_wasm_build_ds5_feedback(void) {
    uint16_t n = joypad_build_sony_ds5_feedback(&g_feedback, g_feedback_buf, sizeof(g_feedback_buf));
    if (n == 0) return 0;
    return g_feedback_buf;
}
