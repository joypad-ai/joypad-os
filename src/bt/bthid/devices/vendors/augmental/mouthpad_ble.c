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
#include "core/services/players/manager.h"
#include <string.h>
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
    bool          initialized;
} mouthpad_data_t;

static mouthpad_data_t mouthpad_data[BTHID_MAX_DEVICES];

// ----------------------------------------------------------------------------
// Map raw MouthPad mouse buttons -> JP_BUTTON_* (kept persistent across reports)
// ----------------------------------------------------------------------------
static uint32_t map_mouse_buttons(uint8_t raw)
{
    uint32_t b = 0;
    if (raw & MP_MOUSE_BTN_LEFT)    b |= JP_BUTTON_B1;  // sinput mouse: left
    if (raw & MP_MOUSE_BTN_RIGHT)   b |= JP_BUTTON_B2;  // sinput mouse: right
    if (raw & MP_MOUSE_BTN_MIDDLE)  b |= JP_BUTTON_B3;  // sinput mouse: middle
    if (raw & MP_MOUSE_BTN_BACK)    b |= JP_BUTTON_B4;  // side -> remappable
    if (raw & MP_MOUSE_BTN_FORWARD) b |= JP_BUTTON_L1;  // side -> remappable
    return b;
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
static bool mouthpad_match(const char* device_name, const uint8_t* class_of_device,
                           uint16_t vendor_id, uint16_t product_id, bool is_ble)
{
    (void)class_of_device; (void)vendor_id; (void)product_id;

    // MouthPad is BLE-only and advertises a name containing "MouthPad".
    if (!is_ble) return false;
    if (device_name && strstr(device_name, "MouthPad") != NULL) return true;
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
            mouthpad_data[i].initialized = true;

            mouthpad_data[i].event.type      = INPUT_TYPE_MOUSE;
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
            md->mouse_buttons = p[0];
            ev->buttons  = map_mouse_buttons(md->mouse_buttons);
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
            ev->buttons     = map_mouse_buttons(md->mouse_buttons);  // keep button state
            ev->delta_x     = sext12(xr);
            ev->delta_y     = sext12(yr);
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
            // TODO(phase 2.5): keyboard emission for MOUSE-type events.
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
    (void)device;  // No periodic output (no rumble on MouthPad).
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
