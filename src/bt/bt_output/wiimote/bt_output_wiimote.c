// bt_output_wiimote.c - Wiimote-over-Bluetooth Classic HID device output
// SPDX-License-Identifier: Apache-2.0
//
// STATUS (Phase 1b): core report generation + output decoding is wired.
// Remaining work:
//   Phase 1c: Nunchuk / Classic extension bytes
//   Phase 1d: BTstack hid_device_init + custom SDP + PIN pairing
//   Phase 1e: usb2wiimote / bt2wiimote apps
//
// Right now router_tap updates a cached input_event_t and marks it dirty;
// a periodic task builds the currently-selected report format. Once the
// BTstack wiring lands in 1d, the task will push via
// hid_device_send_interrupt_message().

#include "bt_output_wiimote.h"
#include "../bt_output.h"
#include "wiimote_reports.h"
#include "wiimote_eeprom.h"
#include "wiimote_ext.h"
#include "wiimote_sdp.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// INTERNAL STATE
// ============================================================================

static wiimote_state_t      wm_state;
static wiimote_ext_mode_t   current_ext = WIIMOTE_EXT_NONE;
static bool                 connected = false;

// Cached most-recent router event. We don't generate a report unless
// something changed OR the Wii requested continuous mode.
static input_event_t        last_event;
static bool                 event_valid = false;
static bool                 event_dirty = false;

// ============================================================================
// ROUTER TAP — called when new input routes to OUTPUT_TARGET_WIIMOTE_BT
// ============================================================================

static void wiimote_router_tap(output_target_t target, uint8_t player_index,
                                const input_event_t* event) {
    (void)target;
    (void)player_index;
    if (!event) return;
    last_event  = *event;
    event_valid = true;
    event_dirty = true;
}

// ============================================================================
// OUTPUT-REPORT HANDLERS — called from BTstack set-report callback (Phase 1d)
// ============================================================================
//
// For now these are direct-callable entry points. Once hid_device is wired,
// the BTstack packet handler forwards SET_REPORT payloads here.

void bt_output_wiimote_handle_output_report(const uint8_t* report, uint16_t len) {
    if (!report || len < 1) return;

    uint8_t id = report[0];

    // Memory read (0x17) — return a 0x21 response from the virtual EEPROM.
    if (id == 0x17 && len >= 7) {
        uint32_t addr = ((uint32_t)report[2] << 16) | ((uint32_t)report[3] << 8) | report[4];
        uint16_t size = ((uint16_t)report[5] << 8) | report[6];
        if (size > 16) size = 16;       // 0x21 payload holds up to 16 bytes

        uint8_t buf[22] = {0};
        buf[0] = 0x21;
        uint16_t btns = event_valid ? wiimote_buttons_from_event(&last_event) : 0;
        buf[1] = btns & 0xff;
        buf[2] = (btns >> 8) & 0xff;
        buf[3] = ((size - 1) & 0x0f) << 4;  // upper nibble = size-1, lower = err=0
        buf[4] = (addr >> 8) & 0xff;
        buf[5] = addr & 0xff;
        wiimote_eeprom_read_block(addr, &buf[6], size);
        wiimote_sdp_send_report(buf, 6 + size);
        return;
    }

    bool need_status = wiimote_apply_output(&wm_state, report, len);
    if (need_status) {
        uint8_t status_buf[8];
        uint16_t n = wiimote_build_status(&wm_state,
                                          event_valid ? &last_event : NULL,
                                          status_buf);
        wiimote_sdp_send_report(status_buf, n);
    }
}

// ============================================================================
// PERIODIC TASK
// ============================================================================

static void wiimote_init(void) {
    printf("[bt_wiimote] init\n");
    wiimote_state_init(&wm_state);
    current_ext = WIIMOTE_EXT_NONE;
    connected = false;
    event_valid = false;
    event_dirty = false;
    memset(&last_event, 0, sizeof(last_event));

    // Register BTstack HID + SDP + GAP setup.
    wiimote_sdp_register();

    // Wire router so the router delivers input events to us.
    router_set_tap_exclusive(OUTPUT_TARGET_WIIMOTE_BT, wiimote_router_tap);
}

static void wiimote_task(void) {
    connected = wiimote_sdp_is_connected();
    if (!connected) return;
    if (!event_valid) return;

    // Emit when something changed OR the Wii asked for continuous mode.
    if (!event_dirty && !wm_state.continuous) return;
    event_dirty = false;

    uint8_t buf[23];
    uint16_t n = wiimote_build_current(&wm_state, &last_event, buf);
    wiimote_sdp_send_report(buf, n);
}

// ============================================================================
// PUBLIC API
// ============================================================================

void bt_output_wiimote_set_ext(wiimote_ext_mode_t mode) {
    if (mode >= WIIMOTE_EXT_COUNT) return;
    if (mode == current_ext) return;
    printf("[bt_wiimote] ext mode %d -> %d (hot-plug)\n", current_ext, mode);
    current_ext = mode;
    wm_state.extension_attached = (mode != WIIMOTE_EXT_NONE);
    // TODO Phase 1c: send 0x20 status with the extension-attached bit
    // toggled so the Wii re-reads the ext ID at 0xFA.
}

wiimote_ext_mode_t bt_output_wiimote_get_ext(void) {
    return current_ext;
}

// ============================================================================
// OUTPUT INTERFACE DEFINITION
// ============================================================================

const OutputInterface bt_output_wiimote_interface = {
    .name    = "Wiimote (BT Classic)",
    .target  = OUTPUT_TARGET_WIIMOTE_BT,
    .init    = wiimote_init,
    .task    = wiimote_task,
    .core1_task = NULL,
    .get_feedback = NULL,
    .get_rumble = NULL,
    .get_player_led = NULL,
    .get_profile_count = NULL,
    .get_active_profile = NULL,
    .set_active_profile = NULL,
    .get_profile_name = NULL,
    .get_trigger_threshold = NULL,
    .get_native_config = NULL,
    .set_native_config = NULL,
};

// ============================================================================
// SHARED BT OUTPUT STUBS
// ============================================================================

void bt_output_init(void) {
    wiimote_init();
}

void bt_output_task(void) {
    wiimote_task();
}

bool bt_output_is_connected(void) {
    return connected;
}
