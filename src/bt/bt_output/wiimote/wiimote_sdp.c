// wiimote_sdp.c - BTstack Classic HID + SDP setup for Wiimote emulation
// SPDX-License-Identifier: Apache-2.0
//
// Implements the BTstack-facing glue for the Wiimote emulator. The critical
// bits that make a real Wii recognize us as RVL-CNT-01:
//
//   1. GAP class-of-device 0x002504 (Peripheral / Gamepad / Major Peripheral)
//   2. Local name exactly "Nintendo RVL-CNT-01"
//   3. SSP (Secure Simple Pairing) DISABLED — Wiimote uses legacy PIN pairing
//   4. PIN-code response = reversed Wii BD_ADDR (the classic trick)
//   5. Outgoing connect to the paired Wii (Wiimote is the connection initiator,
//      triggered by the user pressing a button)
//   6. Standard Bluetooth HID SDP record, populated with the Wiimote HID
//      descriptor from wiimote_hid_descriptor.h
//
// What we intentionally DO NOT (yet) replicate:
//   - The 5 canned custom SDP responses that the real Wiimote returns.
//     Most Wiis work with the standard HID SDP record; if not, expand later.
//   - IAC LAP limited-inquiry trick (affects discoverability during sync).
//
// Build note: requires the CLASSIC Bluetooth HID device stack. Must be
// linked into apps that declare a BT Classic transport (Pico W CYW43 or
// USB BT dongle). BLE-only platforms (ESP32-S3, nRF52840) won't build this.

#include "wiimote_sdp.h"
#include "wiimote_hid_descriptor.h"

// BTstack includes (specific headers only — <btstack.h> drags in audio codecs)
#include "btstack_config.h"
#include "bluetooth.h"
#include "bluetooth_data_types.h"
#include "btstack_defines.h"
#include "btstack_event.h"
#include "btstack_util.h"
#include "gap.h"
#include "hci.h"
#include "l2cap.h"
#include "classic/hid_device.h"
#include "classic/sdp_util.h"
#include "classic/sdp_server.h"
#include "classic/device_id_server.h"

#include <stdio.h>
#include <string.h>

// ============================================================================
// INTERNAL STATE
// ============================================================================

static uint8_t  hid_service_buffer[400];
static uint8_t  device_id_buffer[100];
static uint16_t hid_cid = 0;
static bool     hid_ready = false;          // HID control+interrupt channels up
static bool     can_send = false;           // set from CAN_SEND_NOW subevent

static bd_addr_t wii_addr = {0, 0, 0, 0, 0, 0};
static bool      wii_addr_valid = false;

static btstack_packet_callback_registration_t hci_event_cb_reg;

// ============================================================================
// PIN CODE HANDLER — reversed Wii BD_ADDR
// ============================================================================

static void handle_pin_code_request(const uint8_t* packet) {
    if (!wii_addr_valid) {
        printf("[wiimote_sdp] PIN request but no Wii addr stored — declining\n");
        bd_addr_t requester;
        hci_event_pin_code_request_get_bd_addr(packet, requester);
        gap_pin_code_negative(requester);
        return;
    }

    bd_addr_t requester;
    hci_event_pin_code_request_get_bd_addr(packet, requester);

    // Wiimote PIN trick: PIN = reversed BD_ADDR of the Wii (6 raw bytes, NOT
    // ASCII). Some Wiis require the requester's BD_ADDR reversed, others the
    // stored Wii address; we try the stored-Wii form because that's what
    // the Sync-button flow expects.
    uint8_t pin[6];
    for (int i = 0; i < 6; i++) pin[i] = wii_addr[5 - i];

    printf("[wiimote_sdp] PIN request from %02x:%02x:%02x:%02x:%02x:%02x — responding\n",
           requester[0], requester[1], requester[2], requester[3], requester[4], requester[5]);

    gap_pin_code_response_binary(requester, pin, 6);
}

// ============================================================================
// EVENT HANDLER
// ============================================================================

static void packet_handler(uint8_t packet_type, uint16_t channel, uint8_t* packet, uint16_t size) {
    (void)channel;
    (void)size;

    if (packet_type != HCI_EVENT_PACKET) return;

    uint8_t ev = hci_event_packet_get_type(packet);
    switch (ev) {
        case HCI_EVENT_PIN_CODE_REQUEST:
            handle_pin_code_request(packet);
            break;

        case HCI_EVENT_USER_CONFIRMATION_REQUEST:
            // SSP path — Wiimote doesn't use this, but answer to keep things
            // sane if the peer tries anyway.
            {
                bd_addr_t addr;
                hci_event_user_confirmation_request_get_bd_addr(packet, addr);
                gap_ssp_confirmation_response(addr);
            }
            break;

        case HCI_EVENT_HID_META:
            switch (hci_event_hid_meta_get_subevent_code(packet)) {
                case HID_SUBEVENT_CONNECTION_OPENED: {
                    uint8_t status = hid_subevent_connection_opened_get_status(packet);
                    if (status != ERROR_CODE_SUCCESS) {
                        printf("[wiimote_sdp] HID connect failed 0x%02x\n", status);
                        hid_cid = 0;
                        hid_ready = false;
                        return;
                    }
                    hid_cid = hid_subevent_connection_opened_get_hid_cid(packet);
                    hid_ready = true;
                    printf("[wiimote_sdp] HID connection opened cid=0x%04x\n", hid_cid);
                    break;
                }
                case HID_SUBEVENT_CONNECTION_CLOSED:
                    printf("[wiimote_sdp] HID connection closed\n");
                    hid_cid = 0;
                    hid_ready = false;
                    can_send = false;
                    break;
                case HID_SUBEVENT_CAN_SEND_NOW:
                    can_send = true;
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
// PUBLIC API
// ============================================================================

void wiimote_sdp_register(void) {
    // --- GAP / discoverability setup ---
    // 0x002504 = Major: Peripheral, Minor: Joystick + Gamepad
    gap_set_class_of_device(0x002504);
    gap_set_local_name("Nintendo RVL-CNT-01");

    // Legacy pairing only — the Wiimote was designed pre-SSP.
    gap_ssp_set_enable(0);

    gap_discoverable_control(1);
    gap_connectable_control(1);
    gap_set_default_link_policy_settings(
        LM_LINK_POLICY_ENABLE_ROLE_SWITCH |
        LM_LINK_POLICY_ENABLE_SNIFF_MODE);
    gap_set_allow_role_switch(true);

    // --- HID SDP record ---
    memset(hid_service_buffer, 0, sizeof(hid_service_buffer));
    hid_sdp_record_t hid_params = {
        // HID subclass 0x0480 = Major:Peripheral / Minor:Gamepad
        .hid_device_subclass    = 0x0480,
        .hid_country_code       = 0,
        .hid_virtual_cable      = 0,
        .hid_remote_wake        = 1,
        .hid_reconnect_initiate = 1,
        .hid_normally_connectable = true,
        .hid_boot_device        = false,
        .hid_ssr_host_max_latency = 1600,
        .hid_ssr_host_min_timeout = 3200,
        .hid_supervision_timeout  = 3200,
        .hid_descriptor         = wiimote_hid_descriptor,
        .hid_descriptor_size    = WIIMOTE_HID_DESCRIPTOR_SIZE,
        .device_name            = "Nintendo RVL-CNT-01",
    };
    hid_create_sdp_record(hid_service_buffer, sdp_create_service_record_handle(), &hid_params);
    sdp_register_service(hid_service_buffer);

    // --- Bluetooth Device ID record (Nintendo / Wiimote) ---
    // Real Wiimote advertises:
    //   Vendor ID source = Bluetooth SIG (0x0001)
    //   Vendor ID        = 0x057E (Nintendo)
    //   Product ID       = 0x0306 (Wiimote)
    //   Version          = 0x0600
    // Some Wii firmware versions cross-check these against the HID record.
    memset(device_id_buffer, 0, sizeof(device_id_buffer));
    device_id_create_sdp_record(device_id_buffer,
                                sdp_create_service_record_handle(),
                                DEVICE_ID_VENDOR_ID_SOURCE_BLUETOOTH,
                                0x057E,     // Nintendo
                                0x0306,     // Wiimote
                                0x0600);
    sdp_register_service(device_id_buffer);

    // --- HID device + event handlers ---
    hid_device_init(false, WIIMOTE_HID_DESCRIPTOR_SIZE, wiimote_hid_descriptor);
    hid_device_register_packet_handler(packet_handler);

    // --- HCI event handler (for PIN/user-confirmation) ---
    hci_event_cb_reg.callback = &packet_handler;
    hci_add_event_handler(&hci_event_cb_reg);

    printf("[wiimote_sdp] HID + SDP registered (CoD 0x002504, name 'Nintendo RVL-CNT-01')\n");
}

void wiimote_sdp_set_wii_addr(const bd_addr_t addr) {
    if (!addr) return;
    bool all_zero = true;
    for (int i = 0; i < 6; i++) {
        wii_addr[i] = addr[i];
        if (addr[i]) all_zero = false;
    }
    wii_addr_valid = !all_zero;
    if (wii_addr_valid) {
        printf("[wiimote_sdp] Stored Wii addr %02x:%02x:%02x:%02x:%02x:%02x\n",
               wii_addr[0], wii_addr[1], wii_addr[2],
               wii_addr[3], wii_addr[4], wii_addr[5]);
    }
}

bool wiimote_sdp_get_wii_addr(bd_addr_t out) {
    if (!out || !wii_addr_valid) return false;
    memcpy(out, wii_addr, 6);
    return true;
}

uint8_t wiimote_sdp_reconnect(void) {
    if (!wii_addr_valid) return ERROR_CODE_UNSPECIFIED_ERROR;
    if (hid_ready) return ERROR_CODE_SUCCESS;
    uint16_t cid_out = 0;
    uint8_t status = hid_device_connect(wii_addr, &cid_out);
    if (status == ERROR_CODE_SUCCESS) {
        hid_cid = cid_out;
        printf("[wiimote_sdp] outgoing connect requested, cid=0x%04x\n", hid_cid);
    } else {
        printf("[wiimote_sdp] hid_device_connect failed 0x%02x\n", status);
    }
    return status;
}

void wiimote_sdp_disconnect(void) {
    if (!hid_cid) return;
    hid_device_disconnect(hid_cid);
    hid_cid = 0;
    hid_ready = false;
    can_send = false;
}

bool wiimote_sdp_send_report(const uint8_t* report, uint16_t len) {
    if (!hid_ready || !report || len == 0) return false;
    // BTstack HID device requires a two-step: ask for CAN_SEND_NOW, then
    // ship the message from the subevent. For the scaffold we push
    // synchronously if can_send latched true; the event handler clears it
    // after each send on real hardware.
    hid_device_request_can_send_now_event(hid_cid);
    if (!can_send) return false;
    hid_device_send_interrupt_message(hid_cid, report, len);
    can_send = false;
    return true;
}

bool wiimote_sdp_is_connected(void) {
    return hid_ready;
}
