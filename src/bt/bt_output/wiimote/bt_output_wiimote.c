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

// Build the byte stream for the currently-selected extension into `out`
// (6 bytes for Nunchuk / Classic, 0 for None). Returns bytes written.
static uint16_t build_ext_payload(uint8_t* out) {
    wm_ext_kind_t kind = WM_EXT_NONE;
    switch (current_ext) {
        case WIIMOTE_EXT_NUNCHUK:     kind = WM_EXT_NUNCHUK; break;
        case WIIMOTE_EXT_CLASSIC:     kind = WM_EXT_CLASSIC; break;
        case WIIMOTE_EXT_CLASSIC_PRO: kind = WM_EXT_CLASSIC_PRO; break;
        case WIIMOTE_EXT_NONE:        return 0;
        default: return 0;
    }
    return wiimote_ext_build_payload(kind, event_valid ? &last_event : NULL, out);
}

// Answer a 0x17 memory-read request by returning a 0x21 response.
static void answer_memory_read(const uint8_t* report, uint16_t len) {
    if (len < 7) return;

    uint8_t  flags = report[1];
    uint32_t addr  = ((uint32_t)report[2] << 16) | ((uint32_t)report[3] << 8) | report[4];
    uint16_t size  = ((uint16_t)report[5] << 8) | report[6];
    bool     is_register = (flags & 0x04) != 0;    // 0=EEPROM, 1=control register

    if (size > 16) size = 16;   // 0x21 payload holds at most 16 bytes
    if (size == 0) return;

    uint8_t buf[22] = {0};
    buf[0] = 0x21;
    uint16_t btns = event_valid ? wiimote_buttons_from_event(&last_event) : 0;
    buf[1] = btns & 0xff;
    buf[2] = (btns >> 8) & 0xff;
    buf[3] = ((size - 1) & 0x0f) << 4;      // upper nibble = size-1, lower = err=0
    buf[4] = (addr >> 8) & 0xff;
    buf[5] = addr & 0xff;

    if (is_register && (addr & 0x00FF0000) == 0x00A40000) {
        // Extension register space. Address within 0xA400xx - 0xA400FF.
        uint32_t ext_addr = addr & 0xFF;

        // Resolve the current extension kind once.
        wm_ext_kind_t kind = (current_ext == WIIMOTE_EXT_NUNCHUK)     ? WM_EXT_NUNCHUK
                           : (current_ext == WIIMOTE_EXT_CLASSIC)     ? WM_EXT_CLASSIC
                           : (current_ext == WIIMOTE_EXT_CLASSIC_PRO) ? WM_EXT_CLASSIC_PRO
                           : WM_EXT_NONE;

        // Pre-compute calibration + ID blocks so bulk reads don't re-call
        // the builders per byte.
        uint8_t calib[16];
        wiimote_ext_build_calibration(kind, calib);
        uint8_t id_bytes[6];
        bool id_valid = wiimote_ext_get_id(kind, id_bytes);

        for (uint16_t i = 0; i < size; i++) {
            uint32_t a = ext_addr + i;
            uint8_t  b = 0x00;
            if (a >= 0x20 && a < 0x30) {
                b = calib[a - 0x20];
            } else if (a >= 0x30 && a < 0x40) {
                // Real Wiimote duplicates the cal block.
                b = calib[a - 0x30];
            } else if (a >= 0xFA && a <= 0xFF) {
                if (id_valid) b = id_bytes[a - 0xFA];
            }
            buf[6 + i] = b;
        }
    } else {
        // EEPROM space.
        wiimote_eeprom_read_block(addr, &buf[6], size);
    }

    wiimote_sdp_send_report(buf, 6 + size);
}

static void queue_status_report(void) {
    uint8_t status_buf[8];
    uint16_t n = wiimote_build_status(&wm_state,
                                      event_valid ? &last_event : NULL,
                                      status_buf);
    wiimote_sdp_send_report(status_buf, n);
}

// Handle a 0x16 memory write. Payload (after 0x16):
//   byte 1    : flags (bit 0 = rumble, bit 2 = register space)
//   byte 2-4  : 24-bit address
//   byte 5    : size (1-16)
//   byte 6-21 : data bytes (up to 16)
// Acknowledge with a 0x22 report: [0x22, btn_lo, btn_hi, report_id_that_caused, error]
static void answer_memory_write(const uint8_t* report, uint16_t len) {
    if (len < 7) return;

    uint8_t  flags = report[1];
    uint32_t addr  = ((uint32_t)report[2] << 16) | ((uint32_t)report[3] << 8) | report[4];
    uint8_t  size  = report[5];
    bool     is_register = (flags & 0x04) != 0;

    if (size > 16) size = 16;
    if (len < (uint16_t)(6 + size)) size = (uint16_t)(len - 6);

    if (is_register && (addr & 0x00FF0000) == 0x00A40000) {
        uint32_t ext_addr = addr & 0xFF;
        for (uint8_t i = 0; i < size; i++) {
            uint32_t a = ext_addr + i;
            uint8_t  b = report[6 + i];
            if (a >= 0x40 && a < 0x50) {
                // Encryption key — 16 bytes at 0x40-0x4F.
                wm_state.ext_key[a - 0x40] = b;
                if (a == 0x4F) {
                    wm_state.ext_key_set = true;
                    // Receiving a key implies the Wii intends to use
                    // encryption from this point forward. A subsequent
                    // write to 0xF0 can still flip it off.
                    wm_state.ext_encrypted = true;
                    printf("[bt_wiimote] ext encryption key armed\n");
                }
            } else if (a == 0xF0) {
                // 0xAA enables encryption, 0x55 / 0x00 disables.
                bool new_enc = (b == 0xAA);
                if (new_enc != wm_state.ext_encrypted) {
                    printf("[bt_wiimote] ext encryption %s\n", new_enc ? "ON" : "OFF");
                }
                wm_state.ext_encrypted = new_enc;
            }
            // Other writes silently accepted.
        }
    }
    // EEPROM writes (is_register==false) are ignored — we're read-only.

    // 0x22 ack: report ID, 2 byte buttons, caused-report-ID, error code.
    uint8_t ack[5];
    ack[0] = 0x22;
    uint16_t btns = event_valid ? wiimote_buttons_from_event(&last_event) : 0;
    ack[1] = btns & 0xff;
    ack[2] = (btns >> 8) & 0xff;
    ack[3] = 0x16;
    ack[4] = 0x00;  // 0 = success
    wiimote_sdp_send_report(ack, sizeof(ack));
}

void bt_output_wiimote_handle_output_report(const uint8_t* report, uint16_t len) {
    if (!report || len < 1) return;

    uint8_t id = report[0];

    if (id == 0x16) {
        answer_memory_write(report, len);
        return;
    }
    if (id == 0x17) {
        answer_memory_read(report, len);
        return;
    }

    bool need_status = wiimote_apply_output(&wm_state, report, len);
    if (need_status) queue_status_report();
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

    uint8_t ext_buf[6];
    uint16_t ext_len = build_ext_payload(ext_buf);

    uint8_t buf[23];
    uint16_t n = wiimote_build_current(&wm_state, &last_event,
                                       ext_len ? ext_buf : NULL, ext_len, buf);
    wiimote_sdp_send_report(buf, n);
}

// ============================================================================
// PUBLIC API
// ============================================================================

void bt_output_wiimote_set_ext(wiimote_ext_mode_t mode) {
    if (mode >= WIIMOTE_EXT_COUNT) return;
    if (mode == current_ext) return;
    printf("[bt_wiimote] ext mode %d -> %d (hot-plug)\n", current_ext, mode);

    // Wiimote hot-plug protocol: briefly report "no extension" (so the Wii
    // sees the previous one detach), then report "extension attached" with
    // the new ID. The Wii re-reads 0xFA-0xFF when it sees the ext bit go
    // high again.
    current_ext = WIIMOTE_EXT_NONE;
    wm_state.extension_attached = false;
    if (wiimote_sdp_is_connected()) queue_status_report();

    current_ext = mode;
    wm_state.extension_attached = (mode != WIIMOTE_EXT_NONE);
    if (wiimote_sdp_is_connected()) queue_status_report();
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
