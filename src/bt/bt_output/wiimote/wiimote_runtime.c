// wiimote_runtime.c - Wiimote mode runtime (state + report pump)
// SPDX-License-Identifier: Apache-2.0

#include "wiimote_runtime.h"
#include "wiimote_reports.h"
#include "wiimote_eeprom.h"
#include "wiimote_ext.h"
#include "wiimote_ir.h"
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
static wiimote_ir_state_t   ir_state;

// Cached most-recent router event. We don't generate a report unless
// something changed OR the Wii requested continuous mode.
static input_event_t        last_event;
static bool                 event_valid = false;
static bool                 event_dirty = false;

// ============================================================================
// HELPERS
// ============================================================================

// Map the public wiimote_ext_mode_t to the internal wm_ext_kind_t used by
// wiimote_ext.h builders.
static wm_ext_kind_t ext_kind_for(wiimote_ext_mode_t m) {
    switch (m) {
        case WIIMOTE_EXT_NUNCHUK:     return WM_EXT_NUNCHUK;
        case WIIMOTE_EXT_CLASSIC:     return WM_EXT_CLASSIC;
        case WIIMOTE_EXT_CLASSIC_PRO: return WM_EXT_CLASSIC_PRO;
        case WIIMOTE_EXT_NONE:
        default:                      return WM_EXT_NONE;
    }
}

// Build the byte stream for the currently-selected extension into `out`
// (6 bytes for Nunchuk / Classic, 0 for None).
static uint16_t build_ext_payload(uint8_t* out) {
    wm_ext_kind_t kind = ext_kind_for(current_ext);
    if (kind == WM_EXT_NONE) return 0;
    return wiimote_ext_build_payload(kind, event_valid ? &last_event : NULL, out);
}

static void queue_status_report(void) {
    uint8_t status_buf[8];
    uint16_t n = wiimote_build_status(&wm_state,
                                      event_valid ? &last_event : NULL,
                                      status_buf);
    wiimote_sdp_send_report(status_buf, n);
}

// Derive the virtual IR pointer from the cached input event.
static void update_ir_state(void) {
    if (!event_valid) {
        ir_state.active = false;
        return;
    }
    // Right-stick 0..255 -> 0..1, centred at 128.
    ir_state.x = (float)last_event.analog[ANALOG_RX] / 255.0f;
    ir_state.y = (float)last_event.analog[ANALOG_RY] / 255.0f;
    ir_state.active = true;
    ir_state.bar_above_tv = true;
}

// Answer a 0x17 memory-read request with a 0x21 response.
static void answer_memory_read(const uint8_t* report, uint16_t len) {
    if (len < 7) return;

    uint8_t  flags = report[1];
    uint32_t addr  = ((uint32_t)report[2] << 16) | ((uint32_t)report[3] << 8) | report[4];
    uint16_t size  = ((uint16_t)report[5] << 8) | report[6];
    bool     is_register = (flags & 0x04) != 0;

    if (size > 16) size = 16;
    if (size == 0) return;

    uint8_t buf[22] = {0};
    buf[0] = 0x21;
    uint16_t btns = event_valid ? wiimote_buttons_from_event(&last_event) : 0;
    buf[1] = btns & 0xff;
    buf[2] = (btns >> 8) & 0xff;
    buf[3] = ((size - 1) & 0x0f) << 4;
    buf[4] = (addr >> 8) & 0xff;
    buf[5] = addr & 0xff;

    if (is_register && (addr & 0x00FF0000) == 0x00A40000) {
        uint32_t ext_addr = addr & 0xFF;
        wm_ext_kind_t kind = ext_kind_for(current_ext);
        uint8_t calib[16];
        wiimote_ext_build_calibration(kind, calib);
        uint8_t id_bytes[6];
        bool id_valid = wiimote_ext_get_id(kind, id_bytes);

        for (uint16_t i = 0; i < size; i++) {
            uint32_t a = ext_addr + i;
            uint8_t  b = 0x00;
            if      (a >= 0x20 && a < 0x30) b = calib[a - 0x20];
            else if (a >= 0x30 && a < 0x40) b = calib[a - 0x30];  // real Wiimote duplicates
            else if (a >= 0xFA && a <= 0xFF && id_valid) b = id_bytes[a - 0xFA];
            buf[6 + i] = b;
        }
    } else {
        wiimote_eeprom_read_block(addr, &buf[6], size);
    }

    wiimote_sdp_send_report(buf, 6 + size);
}

// Handle a 0x16 memory write from the Wii. Captures encryption key + enable.
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
                wm_state.ext_key[a - 0x40] = b;
                if (a == 0x4F) {
                    wm_state.ext_key_set = true;
                    wm_state.ext_encrypted = true;
                    printf("[wiimote] ext encryption key armed\n");
                }
            } else if (a == 0xF0) {
                bool new_enc = (b == 0xAA);
                if (new_enc != wm_state.ext_encrypted) {
                    printf("[wiimote] ext encryption %s\n", new_enc ? "ON" : "OFF");
                }
                wm_state.ext_encrypted = new_enc;
            }
        }
    }

    // 0x22 ack
    uint8_t ack[5];
    ack[0] = 0x22;
    uint16_t btns = event_valid ? wiimote_buttons_from_event(&last_event) : 0;
    ack[1] = btns & 0xff;
    ack[2] = (btns >> 8) & 0xff;
    ack[3] = 0x16;
    ack[4] = 0x00;
    wiimote_sdp_send_report(ack, sizeof(ack));
}

// ============================================================================
// PUBLIC API
// ============================================================================

void wiimote_runtime_init(void) {
    printf("[wiimote] runtime init\n");
    wiimote_state_init(&wm_state);
    current_ext = WIIMOTE_EXT_NONE;
    event_valid = false;
    event_dirty = false;
    memset(&last_event, 0, sizeof(last_event));

    wiimote_sdp_register();
}

void wiimote_runtime_handle_output_report(const uint8_t* report, uint16_t len) {
    if (!report || len < 1) return;

    uint8_t id = report[0];

    if (id == 0x16) { answer_memory_write(report, len); return; }
    if (id == 0x17) { answer_memory_read(report, len); return; }

    bool need_status = wiimote_apply_output(&wm_state, report, len);
    if (need_status) queue_status_report();
}

void wiimote_runtime_task(void) {
    if (!wiimote_sdp_is_connected()) return;

    // Poll the router for the latest event on OUTPUT_TARGET_BT.
    const input_event_t* e = router_get_output(OUTPUT_TARGET_BT, 0);
    if (e) {
        last_event  = *e;
        event_valid = true;
        event_dirty = true;
    }

    if (!event_valid) return;
    if (!event_dirty && !wm_state.continuous) return;
    event_dirty = false;

    uint8_t ext_buf[6];
    uint16_t ext_len = build_ext_payload(ext_buf);

    update_ir_state();
    uint8_t ir12[12];
    uint8_t ir10[10];
    wiimote_ir_pack_extended(&ir_state, ir12);
    wiimote_ir_pack_basic(&ir_state, ir10);

    uint8_t buf[23];
    uint16_t n = wiimote_build_current(&wm_state, &last_event,
                                       ext_len ? ext_buf : NULL, ext_len,
                                       ir12, ir10, buf);
    wiimote_sdp_send_report(buf, n);
}

void wiimote_runtime_set_ext(wiimote_ext_mode_t mode) {
    if (mode >= WIIMOTE_EXT_COUNT) return;
    if (mode == current_ext) return;
    printf("[wiimote] ext mode %d -> %d (hot-plug)\n", current_ext, mode);

    // Hot-plug handshake: unplug -> plug with new kind.
    current_ext = WIIMOTE_EXT_NONE;
    wm_state.extension_attached = false;
    if (wiimote_sdp_is_connected()) queue_status_report();

    current_ext = mode;
    wm_state.extension_attached = (mode != WIIMOTE_EXT_NONE);
    if (wiimote_sdp_is_connected()) queue_status_report();
}

wiimote_ext_mode_t wiimote_runtime_get_ext(void) {
    return current_ext;
}

bool wiimote_runtime_is_connected(void) {
    return wiimote_sdp_is_connected();
}
