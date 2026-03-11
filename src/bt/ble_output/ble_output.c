// ble_output.c - BLE Composite HID Output Interface (HOGP Peripheral)
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Implements OutputInterface for BLE HID composite output (gamepad + keyboard + mouse)
// using BTstack's hids_device GATT service. Appears as a wireless HID device via HOGP.

#include "ble_output.h"
#include "ble_output_keyboard.h"
#include "ble_output_mouse.h"
#include "ble_gamepad.h"  // Generated from ble_gamepad.gatt by compile_gatt.py

#include "core/buttons.h"
#include "core/input_event.h"
#include "core/router/router.h"

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

#include <stdio.h>
#include <string.h>

// ============================================================================
// HID REPORT DESCRIPTOR — Composite: Keyboard + Mouse + Gamepad
// ============================================================================

static const uint8_t ble_hid_descriptor[] = {
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

// Gamepad report (15 bytes, Report ID 3)
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

// Hat switch values (0-7 = directions, 8 = center/null)
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
// PENDING REPORT — type-tagged union for flow-controlled sending
// ============================================================================

typedef enum {
    PENDING_NONE = 0,
    PENDING_GAMEPAD,
    PENDING_KEYBOARD,
    PENDING_MOUSE,
} pending_report_type_t;

// ============================================================================
// STATE
// ============================================================================

static hci_con_handle_t con_handle = HCI_CON_HANDLE_INVALID;
static bool ble_connected = false;

// Pending report (flow-controlled — only one at a time)
static pending_report_type_t pending_type = PENDING_NONE;
static ble_gamepad_report_t pending_gamepad;
static ble_keyboard_report_t pending_keyboard;
static ble_mouse_report_t pending_mouse;

// Last sent reports (for change detection)
static ble_gamepad_report_t last_sent_gamepad;
static ble_keyboard_report_t last_sent_keyboard;
static ble_mouse_report_t last_sent_mouse;

// Report storage for hids_device_init_with_storage()
// 6 reports: keyboard input, keyboard output, mouse input, gamepad input, player output, feature
static hids_device_report_t hid_report_storage[6];
#define HID_REPORT_STORAGE_COUNT (sizeof(hid_report_storage) / sizeof(hid_report_storage[0]))

// Mode
static ble_output_mode_t current_mode = BLE_MODE_STANDARD;

static btstack_packet_callback_registration_t hci_event_callback_registration;
static btstack_packet_callback_registration_t sm_event_callback_registration;

// ============================================================================
// ADVERTISING DATA
// ============================================================================

static const uint8_t adv_data[] = {
    // Flags: general discoverable, BR/EDR not supported
    0x02, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    // Complete local name: "Joypad Gamepad"
    0x0F, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME,
    'J', 'o', 'y', 'p', 'a', 'd', ' ',
    'G', 'a', 'm', 'e', 'p', 'a', 'd',
    // 16-bit Service UUIDs: HID Service
    0x03, BLUETOOTH_DATA_TYPE_COMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS,
    ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE & 0xFF,
    ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE >> 8,
    // Appearance: Gamepad (0x03C4)
    0x03, BLUETOOTH_DATA_TYPE_APPEARANCE, 0xC4, 0x03,
};

// ============================================================================
// CONVERSION HELPERS
// ============================================================================

// Convert Joypad buttons to BLE gamepad 16-bit button field
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

// Convert Joypad dpad to hat switch value
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
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            con_handle = HCI_CON_HANDLE_INVALID;
            ble_connected = false;
            pending_type = PENDING_NONE;
            printf("[ble_output] Disconnected, restarting advertising\n");
            gap_advertisements_enable(1);
            break;

        case SM_EVENT_JUST_WORKS_REQUEST:
            sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
            break;

        case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
            sm_numeric_comparison_confirm(sm_event_passkey_display_number_get_handle(packet));
            break;

        case HCI_EVENT_HIDS_META:
            switch (hci_event_hids_meta_get_subevent_code(packet)) {
                case HIDS_SUBEVENT_INPUT_REPORT_ENABLE:
                    con_handle = hids_subevent_input_report_enable_get_con_handle(packet);
                    ble_connected = true;
                    printf("[ble_output] BLE connected (handle=0x%04x)\n", con_handle);
                    break;

                case HIDS_SUBEVENT_CAN_SEND_NOW:
                    if (pending_type != PENDING_NONE && con_handle != HCI_CON_HANDLE_INVALID) {
                        switch (pending_type) {
                            case PENDING_GAMEPAD:
                                hids_device_send_input_report_for_id(con_handle, 3,
                                    (const uint8_t *)&pending_gamepad, sizeof(pending_gamepad));
                                last_sent_gamepad = pending_gamepad;
                                break;
                            case PENDING_KEYBOARD:
                                hids_device_send_input_report_for_id(con_handle, 1,
                                    (const uint8_t *)&pending_keyboard, sizeof(pending_keyboard));
                                last_sent_keyboard = pending_keyboard;
                                break;
                            case PENDING_MOUSE:
                                hids_device_send_input_report_for_id(con_handle, 2,
                                    (const uint8_t *)&pending_mouse, sizeof(pending_mouse));
                                last_sent_mouse = pending_mouse;
                                break;
                            default:
                                break;
                        }
                        pending_type = PENDING_NONE;
                    }
                    break;

                case HIDS_SUBEVENT_SET_REPORT: {
                    uint8_t report_id = hids_subevent_set_report_get_report_id(packet);
                    uint8_t report_type = hids_subevent_set_report_get_report_type(packet);
                    (void)report_type;
                    if (report_id == 1) {
                        // Keyboard LED output report — ignore for now
                    } else if (report_id == 4) {
                        // Player indicator output report — ignore for now
                    }
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
    printf("[ble_output] Initializing BLE Composite HID output (early)\n");

    // Initialize gamepad report to neutral state
    memset(&pending_gamepad, 0, sizeof(pending_gamepad));
    pending_gamepad.hat = BLE_HAT_CENTER;
    pending_gamepad.lx = 16384;  // 0x4000 = center of 0-32767
    pending_gamepad.ly = 16384;
    pending_gamepad.rx = 16384;
    pending_gamepad.ry = 16384;
    last_sent_gamepad = pending_gamepad;

    memset(&last_sent_keyboard, 0, sizeof(last_sent_keyboard));
    memset(&last_sent_mouse, 0, sizeof(last_sent_mouse));
}

// Called after bt_init() — BTstack must be running before GATT/GAP setup
void ble_output_late_init(void)
{
    printf("[ble_output] Setting up BLE GATT services\n");

    // Setup ATT server with compiled GATT profile
    att_server_init(profile_data, NULL, NULL);

    // Setup GATT services
    battery_service_server_init(100);
    device_information_service_server_init();
    device_information_service_server_set_manufacturer_name("Joypad");
    device_information_service_server_set_model_number("USB2BLE");
    device_information_service_server_set_software_revision("1.0.0");
    // PnP ID: Bluetooth SIG (0x01), VID 0xe502, PID 0xbbab, version 1.0.0
    device_information_service_server_set_pnp_id(0x01, 0xe502, 0xbbab, 0x0100);

    // Setup HID Device service (6 reports need custom storage)
    hids_device_init_with_storage(0, ble_hid_descriptor, sizeof(ble_hid_descriptor),
        HID_REPORT_STORAGE_COUNT, hid_report_storage);

    // Setup Security Manager: No Input No Output, bonding enabled
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(SM_AUTHREQ_SECURE_CONNECTION | SM_AUTHREQ_BONDING);

    // Override GAP local name (btstack_host sets "Joypad Adapter" for host mode)
    gap_set_local_name("Joypad Gamepad");

    // Setup advertisements
    uint16_t adv_int_min = 0x0030;  // 30ms
    uint16_t adv_int_max = 0x0030;  // 30ms
    bd_addr_t null_addr;
    memset(null_addr, 0, 6);
    gap_advertisements_set_params(adv_int_min, adv_int_max, 0, 0, null_addr, 0x07, 0x00);
    gap_advertisements_set_data(sizeof(adv_data), (uint8_t *)adv_data);
    gap_advertisements_enable(1);

    // Register event handlers
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    sm_event_callback_registration.callback = &packet_handler;
    sm_add_event_handler(&sm_event_callback_registration);

    hids_device_register_packet_handler(packet_handler);

    printf("[ble_output] BLE Composite HID advertising as 'Joypad Gamepad'\n");
}

void ble_output_task(void)
{
    if (!ble_connected || con_handle == HCI_CON_HANDLE_INVALID) return;

    // Poll router for latest input
    const input_event_t *event = router_get_output(OUTPUT_TARGET_BLE_PERIPHERAL, 0);
    if (!event) return;

    // Route by input device type
    switch (event->type) {
        case INPUT_TYPE_KEYBOARD: {
            ble_keyboard_report_t report;
            ble_keyboard_report_from_event(event, &report);

            if (memcmp(&report, &last_sent_keyboard, sizeof(report)) == 0) return;

            pending_keyboard = report;
            pending_type = PENDING_KEYBOARD;
            hids_device_request_can_send_now_event(con_handle);
            break;
        }

        case INPUT_TYPE_MOUSE: {
            ble_mouse_report_t report;
            ble_mouse_report_from_event(event, &report);

            // Always send mouse reports (relative motion resets to 0, so "no change" is still meaningful)
            if (memcmp(&report, &last_sent_mouse, sizeof(report)) == 0) return;

            pending_mouse = report;
            pending_type = PENDING_MOUSE;
            hids_device_request_can_send_now_event(con_handle);
            break;
        }

        default: {
            // Gamepad (default for all controller types)
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

            if (memcmp(&report, &last_sent_gamepad, sizeof(report)) == 0) return;

            pending_gamepad = report;
            pending_type = PENDING_GAMEPAD;
            hids_device_request_can_send_now_event(con_handle);
            break;
        }
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
    if (mode >= BLE_MODE_COUNT) return;
    current_mode = mode;
}

ble_output_mode_t ble_output_get_next_mode(void)
{
    return (ble_output_mode_t)((current_mode + 1) % BLE_MODE_COUNT);
}

const char* ble_output_get_mode_name(ble_output_mode_t mode)
{
    switch (mode) {
        case BLE_MODE_STANDARD: return "Standard (Composite)";
        case BLE_MODE_XBOX:     return "Xbox BLE";
        default:                return "Unknown";
    }
}

void ble_output_get_mode_color(ble_output_mode_t mode, uint8_t *r, uint8_t *g, uint8_t *b)
{
    switch (mode) {
        case BLE_MODE_STANDARD: *r = 0; *g = 0; *b = 64; break;   // Blue
        case BLE_MODE_XBOX:     *r = 0; *g = 64; *b = 0; break;   // Green
        default:                *r = 64; *g = 64; *b = 64; break;  // White
    }
}

// ============================================================================
// OUTPUT INTERFACE EXPORT
// ============================================================================

const OutputInterface ble_output_interface = {
    .name = "BLE Composite HID",
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
