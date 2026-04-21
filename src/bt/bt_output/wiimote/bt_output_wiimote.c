// bt_output_wiimote.c - Wiimote-over-Bluetooth Classic HID device output
// SPDX-License-Identifier: Apache-2.0
//
// STATUS: scaffold. The OutputInterface is wired up so apps can include
// this module and the router submits input_event_t here, but the BTstack
// Classic HID device role, custom SDP records, PIN handshake, and Wiimote
// report generation are all TODO.
//
// Implementation plan lives in .dev/docs/picogamepad_absorption.md and will
// grow in the following files as work proceeds:
//   wiimote_reports.c     : HID input/output reports 0x11-0x1f, 0x20-0x3f
//   wiimote_eeprom.h      : calibration blob (clean-roomed from WiiBrew)
//   wiimote_ir.c          : IR sensor-bar dot synthesis
//   wiimote_ext_nunchuk.c : Nunchuk extension emulation
//   wiimote_ext_classic.c : Classic Controller extension emulation
//   wiimote_ext_crypto.c  : extension encryption s-boxes
//   wiimote_sdp.c         : custom SDP records mimicking RVL-CNT-01

#include "bt_output_wiimote.h"
#include "../bt_output.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// INTERNAL STATE (scaffold)
// ============================================================================

static wiimote_ext_mode_t current_ext = WIIMOTE_EXT_NONE;
static bool connected = false;

// ============================================================================
// OUTPUT INTERFACE CALLBACKS (scaffold)
// ============================================================================

static void wiimote_init(void) {
    printf("[bt_wiimote] init (scaffold — BTstack wiring TODO)\n");
    current_ext = WIIMOTE_EXT_NONE;
    connected = false;
}

static void wiimote_task(void) {
    // TODO: drive report pump, handle reconnect, service BTstack events if
    // needed (most BTstack work happens inside its event handler).
}

// Router tap — called by router when a new input_event_t arrives that's
// routed to OUTPUT_TARGET_WIIMOTE_BT. See router_set_tap() in app_init.
// TODO: translate input_event_t to Wiimote HID report and queue for send.
__attribute__((unused))
static void wiimote_router_tap(output_target_t target, uint8_t player_index,
                                const input_event_t* event) {
    (void)target;
    (void)player_index;
    (void)event;
    // TODO: regenerate 0x30-0x37 style input report from event + current_ext
}

// ============================================================================
// PUBLIC API
// ============================================================================

void bt_output_wiimote_set_ext(wiimote_ext_mode_t mode) {
    if (mode >= WIIMOTE_EXT_COUNT) return;
    if (mode == current_ext) return;
    printf("[bt_wiimote] ext mode %d -> %d (hot-plug)\n", current_ext, mode);
    current_ext = mode;
    // TODO: trigger extension-change notification to the Wii
    //   (send 0x20 status with extension-attached bit toggled, then the Wii
    //    will re-read the ext ID bytes at 0xFA)
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
    // Core 1 task not needed — BTstack is event-driven from its run loop
    .core1_task = NULL,
    // Feedback (rumble from Wii) will come later — Wiimote 0x13/0x19 output
    // reports carry rumble + LED state that we forward to input devices.
    .get_feedback = NULL,
    .get_rumble = NULL,
    .get_player_led = NULL,
    // No profile system yet
    .get_profile_count = NULL,
    .get_active_profile = NULL,
    .set_active_profile = NULL,
    .get_profile_name = NULL,
    .get_trigger_threshold = NULL,
    // No native-config JSON for this output (BT config lives elsewhere)
    .get_native_config = NULL,
    .set_native_config = NULL,
};

// ============================================================================
// SHARED BT OUTPUT STUBS (scaffold for src/bt/bt_output/bt_output.h)
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
