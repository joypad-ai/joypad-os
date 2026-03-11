// ble_output.c - BLE Gamepad Output Interface (HOGP Peripheral)
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Implements OutputInterface for BLE HID gamepad output using BTstack's
// hids_device GATT service. Appears as a wireless gamepad via HOGP.

#include "ble_output.h"
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
// HID REPORT DESCRIPTOR — Clean BLE gamepad
// ============================================================================
// 16 buttons (2 bytes) + hat switch (4 bits + 4 padding) + 4 stick axes + 2 triggers
// Total report: 9 bytes

static const uint8_t ble_hid_descriptor[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    0x85, 0x03,        //   Report ID (3) — matches ESP32-BLE-Gamepad & hids.gatt

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

    // 6 axes × 16-bit: X, Y, Z, Rz (sticks), Rx, Ry (triggers)
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
};

// ============================================================================
// BLE REPORT STRUCTURE (15 bytes, matches descriptor above)
// ============================================================================
// 16 buttons (2B) + hat (1B) + 6 axes × 16-bit (12B) = 15 bytes

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
// STATE
// ============================================================================

static hci_con_handle_t con_handle = HCI_CON_HANDLE_INVALID;
static bool ble_connected = false;
static bool report_pending = false;
static ble_gamepad_report_t pending_report;
static ble_gamepad_report_t last_sent_report;

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
// Maps directly: bit 0 = B1 (Cross/A), bit 1 = B2 (Circle/B), etc.
// 14 buttons: B1-B4, L1-R2, S1-S2, L3-R3, A1, A2
// D-pad is hat switch only (not duplicated in button bits)
static uint16_t convert_buttons(uint32_t buttons)
{
    uint16_t ble_buttons = 0;

    if (buttons & JP_BUTTON_B1) ble_buttons |= (1 << 0);   // Button 1: Cross/A
    if (buttons & JP_BUTTON_B2) ble_buttons |= (1 << 1);   // Button 2: Circle/B
    if (buttons & JP_BUTTON_B3) ble_buttons |= (1 << 2);   // Button 3: Square/X
    if (buttons & JP_BUTTON_B4) ble_buttons |= (1 << 3);   // Button 4: Triangle/Y
    if (buttons & JP_BUTTON_L1) ble_buttons |= (1 << 4);   // Button 5: L1/LB
    if (buttons & JP_BUTTON_R1) ble_buttons |= (1 << 5);   // Button 6: R1/RB
    if (buttons & JP_BUTTON_L2) ble_buttons |= (1 << 6);   // Button 7: L2/LT
    if (buttons & JP_BUTTON_R2) ble_buttons |= (1 << 7);   // Button 8: R2/RT
    if (buttons & JP_BUTTON_S1) ble_buttons |= (1 << 8);   // Button 9: Select/Back
    if (buttons & JP_BUTTON_S2) ble_buttons |= (1 << 9);   // Button 10: Start
    if (buttons & JP_BUTTON_L3) ble_buttons |= (1 << 10);  // Button 11: L3
    if (buttons & JP_BUTTON_R3) ble_buttons |= (1 << 11);  // Button 12: R3
    if (buttons & JP_BUTTON_A1) ble_buttons |= (1 << 12);  // Button 13: Guide/Home
    if (buttons & JP_BUTTON_A2) ble_buttons |= (1 << 13);  // Button 14: Misc

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
            report_pending = false;
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
                    if (report_pending && con_handle != HCI_CON_HANDLE_INVALID) {
                        const uint8_t *rpt = (const uint8_t *)&pending_report;
                        hids_device_send_input_report(con_handle,
                            rpt, sizeof(pending_report));
                        last_sent_report = pending_report;
                        report_pending = false;
                    }
                    break;

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
    printf("[ble_output] Initializing BLE Gamepad output (early)\n");

    // Initialize report to neutral state (no BTstack dependency)
    memset(&pending_report, 0, sizeof(pending_report));
    pending_report.hat = BLE_HAT_CENTER;
    pending_report.lx = 16384;  // 0x4000 = center of 0-32767
    pending_report.ly = 16384;
    pending_report.rx = 16384;
    pending_report.ry = 16384;
    last_sent_report = pending_report;
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
    // Mirrors ESP32-BLE-Gamepad convention for BLE gamepad compatibility
    device_information_service_server_set_pnp_id(0x01, 0xe502, 0xbbab, 0x0100);

    // Setup HID Device service
    hids_device_init(0, ble_hid_descriptor, sizeof(ble_hid_descriptor));

    // Setup Security Manager: No Input No Output, bonding enabled
    sm_set_io_capabilities(IO_CAPABILITY_NO_INPUT_NO_OUTPUT);
    sm_set_authentication_requirements(SM_AUTHREQ_SECURE_CONNECTION | SM_AUTHREQ_BONDING);

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

    printf("[ble_output] BLE Gamepad advertising as 'Joypad Gamepad'\n");
}

void ble_output_task(void)
{
    if (!ble_connected || con_handle == HCI_CON_HANDLE_INVALID) return;

    // Poll router for latest input
    const input_event_t *event = router_get_output(OUTPUT_TARGET_BLE_PERIPHERAL, 0);
    if (!event) return;

    // Build report from input event
    // Scale 8-bit input (0-255) to 16-bit BLE (0-32767): val * 32767 / 255
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

    // Only send if report changed (avoid flooding BLE link)
    if (memcmp(&report, &last_sent_report, sizeof(report)) == 0) return;

    // Queue report and request send slot (flow-controlled)
    pending_report = report;
    report_pending = true;
    hids_device_request_can_send_now_event(con_handle);
}

// ============================================================================
// OUTPUT INTERFACE EXPORT
// ============================================================================

const OutputInterface ble_output_interface = {
    .name = "BLE Gamepad",
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
