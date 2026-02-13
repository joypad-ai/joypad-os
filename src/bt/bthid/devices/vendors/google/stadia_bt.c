// stadia_bt.c - Google Stadia Controller Bluetooth driver
#include "stadia_bt.h"
#include "bt/bthid/bthid.h"
#include "bt/transport/bt_transport.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include <string.h>
#include <stdio.h>

// Google Stadia Controller IDs
#define GOOGLE_VID      0x18D1
#define STADIA_PID      0x9400

// Input Report 0x03 (10 bytes, may have report ID prepended)
typedef struct __attribute__((packed)) {
    uint8_t dpad;           // 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW, 8=neutral
    uint8_t buttons1;       // A3=0x01, A2=0x02, L2=0x04, R2=0x08, A1=0x10, S2=0x20, S1=0x40, R3=0x80
    uint8_t buttons2;       // L3=0x01, R1=0x02, L1=0x04, B4=0x08, B3=0x10, B2=0x20, B1=0x40
    uint8_t left_x;         // 0-255, center 128
    uint8_t left_y;         // 0-255, center 128
    uint8_t right_x;        // 0-255, center 128
    uint8_t right_y;        // 0-255, center 128
    uint8_t l2_trigger;     // 0-255
    uint8_t r2_trigger;     // 0-255
    uint8_t consumer;       // Volume, play/pause (unused)
} stadia_report_t;

// Button masks for buttons1
#define STADIA_BTN1_A3      0x01  // Assistant/Capture button
#define STADIA_BTN1_A2      0x02  // Google Assistant button
#define STADIA_BTN1_L2      0x04
#define STADIA_BTN1_R2      0x08
#define STADIA_BTN1_A1      0x10  // Stadia button
#define STADIA_BTN1_S2      0x20  // Menu/Start
#define STADIA_BTN1_S1      0x40  // Options/Select
#define STADIA_BTN1_R3      0x80

// Button masks for buttons2
#define STADIA_BTN2_L3      0x01
#define STADIA_BTN2_R1      0x02
#define STADIA_BTN2_L1      0x04
#define STADIA_BTN2_B4      0x08  // Y
#define STADIA_BTN2_B3      0x10  // X
#define STADIA_BTN2_B2      0x20  // B
#define STADIA_BTN2_B1      0x40  // A

// ============================================================================
// DRIVER DATA
// ============================================================================

typedef struct {
    input_event_t event;
    stadia_report_t prev_report;
    bool initialized;
} stadia_bt_data_t;

static stadia_bt_data_t stadia_data[BTHID_MAX_DEVICES];

// ============================================================================
// DRIVER IMPLEMENTATION
// ============================================================================

static bool stadia_match(const char* device_name, const uint8_t* class_of_device,
                         uint16_t vendor_id, uint16_t product_id, bool is_ble)
{
    (void)class_of_device;
    (void)is_ble;

    // Match by VID/PID
    if (vendor_id == GOOGLE_VID && product_id == STADIA_PID) {
        return true;
    }

    // Match by name (BLE advertises as "Stadia...")
    if (device_name && strstr(device_name, "Stadia") != NULL) {
        return true;
    }

    return false;
}

static bool stadia_init(bthid_device_t* device)
{
    printf("[STADIA_BT] Init for device: %s\n", device->name);

    // Find free data slot
    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (!stadia_data[i].initialized) {
            init_input_event(&stadia_data[i].event);
            memset(&stadia_data[i].prev_report, 0, sizeof(stadia_report_t));
            stadia_data[i].prev_report.dpad = 8;  // Neutral
            stadia_data[i].initialized = true;

            stadia_data[i].event.type = INPUT_TYPE_GAMEPAD;
            stadia_data[i].event.dev_addr = device->conn_index;
            stadia_data[i].event.instance = 0;
            stadia_data[i].event.transport = INPUT_TRANSPORT_BT_BLE;

            device->driver_data = &stadia_data[i];
            return true;
        }
    }

    return false;
}

static void stadia_process_report(bthid_device_t* device, const uint8_t* data, uint16_t len)
{
    stadia_bt_data_t* sd = (stadia_bt_data_t*)device->driver_data;
    if (!sd) return;

    // Skip report ID if present (0x03 for input report)
    if (len == sizeof(stadia_report_t) + 1 && data[0] == 0x03) {
        data++;
        len--;
    }

    if (len < sizeof(stadia_report_t)) {
        printf("[STADIA_BT] Report too short: %d bytes\n", len);
        return;
    }

    stadia_report_t report;
    memcpy(&report, data, sizeof(stadia_report_t));

    // Parse D-pad (hat switch)
    bool dpad_up    = (report.dpad == 0 || report.dpad == 1 || report.dpad == 7);
    bool dpad_right = (report.dpad >= 1 && report.dpad <= 3);
    bool dpad_down  = (report.dpad >= 3 && report.dpad <= 5);
    bool dpad_left  = (report.dpad >= 5 && report.dpad <= 7);

    // Map buttons to JP_BUTTON format
    uint32_t buttons = 0;

    // D-pad
    if (dpad_up)    buttons |= JP_BUTTON_DU;
    if (dpad_down)  buttons |= JP_BUTTON_DD;
    if (dpad_left)  buttons |= JP_BUTTON_DL;
    if (dpad_right) buttons |= JP_BUTTON_DR;

    // Face buttons (from buttons2)
    if (report.buttons2 & STADIA_BTN2_B1) buttons |= JP_BUTTON_B1;  // A
    if (report.buttons2 & STADIA_BTN2_B2) buttons |= JP_BUTTON_B2;  // B
    if (report.buttons2 & STADIA_BTN2_B3) buttons |= JP_BUTTON_B3;  // X
    if (report.buttons2 & STADIA_BTN2_B4) buttons |= JP_BUTTON_B4;  // Y

    // Shoulders (from buttons2)
    if (report.buttons2 & STADIA_BTN2_L1) buttons |= JP_BUTTON_L1;
    if (report.buttons2 & STADIA_BTN2_R1) buttons |= JP_BUTTON_R1;

    // Triggers (from buttons1)
    if (report.buttons1 & STADIA_BTN1_L2) buttons |= JP_BUTTON_L2;
    if (report.buttons1 & STADIA_BTN1_R2) buttons |= JP_BUTTON_R2;

    // System buttons (from buttons1)
    if (report.buttons1 & STADIA_BTN1_S1) buttons |= JP_BUTTON_S1;  // Options/Select
    if (report.buttons1 & STADIA_BTN1_S2) buttons |= JP_BUTTON_S2;  // Menu/Start

    // Stick clicks
    if (report.buttons2 & STADIA_BTN2_L3) buttons |= JP_BUTTON_L3;
    if (report.buttons1 & STADIA_BTN1_R3) buttons |= JP_BUTTON_R3;

    // Guide button (Stadia button)
    if (report.buttons1 & STADIA_BTN1_A1) buttons |= JP_BUTTON_A1;

    // Update event
    sd->event.buttons = buttons;
    sd->event.analog[ANALOG_LX] = report.left_x;
    sd->event.analog[ANALOG_LY] = report.left_y;
    sd->event.analog[ANALOG_RX] = report.right_x;
    sd->event.analog[ANALOG_RY] = report.right_y;
    sd->event.analog[ANALOG_L2] = report.l2_trigger;
    sd->event.analog[ANALOG_R2] = report.r2_trigger;

    // Submit to router
    router_submit_input(&sd->event);

    sd->prev_report = report;
}

static void stadia_task(bthid_device_t* device)
{
    (void)device;
    // TODO: Implement rumble output if needed
}

static void stadia_disconnect(bthid_device_t* device)
{
    printf("[STADIA_BT] Disconnect: %s\n", device->name);

    stadia_bt_data_t* sd = (stadia_bt_data_t*)device->driver_data;
    if (sd) {
        // Clear router state first (sends zeroed input report)
        router_device_disconnected(sd->event.dev_addr, sd->event.instance);
        remove_players_by_address(sd->event.dev_addr, sd->event.instance);
        init_input_event(&sd->event);
        sd->initialized = false;
    }
}

// ============================================================================
// DRIVER STRUCT
// ============================================================================

const bthid_driver_t stadia_bt_driver = {
    .name = "Google Stadia BT",
    .match = stadia_match,
    .init = stadia_init,
    .process_report = stadia_process_report,
    .task = stadia_task,
    .disconnect = stadia_disconnect,
};

void stadia_bt_register(void)
{
    bthid_register_driver(&stadia_bt_driver);
}
