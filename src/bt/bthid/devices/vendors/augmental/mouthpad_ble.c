// mouthpad_ble.c - Augmental MouthPad BLE driver
//
// The MouthPad is a BLE mouth-controlled pointer. Over HID-over-GATT it is a
// mouse + keyboard + consumer-control device. Report map (verified against the
// Augmental mouthpad-usb reference firmware, usb_hid.c / ble_hid.c):
//
//   Report 1 (mouse buttons + wheel) : [id=1][buttons:5][wheel:i8]
//   Report 2 (mouse motion)          : [id=2][x_lo][x_hi<<4|y_lo][y_hi]   (12-bit X, 12-bit Y)
//   Report 3 (consumer)              : [id=3][usage_lo][usage_hi]          (16-bit Consumer page usage)
//   Report 4 (keyboard)              : [id=4][modifier][reserved][k1..k6]
//
// One logical mouse is split across reports 1 and 2 for BLE airtime reasons:
// motion (report 2) streams continuously; buttons/wheel (report 1) only fire on
// change. Because the two reports arrive as INDEPENDENT BLE notifications, this
// driver MUST hold persistent button state — a motion report must not clear
// buttons, and a button report must not re-emit stale motion. Wheel is a
// per-event delta: it fires once on its report and resets to 0 afterward.
//
// The MouthPad is submitted as INPUT_TYPE_MOUSE so it drives the SInput mouse
// interface (cursor + clicks) at full 12-bit precision. Mouse buttons are
// mapped to JP_BUTTON_B1/B2/B3 (left/right/middle — what sinput_mode reads for
// the mouse report) plus B4/L1 for the side buttons, so they remain remappable
// through profiles. Keyboard (report 4) and consumer (report 3) are parsed into
// the event for the keyboard/consumer output paths.

#include "mouthpad_ble.h"
#include "bt/bthid/bthid.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "core/buttons.h"
#include "core/services/keymap/keymap.h"
#include "core/services/players/manager.h"
#include "platform/platform.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ----------------------------------------------------------------------------
// MouthPad HID report IDs
// ----------------------------------------------------------------------------
#define MP_REPORT_MOUSE_BTN   0x01  // buttons + wheel
#define MP_REPORT_MOUSE_XY    0x02  // 12-bit X / 12-bit Y
#define MP_REPORT_CONSUMER    0x03  // 16-bit consumer usage
#define MP_REPORT_KEYBOARD    0x04  // modifier + 6KRO

// Report 1 mouse button bits
#define MP_MOUSE_BTN_LEFT     0x01
#define MP_MOUSE_BTN_RIGHT    0x02
#define MP_MOUSE_BTN_MIDDLE   0x04
#define MP_MOUSE_BTN_BACK     0x08  // side button 1
#define MP_MOUSE_BTN_FORWARD  0x10  // side button 2

// ----------------------------------------------------------------------------
// Driver data
// ----------------------------------------------------------------------------
// NOTE: input_event_t MUST be the first field — the BTHID layer reads
// device->driver_data as input_event_t* for battery/disconnect handling.
typedef struct {
    input_event_t event;
    uint8_t       mouse_buttons;   // last raw report-1 button bits (persistent)
    uint32_t      kbd_buttons;     // JP_BUTTON_* from keyboard keys (persistent)
    int16_t       aim_x, aim_y;    // accumulated pointing -> right-stick offset (±127)
    uint32_t      last_decay_ms;   // throttle for the idle recenter decay
    bool          initialized;
} mouthpad_data_t;

// Pointing -> right stick (aim) tuning. Pointing deltas (12-bit) accumulate into
// the right stick and HOLD (no decay) so a direction can be held; move the pointer
// back to return. No touch threshold, no sector debounce -> fast and light, and
// needs NO MouthPad profile (default mouse output).
#define MP_AIM_SENS_NUM   1     // delta scale numerator
#define MP_AIM_SENS_DEN   2     // delta scale denominator (dx*NUM/DEN per report)
#define MP_AIM_DEADZONE   2     // ignore |delta| < this (jitter floor)
// Right-stick AIM uses a mouse-look model: head motion deflects the stick, and it
// decays back to center when motion stops (camera turns, then stops). This is the
// correct model for an aim stick — and it can't peg/drift to the edge, so it won't
// dominate when blended with another controller's right stick.
#define MP_AIM_DECAY_MS    16   // recenter tick interval (~60 Hz)
#define MP_AIM_DECAY_SHIFT 2    // decay = value >> 2 (~25%) per tick (snappy stop)

static mouthpad_data_t mouthpad_data[BTHID_MAX_DEVICES];

// Translation mode. Default RIGHT_STICK so the MouthPad is a gamepad in every
// BT app; the `mouthpad-relay` build sets PASSTHROUGH via mouthpad_ble_set_mode().
static mp_mode_t mp_mode = MP_MODE_RIGHT_STICK;

// CDC command hook (MP.MODE): switch translation mode at runtime, per game.
void mouthpad_set_translation_mode(int mode)
{
    if (mode < 0 || mode > MP_MODE_LEFT_STICK) return;
    mouthpad_ble_set_mode((mp_mode_t)mode);
}

int mouthpad_get_translation_mode(void) { return (int)mp_mode; }

void mouthpad_ble_set_mode(mp_mode_t mode)
{
    mp_mode = mode;
    printf("[MOUTHPAD_BLE] translation mode -> %d (%s)\n", (int)mode,
           mode == MP_MODE_PASSTHROUGH ? "passthrough" :
           mode == MP_MODE_LEFT_STICK  ? "left_stick"  : "right_stick");
    input_device_type_t t = (mode == MP_MODE_PASSTHROUGH) ? INPUT_TYPE_MOUSE : INPUT_TYPE_GAMEPAD;
    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (!mouthpad_data[i].initialized) continue;
        mouthpad_data[i].event.type = t;
        mouthpad_data[i].aim_x = 0;
        mouthpad_data[i].aim_y = 0;
        mouthpad_data[i].event.analog[ANALOG_LX] = 128;
        mouthpad_data[i].event.analog[ANALOG_LY] = 128;
        mouthpad_data[i].event.analog[ANALOG_RX] = 128;
        mouthpad_data[i].event.analog[ANALOG_RY] = 128;
    }
}

// ----------------------------------------------------------------------------
// Map raw MouthPad mouse buttons -> JP_BUTTON_* (kept persistent across reports).
// In a stick mode the LEFT click is the aim RECENTER (handled separately), so it
// is NOT a gamepad button; in PASSTHROUGH the left click is a normal button.
// ----------------------------------------------------------------------------
static uint32_t map_mouse_buttons(uint8_t raw, bool include_left)
{
    uint32_t b = 0;
    if (include_left && (raw & MP_MOUSE_BTN_LEFT)) b |= JP_BUTTON_B1;  // left
    if (raw & MP_MOUSE_BTN_RIGHT)   b |= JP_BUTTON_B2;  // right
    if (raw & MP_MOUSE_BTN_MIDDLE)  b |= JP_BUTTON_B3;  // middle
    if (raw & MP_MOUSE_BTN_BACK)    b |= JP_BUTTON_B4;  // side -> remappable
    if (raw & MP_MOUSE_BTN_FORWARD) b |= JP_BUTTON_L1;  // side -> remappable
    return b;
}

// Convenience: include the left click as a button only in PASSTHROUGH mode.
static inline uint32_t mp_buttons(uint8_t raw)
{
    return map_mouse_buttons(raw, mp_mode == MP_MODE_PASSTHROUGH);
}

// Sign-extend a 12-bit value to int16
static inline int16_t sext12(uint16_t v)
{
    v &= 0x0FFF;
    return (v & 0x0800) ? (int16_t)(v - 0x1000) : (int16_t)v;
}

// ----------------------------------------------------------------------------
// Driver callbacks
// ----------------------------------------------------------------------------
// Augmental MouthPad DIS PnP ID (vendor_source=2, Nordic VID 0x1915, PID 0xEEEE).
#define MP_DIS_VID  0x1915
#define MP_DIS_PID  0xEEEE

static bool mouthpad_match(const char* device_name, const uint8_t* class_of_device,
                           uint16_t vendor_id, uint16_t product_id, bool is_ble)
{
    (void)class_of_device;

    // MouthPad is BLE-only.
    if (!is_ble) return false;
    // Primary: advertised name contains "MouthPad" (available at connect time).
    if (device_name && strstr(device_name, "MouthPad") != NULL) return true;
    // Fallback: DIS PnP VID/PID — names can be reset to dev values (e.g. "TL_DEV")
    // that lack "MouthPad". VID/PID arrive after connect (DIS); bthid re-evaluates
    // the driver then, so this still selects the MouthPad driver.
    if (vendor_id == MP_DIS_VID && product_id == MP_DIS_PID) return true;
    return false;
}

static bool mouthpad_init(bthid_device_t* device)
{
    printf("[MOUTHPAD_BLE] Init for device: %s\n", device->name);
    device->type = BTHID_DEVICE_MOUSE;

    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (!mouthpad_data[i].initialized) {
            init_input_event(&mouthpad_data[i].event);
            mouthpad_data[i].mouse_buttons = 0;
            mouthpad_data[i].kbd_buttons = 0;
            mouthpad_data[i].aim_x = 0;
            mouthpad_data[i].aim_y = 0;
            mouthpad_data[i].last_decay_ms = 0;
            mouthpad_data[i].initialized = true;

            // Mode decides how the driver presents the MouthPad: PASSTHROUGH =
            // MOUSE (cursor + clicks + keyboard, for `mouthpad-relay`), otherwise
            // GAMEPAD (pointing → stick + clicks → buttons, the default everywhere).
            mouthpad_data[i].event.type      = (mp_mode == MP_MODE_PASSTHROUGH)
                                                ? INPUT_TYPE_MOUSE : INPUT_TYPE_GAMEPAD;
            mouthpad_data[i].event.dev_addr  = device->conn_index;
            mouthpad_data[i].event.instance  = 0;
            mouthpad_data[i].event.transport = INPUT_TRANSPORT_BT_BLE;

            device->driver_data = &mouthpad_data[i];
            return true;
        }
    }
    printf("[MOUTHPAD_BLE] No free data slot!\n");
    return false;
}

static void mouthpad_process_report(bthid_device_t* device, const uint8_t* data, uint16_t len)
{
    mouthpad_data_t* md = (mouthpad_data_t*)device->driver_data;
    if (!md || len < 1) return;

    uint8_t report_id = data[0];
    const uint8_t* p = data + 1;     // payload after report ID
    uint16_t plen = len - 1;

    input_event_t* ev = &md->event;

    switch (report_id) {
        case MP_REPORT_MOUSE_BTN: {
            // [buttons][wheel]
            if (plen < 1) return;
            uint8_t prev = md->mouse_buttons;
            md->mouse_buttons = p[0];
            // STICK modes only: LEFT click -> RECENTER the held aim stick (the
            // return-to-center the hold model needs). In PASSTHROUGH the left click
            // is a normal mouse button instead (mp_buttons() includes it there).
            if (mp_mode != MP_MODE_PASSTHROUGH &&
                (md->mouse_buttons & MP_MOUSE_BTN_LEFT) && !(prev & MP_MOUSE_BTN_LEFT)) {
                md->aim_x = 0;
                md->aim_y = 0;
                ev->analog[ANALOG_RX] = 128;
                ev->analog[ANALOG_RY] = 128;
                ev->analog[ANALOG_LX] = 128;
                ev->analog[ANALOG_LY] = 128;
            }
            ev->buttons  = mp_buttons(md->mouse_buttons) | md->kbd_buttons;
            ev->delta_x  = 0;        // this report carries no motion
            ev->delta_y  = 0;
            ev->delta_wheel = (plen >= 2) ? (int8_t)p[1] : 0;
            router_submit_input(ev);
            ev->delta_wheel = 0;     // wheel is one-shot
            break;
        }

        case MP_REPORT_MOUSE_XY: {
            // [x_lo][x_hi<<4 | y_lo][y_hi]  -> 12-bit signed X, 12-bit signed Y
            if (plen < 3) return;
            uint16_t xr = (uint16_t)p[0] | ((uint16_t)(p[1] & 0x0F) << 8);
            uint16_t yr = (uint16_t)(p[1] >> 4) | ((uint16_t)p[2] << 4);

            if (mp_mode == MP_MODE_PASSTHROUGH) {
                // Plain cursor: pass the 12-bit motion straight through to the mouse.
                ev->buttons     = mp_buttons(md->mouse_buttons) | md->kbd_buttons;
                ev->delta_x     = sext12(xr);
                ev->delta_y     = sext12(yr);
                ev->delta_wheel = 0;
                router_submit_input(ev);
                break;
            }

            // Stick mode: pointing -> stick (HOLD). Accumulate scaled motion so a
            // direction can be HELD (move to deflect, stop and it stays). A small
            // deadzone ignores sub-threshold jitter. CLUTCH: while LEFT click is
            // held, force the stick to center and ignore motion — reposition your
            // head freely, release to resume from neutral (tap = recenter).
            if (md->mouse_buttons & MP_MOUSE_BTN_LEFT) {
                md->aim_x = 0;
                md->aim_y = 0;
            } else {
                int16_t dx = sext12(xr), dy = sext12(yr);
                if (dx > -MP_AIM_DEADZONE && dx < MP_AIM_DEADZONE) dx = 0;
                if (dy > -MP_AIM_DEADZONE && dy < MP_AIM_DEADZONE) dy = 0;
                md->aim_x += dx * MP_AIM_SENS_NUM / MP_AIM_SENS_DEN;
                md->aim_y += dy * MP_AIM_SENS_NUM / MP_AIM_SENS_DEN;
                if (md->aim_x >  127) md->aim_x =  127;
                if (md->aim_x < -127) md->aim_x = -127;
                if (md->aim_y >  127) md->aim_y =  127;
                if (md->aim_y < -127) md->aim_y = -127;
            }
            // LEFT_STICK mode -> LX/LY, otherwise RIGHT_STICK -> RX/RY.
            uint8_t ax = (mp_mode == MP_MODE_LEFT_STICK) ? ANALOG_LX : ANALOG_RX;
            uint8_t ay = (mp_mode == MP_MODE_LEFT_STICK) ? ANALOG_LY : ANALOG_RY;
            ev->analog[ax]  = (uint8_t)(128 + md->aim_x);
            ev->analog[ay]  = (uint8_t)(128 + md->aim_y);
            ev->buttons     = mp_buttons(md->mouse_buttons) | md->kbd_buttons;
            ev->delta_x     = 0;
            ev->delta_y     = 0;
            ev->delta_wheel = 0;
            router_submit_input(ev);
            break;
        }

        case MP_REPORT_CONSUMER: {
            // [usage_lo][usage_hi] — 16-bit Consumer page usage.
            // TODO(phase 2.5): emit via the consumer-control output channel.
            // For now parse it so it's ready; mouse motion/buttons unaffected.
            if (plen >= 2) {
                ev->consumer_usage = (uint16_t)p[0] | ((uint16_t)p[1] << 8);
            } else if (plen == 1) {
                ev->consumer_usage = p[0];  // legacy 1-byte bitmap (raw, not translated yet)
            }
            break;
        }

        case MP_REPORT_KEYBOARD: {
            // [modifier][reserved][k1..k6]
            if (plen < 1) return;
            ev->kb_modifier = p[0];
            memset(ev->kb_keys, 0, sizeof(ev->kb_keys));
            for (int i = 0; i < 6 && (uint16_t)(i + 2) < plen; i++) {
                ev->kb_keys[i] = p[i + 2];
            }
            // Profile Bridge: a MouthPad running a key-emitting .mkprofile maps
            // touch sectors / sip / puff / swipes to keystrokes. Turn those into
            // gamepad input so they route through the normal profile/remap
            // pipeline to any gamepad output. Directional keys (the touch sectors)
            // drive the LEFT STICK as 8-way full deflection — hold = move, release
            // (no_touch -> empty report) = stick recenters. Non-directional keys
            // (sip/puff/swipes) stay as buttons. kbd_buttons is persistent so a
            // later mouse-motion report doesn't drop held buttons; the stick value
            // likewise persists in ev->analog across mouse reports.
            uint32_t keys = keymap_keys_to_buttons(ev->kb_keys, 6, ev->kb_modifier);
            uint8_t lx = 128, ly = 128;
            if (keys & JP_BUTTON_DL)      lx = 0;
            else if (keys & JP_BUTTON_DR) lx = 255;
            if (keys & JP_BUTTON_DU)      ly = 0;
            else if (keys & JP_BUTTON_DD) ly = 255;
            ev->analog[ANALOG_LX] = lx;
            ev->analog[ANALOG_LY] = ly;
            // Buttons = gestures only (strip the directional bits now on the stick).
            md->kbd_buttons = keys & ~(JP_BUTTON_DU | JP_BUTTON_DD | JP_BUTTON_DL | JP_BUTTON_DR);
            ev->buttons     = mp_buttons(md->mouse_buttons) | md->kbd_buttons;
            ev->delta_x     = 0;     // keyboard report carries no motion
            ev->delta_y     = 0;
            ev->delta_wheel = 0;
            router_submit_input(ev);
            break;
        }

        default:
            // Unknown / boot-protocol report — ignore for now (driver prefers
            // REPORT protocol; boot-mouse fallback is a future refinement).
            break;
    }
}

static void mouthpad_task(bthid_device_t* device)
{
    (void)device;  // Aim stick HOLDS its position — head motion deflects it and it
                   // STAYS there (no auto-decay). Recenter with the left-click
                   // clutch (tap = recenter, hold = keep centered while you
                   // reposition your head). This is the intended behavior.
}

static void mouthpad_disconnect(bthid_device_t* device)
{
    printf("[MOUTHPAD_BLE] Disconnect: %s\n", device->name);
    mouthpad_data_t* md = (mouthpad_data_t*)device->driver_data;
    if (md) {
        router_device_disconnected(md->event.dev_addr, md->event.instance);
        remove_players_by_address(md->event.dev_addr, md->event.instance);
        init_input_event(&md->event);
        md->mouse_buttons = 0;
        md->kbd_buttons = 0;
        md->aim_x = 0;
        md->aim_y = 0;
        md->initialized = false;
    }
}

// ----------------------------------------------------------------------------
// Driver struct + registration
// ----------------------------------------------------------------------------
const bthid_driver_t mouthpad_ble_driver = {
    .name = "Augmental MouthPad BLE",
    .match = mouthpad_match,
    .init = mouthpad_init,
    .process_report = mouthpad_process_report,
    .task = mouthpad_task,
    .disconnect = mouthpad_disconnect,
};

void mouthpad_ble_register(void)
{
    bthid_register_driver(&mouthpad_ble_driver);
}
