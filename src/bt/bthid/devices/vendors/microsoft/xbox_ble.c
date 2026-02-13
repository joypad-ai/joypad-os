// xbox_ble.c - Xbox BLE Controller Driver
// Handles Xbox Series X/S controllers over Bluetooth Low Energy (HID over GATT)
//
// Xbox BLE HID reports are 16 bytes with NO report_id prefix:
// Bytes: 0-1:lx, 2-3:ly, 4-5:rx, 6-7:ry, 8-9:lt, 10-11:rt, 12:hat, 13-14:buttons, 15:pad

#include "xbox_ble.h"
#include "bt/bthid/bthid.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// XBOX BLE CONSTANTS
// ============================================================================

// Xbox BLE controller button masks (verified from testing)
#define XBOX_BLE_A               0x0001
#define XBOX_BLE_B               0x0002
#define XBOX_BLE_X               0x0008
#define XBOX_BLE_Y               0x0010
#define XBOX_BLE_LEFT_SHOULDER   0x0040  // LB
#define XBOX_BLE_RIGHT_SHOULDER  0x0080  // RB
#define XBOX_BLE_BACK            0x0400  // View button
#define XBOX_BLE_START           0x0800  // Menu button
#define XBOX_BLE_GUIDE           0x1000  // Xbox button
#define XBOX_BLE_LEFT_THUMB      0x2000  // L3
#define XBOX_BLE_RIGHT_THUMB     0x4000  // R3

// ============================================================================
// DRIVER DATA
// ============================================================================

typedef struct {
    input_event_t event;
    bool initialized;
} xbox_ble_data_t;

static xbox_ble_data_t xbox_data[BTHID_MAX_DEVICES];

// ============================================================================
// DRIVER IMPLEMENTATION
// ============================================================================

static bool xbox_ble_match(const char* device_name, const uint8_t* class_of_device,
                           uint16_t vendor_id, uint16_t product_id, bool is_ble)
{
    (void)class_of_device;
    (void)vendor_id;
    (void)product_id;

    // Only match BLE connections — Classic BT Xbox controllers use xbox_bt driver
    if (!is_ble) {
        return false;
    }

    if (!device_name) {
        return false;
    }

    // Match known Xbox BLE controller names
    if (strstr(device_name, "Xbox Wireless Controller") != NULL) {
        return true;
    }
    if (strstr(device_name, "Xbox Elite") != NULL) {
        return true;
    }
    if (strstr(device_name, "Xbox Adaptive") != NULL) {
        return true;
    }

    return false;
}

static bool xbox_ble_init(bthid_device_t* device)
{
    printf("[XBOX_BLE] Init for device: %s\n", device->name);

    // Find free data slot
    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (!xbox_data[i].initialized) {
            init_input_event(&xbox_data[i].event);
            xbox_data[i].initialized = true;

            xbox_data[i].event.type = INPUT_TYPE_GAMEPAD;
            xbox_data[i].event.transport = INPUT_TRANSPORT_BT_BLE;
            xbox_data[i].event.dev_addr = device->conn_index;
            xbox_data[i].event.instance = 0;
            xbox_data[i].event.button_count = 10;

            device->driver_data = &xbox_data[i];

            return true;
        }
    }

    return false;
}

static void xbox_ble_process_report(bthid_device_t* device, const uint8_t* data, uint16_t len)
{
    xbox_ble_data_t* xbox = (xbox_ble_data_t*)device->driver_data;
    if (!xbox) return;

    // Xbox BLE reports are 16 bytes, NO report_id prefix
    // But bthid layer adds 0xA1 header, so we get 17 bytes with data starting at [1]
    // OR we might get raw 16 bytes depending on path

    const uint8_t* report;
    uint16_t report_len;

    if (len >= 17 && data[0] == 0xA1) {
        // Has DATA|INPUT header from bthid layer
        report = data + 1;
        report_len = len - 1;
    } else if (len >= 16) {
        // Raw report (direct from BLE notification)
        report = data;
        report_len = len;
    } else {
        return;  // Too short
    }

    if (report_len < 16) return;

    // Parse bytes directly - Xbox BLE report layout:
    // 0-1:lx, 2-3:ly, 4-5:rx, 6-7:ry, 8-9:lt, 10-11:rt, 12:hat, 13-14:buttons
    // Sticks are UNSIGNED 0-65535 (0=left/up, 32768=center, 65535=right/down)
    uint16_t raw_lx = (uint16_t)(report[0] | (report[1] << 8));
    uint16_t raw_ly = (uint16_t)(report[2] | (report[3] << 8));
    uint16_t raw_rx = (uint16_t)(report[4] | (report[5] << 8));
    uint16_t raw_ry = (uint16_t)(report[6] | (report[7] << 8));
    uint16_t raw_lt = (uint16_t)(report[8] | (report[9] << 8));
    uint16_t raw_rt = (uint16_t)(report[10] | (report[11] << 8));
    uint8_t hat = report[12];
    uint16_t btn = (uint16_t)(report[13] | (report[14] << 8));

    // Scale sticks from uint16 (0-65535) to uint8 (0-255)
    uint8_t lx = raw_lx >> 8;
    uint8_t ly = raw_ly >> 8;
    uint8_t rx = raw_rx >> 8;
    uint8_t ry = raw_ry >> 8;
    // Triggers are 10-bit (0-1023), scale to 8-bit
    uint8_t lt = raw_lt >> 2;
    uint8_t rt = raw_rt >> 2;

    uint32_t buttons = 0;

    // D-pad from hat: 0=center, 1=N, 2=NE, 3=E, 4=SE, 5=S, 6=SW, 7=W, 8=NW
    if (hat == 1 || hat == 2 || hat == 8) buttons |= JP_BUTTON_DU;
    if (hat >= 2 && hat <= 4)             buttons |= JP_BUTTON_DR;
    if (hat >= 4 && hat <= 6)             buttons |= JP_BUTTON_DD;
    if (hat >= 6 && hat <= 8)             buttons |= JP_BUTTON_DL;

    // Face buttons and others
    if (btn & XBOX_BLE_A)              buttons |= JP_BUTTON_B1;
    if (btn & XBOX_BLE_B)              buttons |= JP_BUTTON_B2;
    if (btn & XBOX_BLE_X)              buttons |= JP_BUTTON_B3;
    if (btn & XBOX_BLE_Y)              buttons |= JP_BUTTON_B4;
    if (btn & XBOX_BLE_LEFT_SHOULDER)  buttons |= JP_BUTTON_L1;
    if (btn & XBOX_BLE_RIGHT_SHOULDER) buttons |= JP_BUTTON_R1;
    if (lt > 10)                       buttons |= JP_BUTTON_L2;
    if (rt > 10)                       buttons |= JP_BUTTON_R2;
    if (btn & XBOX_BLE_BACK)           buttons |= JP_BUTTON_S1;
    if (btn & XBOX_BLE_START)          buttons |= JP_BUTTON_S2;
    if (btn & XBOX_BLE_LEFT_THUMB)     buttons |= JP_BUTTON_L3;
    if (btn & XBOX_BLE_RIGHT_THUMB)    buttons |= JP_BUTTON_R3;
    if (btn & XBOX_BLE_GUIDE)          buttons |= JP_BUTTON_A1;

    // Fill event struct
    xbox->event.buttons = buttons;
    xbox->event.analog[ANALOG_LX] = lx;
    xbox->event.analog[ANALOG_LY] = ly;
    xbox->event.analog[ANALOG_RX] = rx;
    xbox->event.analog[ANALOG_RY] = ry;
    xbox->event.analog[ANALOG_L2] = lt;
    xbox->event.analog[ANALOG_R2] = rt;

    // Submit to router
    router_submit_input(&xbox->event);
}

static void xbox_ble_task(bthid_device_t* device)
{
    (void)device;
    // Xbox BLE controllers don't need periodic maintenance
    // TODO: Implement rumble via GATT write when needed
}

static void xbox_ble_disconnect(bthid_device_t* device)
{
    printf("[XBOX_BLE] Disconnect: %s\n", device->name);

    xbox_ble_data_t* xbox = (xbox_ble_data_t*)device->driver_data;
    if (xbox) {
        // Clear router state first (sends zeroed input report)
        router_device_disconnected(xbox->event.dev_addr, xbox->event.instance);
        // Remove player assignment
        remove_players_by_address(xbox->event.dev_addr, xbox->event.instance);

        init_input_event(&xbox->event);
        xbox->initialized = false;
    }
}

// ============================================================================
// DRIVER STRUCT
// ============================================================================

const bthid_driver_t xbox_ble_driver = {
    .name = "Xbox Wireless Controller (BLE)",
    .match = xbox_ble_match,
    .init = xbox_ble_init,
    .process_report = xbox_ble_process_report,
    .task = xbox_ble_task,
    .disconnect = xbox_ble_disconnect,
};

void xbox_ble_register(void)
{
    bthid_register_driver(&xbox_ble_driver);
}
