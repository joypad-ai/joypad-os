// ble_output.c - BLE HID Output Interface (HOGP Peripheral)
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Implements OutputInterface for BLE HID output using BTstack's hids_device
// GATT service. Supports two modes:
//   - Standard: Composite gamepad + keyboard + mouse (ESP32-BLE-CompositeHID compatible)
//   - Xbox BLE: Xbox One S / Series X compatible gamepad with rumble

#include "app.h"
#include "ble_output.h"
#include "ble_output_keyboard.h"
#include "ble_output_mouse.h"
#include "ble_output_xbox.h"
#ifdef CONFIG_BT_CLASSIC_OUTPUT
#include "switch_bt/switch_bt.h"
#endif
#include "ble_nus.h"
#include "ble_gamepad.h"  // Generated from ble_gamepad.gatt by compile_gatt.py

// Xbox GATT database (compiled from ble_xbox.gatt, wrapped in ble_xbox_gatt_db.c)
extern const uint8_t *ble_xbox_profile_data;

#include "core/buttons.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "core/services/players/feedback.h"
#include "core/services/storage/flash.h"
#include "usb/usbd/cdc/cdc_commands.h"
#include "usb/usbd/usbd.h"
#include "platform/platform.h"

// Forward declare to avoid pulling in manager.h (TinyUSB type conflicts)
extern void feedback_set_rumble(uint8_t player_index, uint8_t left, uint8_t right);

// BTstack includes
#include "btstack_defines.h"
#include "btstack_event.h"
#include "bluetooth_data_types.h"
#include "bluetooth_gatt.h"
#include "gap.h"
#include "l2cap.h"
#include "ble/att_db.h"
#include "ble/att_server.h"
#include "ble/sm.h"
#include "ble/gatt-service/battery_service_server.h"
#include "ble/gatt-service/device_information_service_server.h"
#include "ble/gatt-service/hids_device.h"

#include "usb/usbd/modes/sinput_mode.h"  // SInput report/feature builders (shared)

#include <stdio.h>
#include <string.h>

// ============================================================================
// HID REPORT DESCRIPTOR — Standard Composite: Keyboard + Mouse + Gamepad
// ============================================================================

static const uint8_t standard_hid_descriptor[] = {
    // ---- Keyboard (Report ID 1) ----
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)

    // Modifier keys (8 bits)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0xE0,        //   Usage Minimum (224 - Left Control)
    0x29, 0xE7,        //   Usage Maximum (231 - Right GUI)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)

    // Reserved byte
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Constant)

    // LED output report (Caps/Num/Scroll Lock)
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (1 - Num Lock)
    0x29, 0x05,        //   Usage Maximum (5 - Kana)
    0x91, 0x02,        //   Output (Data, Variable, Absolute)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x01,        //   Output (Constant) - padding

    // Keycodes (6 keys)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0x65,        //   Usage Maximum (101)
    0x81, 0x00,        //   Input (Data, Array)

    0xC0,              // End Collection

    // ---- Mouse (Report ID 2) ----
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID (2)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)

    // 5 Buttons
    0x05, 0x09,        //     Usage Page (Button)
    0x19, 0x01,        //     Usage Minimum (1)
    0x29, 0x05,        //     Usage Maximum (5)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x05,        //     Report Count (5)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data, Variable, Absolute)

    // 3 bits padding
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x03,        //     Report Size (3)
    0x81, 0x01,        //     Input (Constant)

    // X, Y movement (-127 to 127)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x02,        //     Report Count (2)
    0x81, 0x06,        //     Input (Data, Variable, Relative)

    // Vertical wheel (-127 to 127)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x06,        //     Input (Data, Variable, Relative)

    0xC0,              //   End Collection (Physical)
    0xC0,              // End Collection (Mouse)

    // ---- Gamepad (Report ID 3) ----
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x03,        //   Report ID (3)

    // 16 buttons = 2 bytes
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x10,        //   Usage Maximum (Button 16)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x10,        //   Report Count (16)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    // Hat switch (8 bits: values 1-8 = directions, 0 = center/null)
    0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
    0x09, 0x39,        //   Usage (Hat switch)
    0x15, 0x01,        //   Logical Minimum (1)
    0x25, 0x08,        //   Logical Maximum (8)
    0x35, 0x00,        //   Physical Minimum (0)
    0x46, 0x3B, 0x01,  //   Physical Maximum (315)
    0x65, 0x14,        //   Unit (Eng Rot:Angular Pos)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x42,        //   Input (Data,Var,Abs,Null)
    0x65, 0x00,        //   Unit (None)

    // 6 axes x 16-bit: X, Y, Z, Rz (sticks), Rx, Ry (triggers)
    0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
    0x15, 0x00,        //   Logical Minimum (0)
    0x27, 0xFF, 0x7F, 0x00, 0x00,  // Logical Maximum (0x7FFF = 32767)
    0x35, 0x00,        //   Physical Minimum (0)
    0x47, 0xFF, 0x7F, 0x00, 0x00,  // Physical Maximum (0x7FFF = 32767)
    0x09, 0x30,        //   Usage (X)  - Left Stick X
    0x09, 0x31,        //   Usage (Y)  - Left Stick Y
    0x09, 0x32,        //   Usage (Z)  - Right Stick X
    0x09, 0x35,        //   Usage (Rz) - Right Stick Y
    0x09, 0x33,        //   Usage (Rx) - Left Trigger
    0x09, 0x34,        //   Usage (Ry) - Right Trigger
    0x75, 0x10,        //   Report Size (16)
    0x95, 0x06,        //   Report Count (6)
    0x81, 0x02,        //   Input (Data,Var,Abs)

    0xC0,              // End Collection

    // ---- Player Indicator Output (Report ID 4) ----
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x04,        //   Report ID (4)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x09, 0x4B,        //   Usage (Player Indicator)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0xFF,        //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0x91, 0x02,        //   Output (Data, Variable, Absolute)
    0xC0,              // End Collection

    // ---- Feature Report (Report ID 5) ----
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x05,        //   Report ID (5)
    0x05, 0x06,        //   Usage Page (Generic Device Controls)
    0x09, 0x20,        //   Usage (Battery Strength)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0xB1, 0x02,        //   Feature (Data, Variable, Absolute)
    0xC0,              // End Collection
};

// ============================================================================
// BLE REPORT STRUCTURES
// ============================================================================

// Standard gamepad report (15 bytes, Report ID 3)
typedef struct __attribute__((packed)) {
    uint8_t buttons_lo;     // Buttons 1-8
    uint8_t buttons_hi;     // Buttons 9-16
    uint8_t hat;            // Hat switch (1-8 direction, 0=center)
    int16_t lx;             // Left stick X  (0-32767)
    int16_t ly;             // Left stick Y  (0-32767)
    int16_t rx;             // Right stick X (0-32767)
    int16_t ry;             // Right stick Y (0-32767)
    int16_t lt;             // Left trigger  (0-32767)
    int16_t rt;             // Right trigger (0-32767)
} ble_gamepad_report_t;

// Hat switch values
#define BLE_HAT_CENTER      0
#define BLE_HAT_UP          1
#define BLE_HAT_UP_RIGHT    2
#define BLE_HAT_RIGHT       3
#define BLE_HAT_DOWN_RIGHT  4
#define BLE_HAT_DOWN        5
#define BLE_HAT_DOWN_LEFT   6
#define BLE_HAT_LEFT        7
#define BLE_HAT_UP_LEFT     8

// ============================================================================
// PENDING REPORT — type-tagged for flow-controlled sending
// ============================================================================

typedef enum {
    PENDING_NONE = 0,
    PENDING_GAMEPAD,
    PENDING_KEYBOARD,
    PENDING_MOUSE,
    PENDING_XBOX,
    PENDING_SINPUT,          // SInput input report (ID 1)
    PENDING_SINPUT_FEATURE,  // SInput feature response (ID 2)
} pending_report_type_t;

// SInput BLE composite mouse report (ID 8): 5 buttons, 16-bit X/Y, wheel,
// AC Pan — matches sinput_kbd_mouse_tail. The standard-mode ble_mouse_report_t
// (ID 2) only carries 8-bit deltas, so this has its own layout.
typedef struct __attribute__((packed)) {
    uint8_t buttons;
    int16_t x;
    int16_t y;
    int8_t  wheel;
    int8_t  pan;
} sinput_ble_mouse_report_t;

// ============================================================================
// STATE
// ============================================================================

static hci_con_handle_t con_handle = HCI_CON_HANDLE_INVALID;
static bool ble_connected = false;

// GPIO (raw chip pin) to wake from deep sleep on; <0 disables sleep-on-disconnect.
static int sleep_wake_pin = -1;
static bool sleep_wake_active_high = false;

// --- USB dominance: a USB *data host* takes priority over BT ---
// When one is connected we disconnect BT and stop advertising; when it goes
// away we resume the normal BT logic. A USB host requires VBUS, so gate on both
// (tud_mounted() can read stale-true briefly after an unplug).
extern bool tud_mounted(void);
static bool ble_usb_host(void)
{
    // Runtime wireless policy (flash, live-settable over CDC): under
    // WIRELESS_POLICY_USB with a USB data host connected, input is routed
    // only to USB — the BLE link stays connected and advertising, it just
    // stops carrying input (mirror image of WIRELESS_POLICY_BLE, which mutes
    // USB input while a BLE host is subscribed). BOTH (the default) routes
    // to both. Nothing ever disconnects the BT link over this.
    const flash_t *settings = flash_get_settings();
    if (!settings || settings->wireless_policy != WIRELESS_POLICY_USB) {
        return false;
    }
    // CDC-only USB is a config/debug link, not a controller role — BLE keeps
    // carrying input. HID modes = the USB host owns the input stream.
    if (usbd_get_mode() == USB_OUTPUT_MODE_CDC) return false;
    return platform_usb_powered() && tud_mounted();
}

// Mute filter for the BLE send paths under WIRELESS_POLICY_USB. Router events
// are consume-once, so the tasks still consume (and CDC-stream) every event —
// this decides whether it reaches BLE. On the mute edge a neutral report goes
// out once so the BLE host doesn't hold whatever was pressed last.
static const input_event_t ble_neutral_event = {
    .type = INPUT_TYPE_GAMEPAD,
    .analog = { 128, 128, 128, 128, 0, 0, 0, 0 },
};
static bool ble_mute_active = false;
static const input_event_t *ble_mute_filter(const input_event_t *event)
{
    if (!ble_usb_host()) {
        ble_mute_active = false;
        return event;
    }
    if (!ble_mute_active) {
        ble_mute_active = true;
        return &ble_neutral_event;   // release everything on the BLE side
    }
    return NULL;                     // muted: consumed, not forwarded
}

// Strong override of usbd.c's weak default: under WIRELESS_POLICY_BLE, USB
// input reports are suppressed while a BLE host is subscribed (USB stays
// enumerated as a fallback and CDC keeps working). Mirrors USB dominance:
// the dominant side only wins while its host is actually present.
bool ble_output_suppresses_usb(void)
{
    const flash_t *settings = flash_get_settings();
    return settings && settings->wireless_policy == WIRELESS_POLICY_BLE &&
           ble_output_is_connected();
}

// Tracked advertising state so we can enable/disable idempotently.
static bool adv_on = false;
static void set_adv(bool on)
{
    if (on == adv_on) return;
    adv_on = on;
    gap_advertisements_enable(on ? 1 : 0);
}

bool ble_output_is_connected(void)
{
    return ble_connected;
}

void ble_output_set_sleep_wake_pin(int gpio, bool active_high)
{
    sleep_wake_pin = gpio;
    sleep_wake_active_high = active_high;
}

// Pending reports (flow-controlled — only one at a time)
static pending_report_type_t pending_type = PENDING_NONE;
static ble_gamepad_report_t pending_gamepad;
static ble_keyboard_report_t pending_keyboard;
static ble_mouse_report_t pending_mouse;
static ble_xbox_report_t pending_xbox;
static sinput_report_t pending_sinput;
static sinput_ble_mouse_report_t pending_sinput_mouse;
static uint8_t pending_feature[63];       // SInput feature response payload
static uint16_t pending_feature_len;
// Input state arrived while a feature response was queued: pending_sinput holds
// it (the router hands out each event exactly once), send it after the feature.
static volatile bool sinput_input_after_feature = false;

// Last sent reports (for change detection)
static ble_gamepad_report_t last_sent_gamepad;
static ble_keyboard_report_t last_sent_keyboard;
static ble_mouse_report_t last_sent_mouse;
static ble_xbox_report_t last_sent_xbox;
static sinput_report_t last_sent_sinput;

// Report storage for hids_device_init_with_storage()
// 12 slots to cover the SInput composite (gamepad + keyboard + mouse) when
// enabled; 8 suffices for the non-composite modes.
static hids_device_report_t hid_report_storage[12];
#define HID_REPORT_STORAGE_COUNT (sizeof(hid_report_storage) / sizeof(hid_report_storage[0]))

// Toggle: BLE SInput composite (gamepad + keyboard + mouse). Define = composite
// (Gamepad API + kbd/mouse over BLE); undefine = pure SInput gamepad. Both work
// with the macOS Gamepad API — verified on hardware.
#define SINPUT_BLE_COMPOSITE 1

// Composite SInput keyboard (ID 6) + mouse (ID 8) report map — served as HID
// service instance #2's report map (see hid2_read_callback), NOT concatenated
// onto the gamepad map: kbd/mouse collections inside the gamepad's own map are
// never dispatched by macOS (single event service, primary usage Gamepad).
// SDL keys on the gamepad service + VID/PID (2E8A:10C6), unchanged, so Steam
// is preserved. NUS config is reached via joypad-ble.
//
// TWO HARD-WON GOTCHAS:
//  1) NEVER add a Consumer Control collection here. Over BLE, macOS reclassifies a
//     device exposing Consumer Control as a media remote and drops it from the
//     Gamepad API entirely. Keyboard + mouse are fine; consumer is not.
//  2) Chrome caches the gamepad/HID descriptor PER DEVICE. After any report-map
//     change, a page refresh will NOT pick it up — you must Forget the device in
//     the OS and fully restart the browser. Skipping this makes a working build
//     look broken ("detected then no input"); it cost hours of misdiagnosis.
//
// Built at init by concatenating onto sinput_report_descriptor.
#ifdef SINPUT_BLE_COMPOSITE
static const uint8_t sinput_kbd_mouse_tail[] = {
    // --- Keyboard, Report ID 6 (in: modifier+keys, out: lock LEDs) ---
    0x05,0x01, 0x09,0x06, 0xA1,0x01, 0x85,0x06,
    0x05,0x07, 0x19,0xE0, 0x29,0xE7, 0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x08, 0x81,0x02,
    0x95,0x01, 0x75,0x08, 0x81,0x01,
    0x95,0x05, 0x75,0x01, 0x05,0x08, 0x19,0x01, 0x29,0x05, 0x91,0x02, 0x95,0x01, 0x75,0x03, 0x91,0x01,
    0x95,0x06, 0x75,0x08, 0x15,0x00, 0x25,0x65, 0x05,0x07, 0x19,0x00, 0x29,0x65, 0x81,0x00,
    0xC0,
    // --- Mouse, Report ID 8 (5 buttons, 16-bit X/Y, wheel, pan) ---
    0x05,0x01, 0x09,0x02, 0xA1,0x01, 0x85,0x08, 0x09,0x01, 0xA1,0x00,
    0x05,0x09, 0x19,0x01, 0x29,0x05, 0x15,0x00, 0x25,0x01, 0x95,0x05, 0x75,0x01, 0x81,0x02,
    0x95,0x01, 0x75,0x03, 0x81,0x01,
    0x05,0x01, 0x09,0x30, 0x09,0x31, 0x16,0x00,0x80, 0x26,0xFF,0x7F, 0x75,0x10, 0x95,0x02, 0x81,0x06,
    0x09,0x38, 0x15,0x81, 0x25,0x7F, 0x75,0x08, 0x95,0x01, 0x81,0x06,
    0x05,0x0C, 0x0A,0x38,0x02, 0x15,0x81, 0x25,0x7F, 0x75,0x08, 0x95,0x01, 0x81,0x06,
    0xC0, 0xC0,
};
#endif  // SINPUT_BLE_COMPOSITE

// Mode (loaded from flash on init)
static ble_output_mode_t current_mode = BLE_MODE_STANDARD;

static btstack_packet_callback_registration_t hci_event_callback_registration;
static btstack_packet_callback_registration_t sm_event_callback_registration;

// Diagnostic stage marker — strong implementation on nRF writes a noinit RAM
// ring that survives resets (nrf/src/main.c); weak no-op everywhere else.
__attribute__((weak)) void bt_diag_mark(uint32_t code) { (void)code; }

// Raw LE link handle from connection-complete (see LE_META case): valid even
// before the HID-subscription handshake that sets con_handle.
static uint16_t last_le_handle = 0xFFFF;  // HCI_CON_HANDLE_INVALID

// Pairing-safety state for the USB-dominance enforcement: yanking the link
// with gap_disconnect while SM pairing / encryption setup is in flight
// crashed the controller hard (silent reset, RESETREAS=0) on nRF52840.
// Dominance must wait for pairing to finish and give a fresh link a grace
// window before disconnecting it.
// (Dominance no longer disconnects at all — policy only mutes the input
// stream — but the pairing/link state stays tracked for diagnostics and any
// future path that must not touch a young or pairing link.)
static volatile bool sm_pairing_active = false;
static uint32_t link_up_ms = 0;

#ifdef SINPUT_BLE_COMPOSITE
// ---------------------------------------------------------------------------
// HID service instance #2 — keyboard + mouse. One HOGP HID service = one HID
// device on the host. With kbd/mouse collections inside the gamepad's report
// map, macOS builds a single event service with primary usage Gamepad and
// never dispatches the keyboard/pointer collections: the notifications arrive
// (visible to hidapi) but produce no cursor motion or keystrokes — keystroke-
// injection protection against "a gamepad that can type". Splitting kbd/mouse
// into their own HID service instance mirrors the USB build's separate
// interfaces: macOS enumerates an independent keyboard+mouse device and
// dispatches normally, while the gamepad service stays claimed as a
// controller. BTstack's hids_device binds only the first HID service in the
// DB, so this second instance is served here directly via att_server.
// ---------------------------------------------------------------------------
static att_service_handler_t hid2_service_handler;
static uint8_t  hid2_protocol_mode = 1;   // report protocol
static uint16_t hid2_kbd_ccc = 0;
static uint16_t hid2_mouse_ccc = 0;

// Service #2's attribute handles. Only ble_gamepad.gatt carries it: in Xbox
// mode the device-wide Xbox PnP ID makes macOS/iOS attach a dummy (no-event)
// service to any non-gamepad HID device, so keyboard/mouse can't ride along.
typedef struct {
    uint16_t service_start;
    uint16_t service_end;
    uint16_t protocol_mode;
    uint16_t report_map;
    uint16_t kbd_value;
    uint16_t kbd_ccc;
    uint16_t kbd_leds;
    uint16_t mouse_value;
    uint16_t mouse_ccc;
} ble_hid2_handles_t;

static const ble_hid2_handles_t gamepad_hid2_handles = {
    .service_start = ATT_SERVICE_ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE_02_START_HANDLE,
    .service_end   = ATT_SERVICE_ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE_02_END_HANDLE,
    .protocol_mode = ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_PROTOCOL_MODE_02_VALUE_HANDLE,
    .report_map    = ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_REPORT_MAP_02_VALUE_HANDLE,
    .kbd_value     = ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_REPORT_08_VALUE_HANDLE,
    .kbd_ccc       = ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_REPORT_08_CLIENT_CONFIGURATION_HANDLE,
    .kbd_leds      = ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_REPORT_09_VALUE_HANDLE,
    .mouse_value   = ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_REPORT_0a_VALUE_HANDLE,
    .mouse_ccc     = ATT_CHARACTERISTIC_ORG_BLUETOOTH_CHARACTERISTIC_REPORT_0a_CLIENT_CONFIGURATION_HANDLE,
};
static const ble_hid2_handles_t *hid2 = NULL;   // NULL = active DB has no service #2

static uint16_t hid2_read_callback(hci_con_handle_t con, uint16_t attribute_handle,
                                   uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    (void)con;
    if (!hid2) return 0;
    if (attribute_handle == hid2->report_map)
        return att_read_callback_handle_blob(sinput_kbd_mouse_tail,
            sizeof(sinput_kbd_mouse_tail), offset, buffer, buffer_size);
    if (attribute_handle == hid2->protocol_mode)
        return att_read_callback_handle_byte(hid2_protocol_mode, offset, buffer, buffer_size);
    if (attribute_handle == hid2->kbd_value)
        return att_read_callback_handle_blob((const uint8_t *)&last_sent_keyboard,
            sizeof(last_sent_keyboard), offset, buffer, buffer_size);
    if (attribute_handle == hid2->kbd_ccc)
        return att_read_callback_handle_little_endian_16(hid2_kbd_ccc, offset, buffer, buffer_size);
    if (attribute_handle == hid2->kbd_leds)
        return att_read_callback_handle_byte(0, offset, buffer, buffer_size);
    if (attribute_handle == hid2->mouse_value) {
        static const uint8_t zeros[sizeof(sinput_ble_mouse_report_t)] = {0};
        return att_read_callback_handle_blob(zeros, sizeof(zeros), offset, buffer, buffer_size);
    }
    if (attribute_handle == hid2->mouse_ccc)
        return att_read_callback_handle_little_endian_16(hid2_mouse_ccc, offset, buffer, buffer_size);
    return 0;
}

static int hid2_write_callback(hci_con_handle_t con, uint16_t attribute_handle,
                               uint16_t transaction_mode, uint16_t offset,
                               uint8_t *buffer, uint16_t buffer_size)
{
    (void)offset;
    if (!hid2 || transaction_mode != ATT_TRANSACTION_MODE_NONE) return 0;
    if (attribute_handle == hid2->kbd_ccc && buffer_size >= 2) {
        hid2_kbd_ccc = little_endian_read_16(buffer, 0);
        // A kbd/mouse-only subscriber (no service-1 gamepad CCC write)
        // must still mark the link connected.
        if (hid2_kbd_ccc) { con_handle = con; ble_connected = true; adv_on = false; }
    } else if (attribute_handle == hid2->mouse_ccc && buffer_size >= 2) {
        hid2_mouse_ccc = little_endian_read_16(buffer, 0);
        if (hid2_mouse_ccc) { con_handle = con; ble_connected = true; adv_on = false; }
    } else if (attribute_handle == hid2->protocol_mode && buffer_size >= 1) {
        hid2_protocol_mode = buffer[0];
    }
    return 0;
}

// Keyboard/mouse have their own queue, independent of the gamepad's
// pending_type slot: they notify through att_server on service #2, not
// through hids_device, so a gamepad report arriving while a keystroke is
// queued must not overwrite it (a lost key release is a stuck key).
static volatile bool hid2_kbd_pending = false;
static volatile bool hid2_mouse_pending = false;

// att_server grant for service #2. The registration may only be queued once
// at a time — re-adding a queued registration corrupts att_server's list.
static btstack_context_callback_registration_t hid2_send_request;
static bool hid2_notify_requested = false;   // BTstack thread only

static void hid2_request_send(uint16_t handle);

static void hid2_can_send_now(void *context)
{
    uint16_t h = (uint16_t)(uintptr_t)context;
    hid2_notify_requested = false;
    if (h == HCI_CON_HANDLE_INVALID || !hid2) return;
    if (hid2_kbd_pending) {
        hid2_kbd_pending = false;
        att_server_notify(h, hid2->kbd_value,
            (const uint8_t *)&pending_keyboard, sizeof(pending_keyboard));
        last_sent_keyboard = pending_keyboard;
    } else if (hid2_mouse_pending) {
        hid2_mouse_pending = false;
        att_server_notify(h, hid2->mouse_value,
            (const uint8_t *)&pending_sinput_mouse, sizeof(pending_sinput_mouse));
    }
    if (hid2_kbd_pending || hid2_mouse_pending) hid2_request_send(h);
}

static void hid2_request_send(uint16_t handle)
{
    if (hid2_notify_requested) return;
    hid2_notify_requested = true;
    hid2_send_request.callback = &hid2_can_send_now;
    hid2_send_request.context = (void *)(uintptr_t)handle;
    att_server_request_to_send_notification(&hid2_send_request, handle);
}
#else
static void hid2_request_send(uint16_t handle) { (void)handle; }
#endif  // SINPUT_BLE_COMPOSITE

// ---------------------------------------------------------------------------
// Cross-thread send marshalling. On multi-threaded ports (nRF/ESP32) BTstack
// runs in its own thread, and the ble_output_task_* senders run in the app
// main loop. hids_device_request_can_send_now_event() can send SYNCHRONOUSLY
// in the caller's context when ATT is free, racing the BTstack thread for the
// single HCI TX buffer — hci_reserve_packet_buffer() double-reserve assert,
// device dead (this killed every BLE HID session on the Makerdiary dongle the
// moment a host subscribed). Marshal the request onto the BTstack run-loop
// thread; coalesce because pending_* already holds the latest report.
// ---------------------------------------------------------------------------
#if defined(BTSTACK_USE_NRF) || defined(BTSTACK_USE_ESP32)
static btstack_context_callback_registration_t send_req_marshal;
static volatile bool send_req_queued = false;
static volatile uint16_t send_req_handle;

static void request_send_on_btstack_thread(void *context)
{
    (void)context;
    // Clear BEFORE requesting so a task-side set that lands mid-callback
    // queues a fresh marshal instead of being swallowed.
    send_req_queued = false;
    uint16_t h = send_req_handle;
    if (h != HCI_CON_HANDLE_INVALID) {
        hids_device_request_can_send_now_event(h);
    }
}

static void ble_request_can_send_now(uint16_t handle)
{
    send_req_handle = handle;
    if (send_req_queued) return;  // marshal already in flight; it sends latest
    send_req_queued = true;
    send_req_marshal.callback = &request_send_on_btstack_thread;
    send_req_marshal.context = NULL;
    btstack_run_loop_execute_on_main_thread(&send_req_marshal);
}

static btstack_context_callback_registration_t hid2_req_marshal;
static volatile bool hid2_req_queued = false;
static volatile uint16_t hid2_req_handle;

static void hid2_request_on_btstack_thread(void *context)
{
    (void)context;
    hid2_req_queued = false;
    uint16_t h = hid2_req_handle;
    if (h != HCI_CON_HANDLE_INVALID) hid2_request_send(h);
}

static void hid2_request_can_send_now(uint16_t handle)
{
    hid2_req_handle = handle;
    if (hid2_req_queued) return;
    hid2_req_queued = true;
    hid2_req_marshal.callback = &hid2_request_on_btstack_thread;
    hid2_req_marshal.context = NULL;
    btstack_run_loop_execute_on_main_thread(&hid2_req_marshal);
}
#else
// Single-threaded ports (Pico W): direct call, same context as BTstack.
static void ble_request_can_send_now(uint16_t handle)
{
    hids_device_request_can_send_now_event(handle);
}

static void hid2_request_can_send_now(uint16_t handle)
{
    hid2_request_send(handle);
}
#endif

// Bench tool (BLE.PAIR): send an SM Security Request on the live link so the
// central initiates pairing. Must run in the BTstack context (NUS command
// path does).
void ble_output_request_pairing(void)
{
    uint16_t h = (con_handle != HCI_CON_HANDLE_INVALID) ? con_handle
                                                        : last_le_handle;
    if (h != HCI_CON_HANDLE_INVALID) {
        printf("[ble_output] requesting SM pairing (handle=0x%04x)\n", h);
        bt_diag_mark(0xC1000099u);
        sm_request_pairing(h);
    } else {
        printf("[ble_output] BLE.PAIR: no connection\n");
    }
}

// ============================================================================
// ADVERTISING DATA
// ============================================================================

// NOTE: legacy BLE advertising payload is capped at 31 bytes. The full name
// "JoypadOS Controller" (19 chars) + flags + UUID + appearance would be 32
// bytes, which the controller silently REJECTS — the device then never
// advertises. So the primary adv packet carries only flags + UUID + appearance
// (11 bytes) and the complete name goes in the scan response below.
static const uint8_t adv_data_standard[] = {
    // Flags: general discoverable, BR/EDR not supported
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    // 16-bit Service UUIDs: HID Service
    0x03, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS,
    ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE & 0xFF,
    ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE >> 8,
    // Appearance: Gamepad (0x03C4)
    0x03, BLUETOOTH_DATA_TYPE_APPEARANCE, 0xC4, 0x03,
    // 128-bit Service UUIDs: Nordic UART (NUS). Advertising it lets host apps
    // retrieve the connected peripheral by this UUID even while the OS owns
    // the HID service (CoreBluetooth's retrieve filter matches ADVERTISED
    // services) — the MouthPad-style companion-app pattern. Also enables
    // WebBluetooth discovery. 11 + 18 = 29 bytes, fits the 31-byte cap.
    0x11, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_128_BIT_SERVICE_CLASS_UUIDS,
    0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
    0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E,
};

// Scan response carries the complete local name (21 bytes, fits in 31).
static const uint8_t scan_resp_standard[] = {
    0x14, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,
    'J', 'o', 'y', 'p', 'a', 'd', 'O', 'S', ' ',
    'C', 'o', 'n', 't', 'r', 'o', 'l', 'l', 'e', 'r',
};

static const uint8_t adv_data_xbox[] = {
    // Flags: general discoverable, BR/EDR not supported
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    // Complete local name: "Joypad Xinput"
    0x0E, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,
    'J', 'o', 'y', 'p', 'a', 'd', ' ',
    'X', 'i', 'n', 'p', 'u', 't',
    // 16-bit Service UUIDs: HID Service
    0x03, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS,
    ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE & 0xFF,
    ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE >> 8,
    // Appearance: Gamepad (0x03C4)
    0x03, BLUETOOTH_DATA_TYPE_APPEARANCE, 0xC4, 0x03,
};

// ============================================================================
// STANDARD MODE CONVERSION HELPERS
// ============================================================================

static uint16_t convert_buttons(uint32_t buttons)
{
    uint16_t ble_buttons = 0;

    if (buttons & JP_BUTTON_B1) ble_buttons |= (1 << 0);
    if (buttons & JP_BUTTON_B2) ble_buttons |= (1 << 1);
    if (buttons & JP_BUTTON_B3) ble_buttons |= (1 << 2);
    if (buttons & JP_BUTTON_B4) ble_buttons |= (1 << 3);
    if (buttons & JP_BUTTON_L1) ble_buttons |= (1 << 4);
    if (buttons & JP_BUTTON_R1) ble_buttons |= (1 << 5);
    if (buttons & JP_BUTTON_L2) ble_buttons |= (1 << 6);
    if (buttons & JP_BUTTON_R2) ble_buttons |= (1 << 7);
    if (buttons & JP_BUTTON_S1) ble_buttons |= (1 << 8);
    if (buttons & JP_BUTTON_S2) ble_buttons |= (1 << 9);
    if (buttons & JP_BUTTON_L3) ble_buttons |= (1 << 10);
    if (buttons & JP_BUTTON_R3) ble_buttons |= (1 << 11);
    if (buttons & JP_BUTTON_A1) ble_buttons |= (1 << 12);
    if (buttons & JP_BUTTON_A2) ble_buttons |= (1 << 13);

    return ble_buttons;
}

static uint8_t convert_dpad_to_hat(uint32_t buttons)
{
    uint8_t up    = (buttons & JP_BUTTON_DU) ? 1 : 0;
    uint8_t down  = (buttons & JP_BUTTON_DD) ? 1 : 0;
    uint8_t left  = (buttons & JP_BUTTON_DL) ? 1 : 0;
    uint8_t right = (buttons & JP_BUTTON_DR) ? 1 : 0;

    if (up && right) return BLE_HAT_UP_RIGHT;
    if (up && left)  return BLE_HAT_UP_LEFT;
    if (down && right) return BLE_HAT_DOWN_RIGHT;
    if (down && left)  return BLE_HAT_DOWN_LEFT;
    if (up)    return BLE_HAT_UP;
    if (down)  return BLE_HAT_DOWN;
    if (left)  return BLE_HAT_LEFT;
    if (right) return BLE_HAT_RIGHT;

    return BLE_HAT_CENTER;
}

// ============================================================================
// PACKET HANDLER
// ============================================================================

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size)
{
    (void)channel;
    (void)size;

    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
        case HCI_EVENT_DISCONNECTION_COMPLETE: {
            uint8_t reason = hci_event_disconnection_complete_get_reason(packet);
            bt_diag_mark(0xC0D00000u | reason);
            con_handle = HCI_CON_HANDLE_INVALID;
            last_le_handle = HCI_CON_HANDLE_INVALID;
            sm_pairing_active = false;
            ble_connected = false;
            pending_type = PENDING_NONE;
            sinput_input_after_feature = false;
#ifdef SINPUT_BLE_COMPOSITE
            // att_server drops queued notification requests with the link.
            hid2_kbd_pending = false;
            hid2_mouse_pending = false;
            hid2_notify_requested = false;
#endif

            // Distinguish a deliberate host disconnect from a dropped link:
            //   0x13 = remote user terminated   (host "disconnected")
            //   0x16 = connection terminated by local host
            //   0x08 = supervision timeout      (out of range / host slept)
            // On a deliberate disconnect, power down instead of re-advertising
            // — otherwise a bonded host (e.g. macOS) just auto-reconnects and
            // hogs the link. A dropped link keeps advertising so we reconnect.
            // (Wireless policy never turns BT off — USB-dominant only mutes
            // the input stream, see ble_mute_filter.)
            bool deliberate = (reason == 0x13 || reason == 0x16);
            if (deliberate && sleep_wake_pin >= 0) {
                // platform_deep_sleep() powers down (and never returns) on
                // battery; it no-ops and returns false on USB.
                if (platform_deep_sleep((uint8_t)sleep_wake_pin, sleep_wake_active_high)) {
                    return;  // unreachable on success
                }
            }
            printf("[ble_output] Disconnected (reason 0x%02x), restarting advertising\n", reason);
            adv_on = false;  // the drop stopped advertising; set_adv re-enables
            set_adv(true);
            break;
        }

        case HCI_EVENT_LE_META:
            if (hci_event_le_meta_get_subevent_code(packet) ==
                HCI_SUBEVENT_LE_CONNECTION_COMPLETE) {
                bt_diag_mark(0xC0C00001u);
                // Track the raw link handle: con_handle proper is only set
                // once the host subscribes to HID reports, but BLE.PAIR needs
                // a handle for a bare GATT client (e.g. CoreBluetooth, which
                // never touches the hidden HID service).
                last_le_handle =
                    hci_subevent_le_connection_complete_get_connection_handle(packet);
                link_up_ms = btstack_run_loop_get_time_ms();
                // No SM Security Request here: the report characteristics
                // require encryption, so the host pairs on demand. A
                // peripheral-initiated request races the host's own pairing
                // agent — macOS System Settings completed pairing, dropped
                // the link (0x13) before GATT, and discarded the keys.
            }
            break;

        case SM_EVENT_PAIRING_STARTED:
            bt_diag_mark(0xC1000010u);
            sm_pairing_active = true;
            break;

        case SM_EVENT_PAIRING_COMPLETE:
            bt_diag_mark(0xC1001100u |
                         sm_event_pairing_complete_get_status(packet));
            sm_pairing_active = false;
            break;

        case SM_EVENT_JUST_WORKS_REQUEST:
            bt_diag_mark(0xC1000001u);
            sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
            break;

        case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
            bt_diag_mark(0xC1000002u);
            sm_numeric_comparison_confirm(sm_event_passkey_display_number_get_handle(packet));
            break;

        case HCI_EVENT_HIDS_META:
            switch (hci_event_hids_meta_get_subevent_code(packet)) {
                case HIDS_SUBEVENT_INPUT_REPORT_ENABLE:
                    bt_diag_mark(0xC2000001u);
                    con_handle = hids_subevent_input_report_enable_get_con_handle(packet);
                    ble_connected = true;
                    printf("[ble_output] BLE connected (handle=0x%04x)\n", con_handle);
                    // A connection stops advertising (single adv set on nRF).
                    adv_on = false;
                    break;

                case HIDS_SUBEVENT_CAN_SEND_NOW:
                    bt_diag_mark(0xB2000000u | (uint32_t)pending_type);
                    if (pending_type != PENDING_NONE && con_handle != HCI_CON_HANDLE_INVALID) {
                        switch (pending_type) {
                            case PENDING_GAMEPAD:
                                hids_device_send_input_report_for_id(con_handle, 3,
                                    (const uint8_t *)&pending_gamepad, sizeof(pending_gamepad));
                                last_sent_gamepad = pending_gamepad;
                                break;
                            case PENDING_KEYBOARD:
                                // Standard composite keyboard (report ID 1).
                                // SInput-mode keyboard (ID 6) lives on HID
                                // service #2 and never reaches this handler.
                                hids_device_send_input_report_for_id(con_handle, 1,
                                    (const uint8_t *)&pending_keyboard, sizeof(pending_keyboard));
                                last_sent_keyboard = pending_keyboard;
                                break;
                            case PENDING_MOUSE:
                                hids_device_send_input_report_for_id(con_handle, 2,
                                    (const uint8_t *)&pending_mouse, sizeof(pending_mouse));
                                last_sent_mouse = pending_mouse;
                                break;
                            case PENDING_XBOX:
                                // Real Series X pads use INPUT report ID 1
                                // (rumble OUTPUT is ID 3)
                                hids_device_send_input_report_for_id(con_handle, 1,
                                    (const uint8_t *)&pending_xbox, sizeof(pending_xbox));
                                last_sent_xbox = pending_xbox;
                                break;
                            case PENDING_SINPUT:
                                // Input report ID 1: skip the report_id byte (hids
                                // adds it), send the 63-byte payload.
                                hids_device_send_input_report_for_id(con_handle, SINPUT_REPORT_ID_INPUT,
                                    ((const uint8_t *)&pending_sinput) + 1,
                                    sizeof(pending_sinput) - 1);
                                last_sent_sinput = pending_sinput;
                                break;
                            case PENDING_SINPUT_FEATURE:
                                // Input report ID 2: the SInput feature response.
                                hids_device_send_input_report_for_id(con_handle, SINPUT_REPORT_ID_FEATURES,
                                    pending_feature, pending_feature_len);
                                break;
                            default:
                                break;
                        }
                        bool sent_feature = (pending_type == PENDING_SINPUT_FEATURE);
                        pending_type = PENDING_NONE;
                        // Input state consumed while the feature was in flight
                        // (router hands out each event exactly once — it can't
                        // be re-read): send it now or it's lost.
                        if (sent_feature && sinput_input_after_feature) {
                            sinput_input_after_feature = false;
                            pending_type = PENDING_SINPUT;
                            // Already on the BTstack thread: request directly.
                            hids_device_request_can_send_now_event(con_handle);
                        }
                    }
                    break;

                case HIDS_SUBEVENT_SET_REPORT: {
                    uint8_t report_id = hids_subevent_set_report_get_report_id(packet);
                    uint16_t report_len = hids_subevent_set_report_get_report_length(packet);
                    const uint8_t *report_data = hids_subevent_set_report_get_report_data(packet);

                    printf("[ble_output] SET_REPORT: id=%d len=%d mode=%d\n",
                           report_id, report_len, current_mode);
                    if (current_mode == BLE_MODE_XBOX && report_id == 3) {
                        // Xbox rumble output report
                        uint8_t rumble_left, rumble_right;
                        if (ble_xbox_parse_rumble(report_data, report_len,
                                                  &rumble_left, &rumble_right)) {
                            printf("[ble_output] Rumble: left=%d right=%d\n",
                                   rumble_left, rumble_right);
                            // Forward rumble to connected input controller
                            feedback_set_rumble(0, rumble_left, rumble_right);
                        }
                    }
                    if (current_mode == BLE_MODE_SINPUT && report_id == SINPUT_REPORT_ID_OUTPUT) {
                        // SInput output report (haptic / player LED / RGB / features
                        // request). report_data is the payload (command byte first).
                        sinput_output_received(report_data, report_len);
                        // Forward any updated rumble to the connected input controller.
                        uint8_t rl = 0, rr = 0;
                        sinput_get_rumble_lr(&rl, &rr);
                        feedback_set_rumble(0, rl, rr);
                    }
                    // Standard mode: report_id 1 = keyboard LEDs, report_id 4 = player indicator
                    break;
                }

                default:
                    break;
            }
            break;

        default:
            break;
    }
}

// ============================================================================
// OUTPUT INTERFACE IMPLEMENTATION
// ============================================================================

void ble_output_init(void)
{
    // Load mode from flash
    flash_init();
    flash_t *settings = flash_get_settings();
    if (settings && settings->ble_output_mode < BLE_MODE_COUNT) {
        current_mode = (ble_output_mode_t)settings->ble_output_mode;
    }
    // A persisted mode this build doesn't offer (e.g. Standard BLE hidden by
    // default, Switch-BT on a BLE-only radio) falls back to SInput.
    if (!ble_output_mode_available(current_mode)) {
        current_mode = BLE_MODE_SINPUT;
    }

#ifdef CONFIG_UNIVERSAL
    // universal is a gamepad and (on builds like the tucked-away XIAO)
    // has no practical USB/CDC access to switch modes — SInput is the DEFAULT
    // BLE device mode here, carrying buttons + gyro/accel + battery to
    // SDL/Steam. But honor an EXPLICIT selection (BLE.MODE.SET / web config,
    // marked by ble_mode_saved) and an explicitly-selected Switch-BT mode —
    // otherwise the mode selector is a silent no-op on this app.
    if (!(settings && settings->ble_mode_saved)) {
#ifdef CONFIG_BT_CLASSIC_OUTPUT
        if (current_mode != BLE_MODE_SWITCH_BT)
#endif
            current_mode = BLE_MODE_SINPUT;
    }
#endif

    printf("[ble_output] Initializing BLE output (mode: %s)\n",
           ble_output_get_mode_name(current_mode));

    // Initialize reports to neutral state
    memset(&pending_gamepad, 0, sizeof(pending_gamepad));
    pending_gamepad.hat = BLE_HAT_CENTER;
    pending_gamepad.lx = 16384;
    pending_gamepad.ly = 16384;
    pending_gamepad.rx = 16384;
    pending_gamepad.ry = 16384;
    last_sent_gamepad = pending_gamepad;

    memset(&last_sent_keyboard, 0, sizeof(last_sent_keyboard));
    memset(&last_sent_mouse, 0, sizeof(last_sent_mouse));

    memset(&pending_xbox, 0, sizeof(pending_xbox));
    pending_xbox.lx = 32768;
    pending_xbox.ly = 32768;
    pending_xbox.rx = 32768;
    pending_xbox.ry = 32768;
    last_sent_xbox = pending_xbox;

#ifdef CONFIG_BT_CLASSIC_OUTPUT
    if (current_mode == BLE_MODE_SWITCH_BT) switch_bt_init();
#endif
}

// ATT write callback — debug logging for all GATT writes
static int att_write_callback(hci_con_handle_t con_handle, uint16_t att_handle,
                               uint16_t transaction_mode, uint16_t offset,
                               uint8_t *buffer, uint16_t buffer_size)
{
    printf("[ble_output] ATT_WRITE: handle=0x%04x size=%d", att_handle, buffer_size);
    if (buffer_size > 0 && buffer_size <= 8) {
        printf(" data=");
        for (int i = 0; i < buffer_size; i++) printf("%02x", buffer[i]);
    }
    printf("\n");
    return 0;  // Let hids_device handle it
}

// Tell btstack_host (central path) that we own the ATT server with the full
// peripheral GATT profile, so it skips installing its minimal fallback server.
bool btstack_host_external_att_server(void) { return true; }

// --- BLE Battery Service: mirror the router's onboard battery percentage ---

static btstack_timer_source_t battery_timer;

static void battery_timer_handler(btstack_timer_source_t *ts)
{
    // The app samples the ADC (one reader, main thread) into the router; we
    // just mirror it here. <0 means no battery sense on this board → leave the
    // BAS at its init value. Runs in the BTstack thread, so set_battery_value
    // is on the correct thread.
    int pct = router_onboard_battery_percent();
    if (pct >= 0) {
        battery_service_server_set_battery_value((uint8_t)pct);
    }
    btstack_run_loop_set_timer(ts, 60000);  // battery changes slowly
    btstack_run_loop_add_timer(ts);
}

// --- USB dominance enforcement (BTstack thread) ---
static btstack_timer_source_t usb_dom_timer;

static void usb_dom_timer_handler(btstack_timer_source_t *ts)
{
    // Wireless policy never drops the BT link (USB-dominant only mutes the
    // input stream — see ble_mute_filter); this timer is just a safety net
    // that keeps advertising alive whenever nothing is connected.
    if (con_handle == HCI_CON_HANDLE_INVALID) {
        set_adv(true);
    }
    btstack_run_loop_set_timer(ts, 500);
    btstack_run_loop_add_timer(ts);
}

// Called after bt_init() — BTstack must be running before GATT/GAP setup
void ble_output_late_init(void)
{
#ifdef CONFIG_BT_CLASSIC_OUTPUT
    if (current_mode == BLE_MODE_SWITCH_BT) { switch_bt_late_init(); return; }
#endif
    printf("[ble_output] Setting up BLE GATT services (mode: %s)\n",
           ble_output_get_mode_name(current_mode));

    // Initialize L2CAP and Security Manager (required before ATT/GATT setup)
    l2cap_init();
    sm_init();

    // Setup ATT server with mode-appropriate GATT profile
    const uint8_t *gatt_db = (current_mode == BLE_MODE_XBOX)
        ? ble_xbox_profile_data : profile_data;
    printf("[ble_output] Using %s GATT database (ptr=%p)\n",
           (current_mode == BLE_MODE_XBOX) ? "Xbox" : "Standard", gatt_db);
    att_server_init(gatt_db, NULL, att_write_callback);

#ifdef SINPUT_BLE_COMPOSITE
    // HID service instance #2 (keyboard + mouse), SInput mode only. hids_device
    // binds service #1 and this handler owns service #2's range. Standard
    // mode keeps sending its keyboard/mouse inside its own composite map.
    if (current_mode == BLE_MODE_SINPUT) {
        hid2 = &gamepad_hid2_handles;
    }
    if (hid2) {
        hid2_service_handler.start_handle = hid2->service_start;
        hid2_service_handler.end_handle = hid2->service_end;
        hid2_service_handler.read_callback = &hid2_read_callback;
        hid2_service_handler.write_callback = &hid2_write_callback;
        att_server_register_service_handler(&hid2_service_handler);
    }
#endif

    // Setup GATT services
    battery_service_server_init(100);
#ifdef BTSTACK_USE_ESP32
    extern void battery_monitor_init(void);
    battery_monitor_init();
#endif
    // Start sampling VBAT into the Battery Service. First sample shortly after
    // boot so it isn't stuck at the 100% init value; then every 60s. No-op on
    // boards where platform_battery_millivolts() returns -1.
    btstack_run_loop_set_timer_handler(&battery_timer, battery_timer_handler);
    btstack_run_loop_set_timer(&battery_timer, 2000);
    btstack_run_loop_add_timer(&battery_timer);

    // Enforce USB dominance (drop/suppress BT while a USB host is connected).
    btstack_run_loop_set_timer_handler(&usb_dom_timer, usb_dom_timer_handler);
    btstack_run_loop_set_timer(&usb_dom_timer, 500);
    btstack_run_loop_add_timer(&usb_dom_timer);

    device_information_service_server_init();

    // Mode-dependent PnP ID and device info
    if (current_mode == BLE_MODE_XBOX) {
        device_information_service_server_set_manufacturer_name("Microsoft");
        device_information_service_server_set_model_number("Xbox Wireless Controller");
        device_information_service_server_set_software_revision("1.0.0");
        // PnP ID: USB IF (0x02), Microsoft VID 0x045E, Xbox Series X PID
        // 0x0B13, product version 0x0509 — the identity verified to make
        // Windows load its own Xbox driver against this report map.
        device_information_service_server_set_pnp_id(0x02, 0x045E, 0x0B13, 0x0509);
    } else if (current_mode == BLE_MODE_SINPUT) {
        device_information_service_server_set_manufacturer_name(SINPUT_MANUFACTURER);
        device_information_service_server_set_model_number(SINPUT_PRODUCT);
        device_information_service_server_set_software_revision("1.0.0");
        // PnP ID: USB IF (0x02) + SInput VID/PID so SDL's SInput driver matches.
        device_information_service_server_set_pnp_id(0x02, SINPUT_VID, SINPUT_PID, SINPUT_BCD_DEVICE);
    } else {
        device_information_service_server_set_manufacturer_name("Joypad");
        device_information_service_server_set_model_number(APP_NAME);
        device_information_service_server_set_software_revision("1.0.0");
        // PnP ID: Bluetooth SIG (0x01), VID 0xe502, PID 0xbbab, version 1.0.0
        device_information_service_server_set_pnp_id(0x01, 0xe502, 0xbbab, 0x0100);
    }

    // Setup HID Device service with mode-appropriate descriptor
    const uint8_t *hid_desc;
    uint16_t hid_desc_size;
    if (current_mode == BLE_MODE_XBOX) {
        hid_desc = ble_xbox_get_descriptor();
        hid_desc_size = ble_xbox_get_descriptor_size();
    } else if (current_mode == BLE_MODE_SINPUT) {
        // Pure SInput gamepad map on HID service #1. The composite's keyboard
        // and mouse live on HID service #2 (sinput_kbd_mouse_tail) — see the
        // hid2_* handler for why they must be a separate service instance.
        hid_desc = sinput_report_descriptor;
        hid_desc_size = sizeof(sinput_report_descriptor);
    } else {
        hid_desc = standard_hid_descriptor;
        hid_desc_size = sizeof(standard_hid_descriptor);
    }
    hids_device_init_with_storage(0, hid_desc, hid_desc_size,
        HID_REPORT_STORAGE_COUNT, hid_report_storage);

    // Setup Security Manager: No Input No Output, bonding enabled
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(SM_AUTHREQ_SECURE_CONNECTION | SM_AUTHREQ_BONDING);

    // Each mode uses a distinct BLE address so hosts see them as separate devices.
    // Derive a static random address from the real BD_ADDR, with mode in the last byte.
    // Static random addresses have the two MSBs of the first byte set to 11.
    if (current_mode != BLE_MODE_STANDARD) {
        bd_addr_t base_addr;
        gap_local_bd_addr(base_addr);
        bd_addr_t mode_addr;
        memcpy(mode_addr, base_addr, 6);
        mode_addr[5] ^= (uint8_t)current_mode;  // Vary last byte by mode
        mode_addr[0] |= 0xC0;                   // Mark as static random address
        gap_random_address_set(mode_addr);
        gap_random_address_set_mode(GAP_RANDOM_ADDRESS_TYPE_STATIC);
        printf("[ble_output] Using distinct BLE address for mode %d\n", current_mode);
    }
#ifdef BTSTACK_USE_NRF
    else {
        // Standard mode on nRF: the SoftDevice has no public BD_ADDR, so
        // advertising must use a random static address. The nRF transport
        // sets the address VALUE during HCI init (from the chip's static
        // address), but the address MODE must be enabled here or BTstack
        // advertises with (empty) public addressing and never goes on air —
        // the device is not discoverable. Non-nRF transports (CYW43) have a
        // real public address, so leave them on the default.
        gap_random_address_set_mode(GAP_RANDOM_ADDRESS_TYPE_STATIC);
        printf("[ble_output] Standard mode: using random static address (nRF)\n");
    }
#endif

    // Mode-dependent GAP name and advertising
    const char *gap_name;
    const uint8_t *adv_data;
    uint16_t adv_data_len;
    if (current_mode == BLE_MODE_XBOX) {
        gap_name = "Joypad Xinput";
        adv_data = adv_data_xbox;
        adv_data_len = sizeof(adv_data_xbox);
    } else if (current_mode == BLE_MODE_SINPUT) {
        gap_name = "Joypad SInput";
        adv_data = adv_data_standard;  // generic HID adv (appearance = gamepad)
        adv_data_len = sizeof(adv_data_standard);
    } else {
        gap_name = "JoypadOS Controller";
        adv_data = adv_data_standard;
        adv_data_len = sizeof(adv_data_standard);
    }
    gap_set_local_name(gap_name);

    uint16_t adv_int_min = 0x0030;  // 30ms
    uint16_t adv_int_max = 0x0030;  // 30ms
    bd_addr_t null_addr;
    memset(null_addr, 0, 6);
    gap_advertisements_set_params(adv_int_min, adv_int_max, 0, 0, null_addr, 0x07, 0x00);
    gap_advertisements_set_data(adv_data_len, (uint8_t *)adv_data);
    // Standard mode keeps the complete name in the scan response (the primary
    // adv packet has no room — see adv_data_standard note). Xbox mode's name
    // fits in its adv packet, so no scan response needed there.
    if (current_mode != BLE_MODE_XBOX) {
        gap_scan_response_set_data(sizeof(scan_resp_standard), (uint8_t *)scan_resp_standard);
    }
    // Always advertise: wireless policy only routes input, never turns BT off.
    set_adv(true);

    // Register event handlers
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    sm_event_callback_registration.callback = &packet_handler;
    sm_add_event_handler(&sm_event_callback_registration);

    hids_device_register_packet_handler(packet_handler);

    // Initialize NUS (Nordic UART Service) for wireless config. Standard and
    // SInput modes share the composite GATT (which includes NUS); Xbox mode uses
    // a different GATT profile without NUS. SDL matches SInput by VID/PID + HID,
    // so the extra NUS service is inert to it.
    if (current_mode == BLE_MODE_STANDARD || current_mode == BLE_MODE_SINPUT) {
        ble_nus_init();
    }

    printf("[ble_output] BLE advertising as '%s'\n", gap_name);
}

// ============================================================================
// TASK — Standard mode (composite: gamepad + keyboard + mouse)
// ============================================================================

static void ble_output_task_standard(void)
{
    const input_event_t *event = router_get_output(OUTPUT_TARGET_BLE_PERIPHERAL, 0);

    // Stream output event to CDC/NUS for web config (if enabled)
    if (event) cdc_commands_send_player_output(0, event->buttons, event->analog);

    // Wireless policy: under USB-dominant with a USB host, input is routed
    // only to USB (one neutral report on the mute edge, then nothing).
    event = ble_mute_filter(event);
    if (!event) return;

    switch (event->type) {
        // NOTE (all cases): the router hands out each event exactly once, so a
        // gated or dropped event is gone forever (stuck buttons). Compare
        // against what will actually go out — the queued report when one is
        // still waiting for CAN_SEND_NOW — and overwrite it, never drop.
        case INPUT_TYPE_KEYBOARD: {
            ble_keyboard_report_t report;
            ble_keyboard_report_from_event(event, &report);
            const ble_keyboard_report_t *ref = (pending_type == PENDING_KEYBOARD)
                ? &pending_keyboard : &last_sent_keyboard;
            if (memcmp(&report, ref, sizeof(report)) == 0) return;
            pending_keyboard = report;
            pending_type = PENDING_KEYBOARD;
            ble_request_can_send_now(con_handle);
            break;
        }

        case INPUT_TYPE_MOUSE: {
            ble_mouse_report_t report;
            ble_mouse_report_from_event(event, &report);
            const ble_mouse_report_t *ref = (pending_type == PENDING_MOUSE)
                ? &pending_mouse : &last_sent_mouse;
            if (memcmp(&report, ref, sizeof(report)) == 0) return;
            pending_mouse = report;
            pending_type = PENDING_MOUSE;
            ble_request_can_send_now(con_handle);
            break;
        }

        default: {
            #define SCALE_8_TO_16(v) ((int16_t)((uint32_t)(v) * 32767 / 255))
            ble_gamepad_report_t report;
            uint16_t buttons = convert_buttons(event->buttons);
            report.buttons_lo = buttons & 0xFF;
            report.buttons_hi = (buttons >> 8) & 0xFF;
            report.hat = convert_dpad_to_hat(event->buttons);
            report.lx = SCALE_8_TO_16(event->analog[ANALOG_LX]);
            report.ly = SCALE_8_TO_16(event->analog[ANALOG_LY]);
            report.rx = SCALE_8_TO_16(event->analog[ANALOG_RX]);
            report.ry = SCALE_8_TO_16(event->analog[ANALOG_RY]);
            report.lt = SCALE_8_TO_16(event->analog[ANALOG_L2]);
            report.rt = SCALE_8_TO_16(event->analog[ANALOG_R2]);
            const ble_gamepad_report_t *ref = (pending_type == PENDING_GAMEPAD)
                ? &pending_gamepad : &last_sent_gamepad;
            if (memcmp(&report, ref, sizeof(report)) == 0) return;
            pending_gamepad = report;
            pending_type = PENDING_GAMEPAD;
            ble_request_can_send_now(con_handle);
            break;
        }
    }
}

// Keyboard (ID 6) and mouse (ID 8) on HID service #2: typed events go out
// their own report characteristics; only gamepad-shaped events feed the
// mode's gamepad report. Same coalescing rules as everywhere else — held
// keyboard state overwrites the queued report, one-shot mouse deltas
// ACCUMULATE into it so a fast drag never loses motion. Returns true when
// the event was a keyboard/mouse event (consumed here).
static bool hid2_dispatch_event(const input_event_t *event)
{
#ifdef SINPUT_BLE_COMPOSITE
    if (!hid2) return false;
    if (event->type == INPUT_TYPE_KEYBOARD) {
        ble_keyboard_report_t kb;
        ble_keyboard_report_from_event(event, &kb);
        const ble_keyboard_report_t *kref = hid2_kbd_pending
            ? &pending_keyboard : &last_sent_keyboard;
        if (memcmp(&kb, kref, sizeof(kb)) == 0) return true;
        pending_keyboard = kb;
        hid2_kbd_pending = true;
        hid2_request_can_send_now(con_handle);
        return true;
    }
    if (event->type == INPUT_TYPE_MOUSE && !event->as_gamepad) {
        uint8_t mb = 0;
        if (event->buttons & JP_BUTTON_B1) mb |= (1 << 0);  // Left
        if (event->buttons & JP_BUTTON_B2) mb |= (1 << 1);  // Right
        if (event->buttons & JP_BUTTON_B3) mb |= (1 << 2);  // Middle
        if (event->buttons & JP_BUTTON_S1) mb |= (1 << 3);  // Back
        if (event->buttons & JP_BUTTON_S2) mb |= (1 << 4);  // Forward
        if (hid2_mouse_pending) {
            // Report still queued: fold this event's deltas into it.
            int32_t x = (int32_t)pending_sinput_mouse.x + event->delta_x;
            int32_t y = (int32_t)pending_sinput_mouse.y + event->delta_y;
            int32_t w = (int32_t)pending_sinput_mouse.wheel + event->delta_wheel;
            pending_sinput_mouse.x = (int16_t)((x > 32767) ? 32767 : (x < -32767) ? -32767 : x);
            pending_sinput_mouse.y = (int16_t)((y > 32767) ? 32767 : (y < -32767) ? -32767 : y);
            pending_sinput_mouse.wheel = (int8_t)((w > 127) ? 127 : (w < -127) ? -127 : w);
            pending_sinput_mouse.buttons = mb;
            return true;  // send request already in flight
        }
        memset(&pending_sinput_mouse, 0, sizeof(pending_sinput_mouse));
        pending_sinput_mouse.buttons = mb;
        pending_sinput_mouse.x = event->delta_x;
        pending_sinput_mouse.y = event->delta_y;
        pending_sinput_mouse.wheel = event->delta_wheel;
        hid2_mouse_pending = true;
        hid2_request_can_send_now(con_handle);
        return true;
    }
#else
    (void)event;
#endif
    return false;
}

// ============================================================================
// TASK — Xbox BLE mode (gamepad only, with rumble feedback)
// ============================================================================

static void ble_output_task_xbox(void)
{
    const input_event_t *event = router_get_output(OUTPUT_TARGET_BLE_PERIPHERAL, 0);

    // Stream output event to CDC/NUS for web config (if enabled)
    if (event) cdc_commands_send_player_output(0, event->buttons, event->analog);

    // Wireless policy: under USB-dominant with a USB host, input is routed
    // only to USB (one neutral report on the mute edge, then nothing).
    event = ble_mute_filter(event);
    if (!event) return;

    ble_xbox_report_t report;
    ble_xbox_report_from_event(event, &report);
    // Router events are consume-once: compare against the queued report (not
    // just last-sent) and overwrite it, or a press→release inside one
    // connection interval loses the release (stuck button).
    const ble_xbox_report_t *ref = (pending_type == PENDING_XBOX)
        ? &pending_xbox : &last_sent_xbox;
    if (memcmp(&report, ref, sizeof(report)) == 0) return;

    pending_xbox = report;
    pending_type = PENDING_XBOX;
    ble_request_can_send_now(con_handle);
}

// ============================================================================
// TASK — SInput BLE mode (SDL/Steam: buttons + IMU + battery + rumble)
// ============================================================================

static void ble_output_task_sinput(void)
{
    const input_event_t *event = router_get_output(OUTPUT_TARGET_BLE_PERIPHERAL, 0);

    // Stream output event to CDC/NUS for web config (if enabled)
    if (event) cdc_commands_send_player_output(0, event->buttons, event->analog);

    // Wireless policy: under USB-dominant with a USB host, input is routed
    // only to USB (one neutral report on the mute edge, then nothing).
    event = ble_mute_filter(event);
    if (!event) return;

    if (hid2_dispatch_event(event)) return;

    // Build the input report; may flag a feature refresh on device change. The
    // host's features request (output report) also sets the pending flag.
    // Router events are consume-once, so this event can never be dropped:
    // it either updates the queued report or is stashed to follow a feature.
    sinput_report_t report;
    sinput_report_build_from_event(&report, event);

    // Feature response takes priority — SDL blocks on the handshake.
    uint16_t flen;
    if (pending_type == PENDING_NONE &&
        sinput_feature_response_take(pending_feature, &flen)) {
        pending_feature_len = flen;
        pending_type = PENDING_SINPUT_FEATURE;
        // Don't lose the input state consumed on this call: queue it behind
        // the feature (CAN_SEND_NOW sends it next).
        pending_sinput = report;
        sinput_input_after_feature = true;
        ble_request_can_send_now(con_handle);
        return;
    }

    if (pending_type == PENDING_SINPUT_FEATURE) {
        // Feature in flight: remember the latest input state to send after it.
        pending_sinput = report;
        sinput_input_after_feature = true;
        return;
    }

    // Send the input report when changed — compared against the queued report
    // when one is waiting, else the last one sent. (The IMU timestamp advances
    // each build, so a device with motion streams continuously.)
    const sinput_report_t *ref = (pending_type == PENDING_SINPUT)
        ? &pending_sinput : &last_sent_sinput;
    if (memcmp(&report, ref, sizeof(report)) == 0) return;
    pending_sinput = report;
    pending_type = PENDING_SINPUT;
    ble_request_can_send_now(con_handle);
}

// ============================================================================
// MAIN TASK DISPATCH
// ============================================================================

void ble_output_task(void)
{
#ifdef CONFIG_BT_CLASSIC_OUTPUT
    if (current_mode == BLE_MODE_SWITCH_BT) { switch_bt_task(); return; }
#endif
    if (!ble_connected || con_handle == HCI_CON_HANDLE_INVALID) return;

    if (current_mode == BLE_MODE_XBOX) {
        ble_output_task_xbox();
    } else if (current_mode == BLE_MODE_SINPUT) {
        ble_output_task_sinput();
    } else {
        ble_output_task_standard();
    }
}

// ============================================================================
// MODE SELECTION
// ============================================================================

ble_output_mode_t ble_output_get_mode(void)
{
    return current_mode;
}

void ble_output_set_mode(ble_output_mode_t mode)
{
    if (!ble_output_mode_available(mode) || mode == current_mode) return;

    printf("[ble_output] Switching mode from %s to %s\n",
           ble_output_get_mode_name(current_mode),
           ble_output_get_mode_name(mode));

    // Save to flash (ble_mode_saved marks this as an explicit user choice so
    // apps with a forced default — universal — honor it after reboot)
    flash_t *settings = flash_get_settings();
    if (settings) {
        settings->ble_output_mode = (uint8_t)mode;
        settings->ble_mode_saved = 1;
        flash_save_force(settings);
    }

    // Brief delay to allow flash write to complete
    platform_sleep_ms(50);

    // Reboot to apply new HID descriptor
    printf("[ble_output] Rebooting for new HID descriptor...\n");
    platform_reboot();
}

ble_output_mode_t ble_output_get_next_mode(void)
{
    // Skip modes not compiled into this build (e.g. Switch-BT on BLE-only radios)
    // so the physical mode-cycle button never lands on an unavailable mode.
    ble_output_mode_t m = current_mode;
    for (int i = 0; i < BLE_MODE_COUNT; i++) {
        m = (ble_output_mode_t)((m + 1) % BLE_MODE_COUNT);
        if (ble_output_mode_available(m)) return m;
    }
    return current_mode;
}

const char* ble_output_get_mode_name(ble_output_mode_t mode)
{
    switch (mode) {
        case BLE_MODE_STANDARD:  return "Standard BLE";
        case BLE_MODE_XBOX:      return "Xbox BLE";
        case BLE_MODE_SINPUT:    return "SInput BLE";
        case BLE_MODE_SWITCH_BT: return "Switch (BT)";
        default:                 return "Unknown";
    }
}

void ble_output_get_mode_color(ble_output_mode_t mode, uint8_t *r, uint8_t *g, uint8_t *b)
{
    switch (mode) {
        case BLE_MODE_STANDARD:  *r = 0; *g = 0; *b = 64; break;   // Blue
        case BLE_MODE_XBOX:      *r = 0; *g = 64; *b = 0; break;   // Green
        case BLE_MODE_SINPUT:    *r = 0; *g = 32; *b = 64; break;  // Cyan
        case BLE_MODE_SWITCH_BT: *r = 64; *g = 0; *b = 0; break;   // Red (Switch/Classic)
        default:                 *r = 64; *g = 64; *b = 64; break;  // White
    }
}

// ============================================================================
// OUTPUT INTERFACE EXPORT
// ============================================================================

const OutputInterface ble_output_interface = {
    .name = "BLE HID",
    .target = OUTPUT_TARGET_BLE_PERIPHERAL,
    .init = ble_output_init,
    .task = ble_output_task,
    .core1_task = NULL,
    .get_feedback = NULL,
    .get_rumble = NULL,
    .get_player_led = NULL,
    .get_profile_count = NULL,
    .get_active_profile = NULL,
    .set_active_profile = NULL,
    .get_profile_name = NULL,
    .get_trigger_threshold = NULL,
};
