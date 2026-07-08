// sinput_ble.c - BLE SInput controller input driver
// SPDX-License-Identifier: Apache-2.0
//
// Reads a JoypadOS SInput controller over BLE HID (HOGP) and submits its input
// to the router. This is the BLE counterpart to sinput_host.c (USB). Without it
// a SInput controller over BLE (e.g. controller_btusb on the XIAO nRF) is only
// seen as an unhandled "Generic BLE Gamepad" — the generic fallback can't parse
// SInput's custom report ID 1, so it produces no input and looks like a pairing
// failure. Matches by DIS VID/PID (0x2E8A/0x10C6) or advertised name.

#include "sinput_ble.h"
#include "bt/bthid/bthid.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include "usb/usbd/modes/sinput_mode.h"  // sinput_report_t + SINPUT_MASK_* + SINPUT_REPORT_ID_* (no tusb.h)
#include <string.h>
#include <stdio.h>

#define SINPUT_BLE_VID  0x2E8A
#define SINPUT_BLE_PID  0x10C6

typedef struct {
    bool          initialized;
    bool          has_motion;
    input_event_t event;
} sinput_ble_data_t;

static sinput_ble_data_t sinput_data[BTHID_MAX_DEVICES];

static bool sinput_ble_match(const char* device_name, const uint8_t* class_of_device,
                             uint16_t vendor_id, uint16_t product_id, bool is_ble)
{
    (void)class_of_device;
    if (!is_ble) return false;
    // VID/PID (from DIS after connect) is the strongest signal.
    if (vendor_id == SINPUT_BLE_VID && product_id == SINPUT_BLE_PID) return true;
    // Advertised name (available before the DIS read).
    if (device_name && (strstr(device_name, "JoypadOS") || strstr(device_name, "SInput")))
        return true;
    return false;
}

static bool sinput_ble_init(bthid_device_t* device)
{
    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (!sinput_data[i].initialized) {
            init_input_event(&sinput_data[i].event);
            sinput_data[i].event.type      = INPUT_TYPE_GAMEPAD;
            sinput_data[i].event.dev_addr  = device->conn_index;
            sinput_data[i].event.instance  = 0;
            sinput_data[i].event.transport = INPUT_TRANSPORT_BT_BLE;
            sinput_data[i].has_motion      = false;
            sinput_data[i].initialized     = true;
            device->type        = BTHID_DEVICE_GAMEPAD;
            device->driver_data = &sinput_data[i];
            printf("[SINPUT_BLE] Init for %s\n", device->name);
            return true;
        }
    }
    printf("[SINPUT_BLE] No free data slot!\n");
    return false;
}

static void sinput_ble_process_report(bthid_device_t* device, const uint8_t* data, uint16_t len)
{
    sinput_ble_data_t* sd = (sinput_ble_data_t*)device->driver_data;
    if (!sd || len < 1) return;

    uint8_t report_id = data[0];

    // Feature response: capability flags (has_motion = byte 0 bit 0).
    if (report_id == SINPUT_REPORT_ID_FEATURES) {
        if (len >= 2) sd->has_motion = (data[1] & 0x01) != 0;
        return;
    }

    if (report_id != SINPUT_REPORT_ID_INPUT) return;
    if (len < sizeof(sinput_report_t) - 1) return;  // -1: report_id already consumed

    sinput_report_t rpt;
    rpt.report_id = report_id;
    memcpy(&rpt.plug_status, data + 1, sizeof(sinput_report_t) - 1);

    uint32_t sb = rpt.buttons[0] | (rpt.buttons[1] << 8) |
                  (rpt.buttons[2] << 16) | (rpt.buttons[3] << 24);

    uint32_t buttons = 0;
    if (sb & SINPUT_MASK_SOUTH)     buttons |= JP_BUTTON_B1;
    if (sb & SINPUT_MASK_EAST)      buttons |= JP_BUTTON_B2;
    if (sb & SINPUT_MASK_WEST)      buttons |= JP_BUTTON_B3;
    if (sb & SINPUT_MASK_NORTH)     buttons |= JP_BUTTON_B4;
    if (sb & SINPUT_MASK_L1)        buttons |= JP_BUTTON_L1;
    if (sb & SINPUT_MASK_R1)        buttons |= JP_BUTTON_R1;
    if (sb & SINPUT_MASK_L2)        buttons |= JP_BUTTON_L2;
    if (sb & SINPUT_MASK_R2)        buttons |= JP_BUTTON_R2;
    if (sb & SINPUT_MASK_BACK)      buttons |= JP_BUTTON_S1;
    if (sb & SINPUT_MASK_START)     buttons |= JP_BUTTON_S2;
    if (sb & SINPUT_MASK_L3)        buttons |= JP_BUTTON_L3;
    if (sb & SINPUT_MASK_R3)        buttons |= JP_BUTTON_R3;
    if (sb & SINPUT_MASK_DU)        buttons |= JP_BUTTON_DU;
    if (sb & SINPUT_MASK_DD)        buttons |= JP_BUTTON_DD;
    if (sb & SINPUT_MASK_DL)        buttons |= JP_BUTTON_DL;
    if (sb & SINPUT_MASK_DR)        buttons |= JP_BUTTON_DR;
    if (sb & SINPUT_MASK_GUIDE)     buttons |= JP_BUTTON_A1;

    input_event_t* ev = &sd->event;
    ev->buttons = buttons;
    // 16-bit signed sticks (-32768..32767, center 0) → 8-bit (0..255, center 128)
    ev->analog[ANALOG_LX] = (uint8_t)((rpt.lx / 256) + 128);
    ev->analog[ANALOG_LY] = (uint8_t)((rpt.ly / 256) + 128);
    ev->analog[ANALOG_RX] = (uint8_t)((rpt.rx / 256) + 128);
    ev->analog[ANALOG_RY] = (uint8_t)((rpt.ry / 256) + 128);
    // 16-bit triggers (0..32767) → 8-bit (0..255)
    ev->analog[ANALOG_L2] = (uint8_t)(((int32_t)rpt.lt * 255) / 32767);
    ev->analog[ANALOG_R2] = (uint8_t)(((int32_t)rpt.rt * 255) / 32767);

    ev->has_motion = sd->has_motion;
    ev->accel[0] = rpt.accel_x; ev->accel[1] = rpt.accel_y; ev->accel[2] = rpt.accel_z;
    ev->gyro[0]  = rpt.gyro_x;  ev->gyro[1]  = rpt.gyro_y;  ev->gyro[2]  = rpt.gyro_z;
    ev->accel_range = 4000;
    ev->gyro_range  = 2000;

    router_submit_input(ev);
}

static void sinput_ble_disconnect(bthid_device_t* device)
{
    sinput_ble_data_t* sd = (sinput_ble_data_t*)device->driver_data;
    if (!sd) return;
    printf("[SINPUT_BLE] Disconnect: %s\n", device->name);
    router_device_disconnected(sd->event.dev_addr, sd->event.instance);
    remove_players_by_address(sd->event.dev_addr, sd->event.instance);
    init_input_event(&sd->event);
    sd->initialized = false;
}

const bthid_driver_t sinput_ble_driver = {
    .name           = "JoypadOS SInput BLE",
    .match          = sinput_ble_match,
    .init           = sinput_ble_init,
    .process_report = sinput_ble_process_report,
    .task           = NULL,
    .disconnect     = sinput_ble_disconnect,
};

void sinput_ble_register(void)
{
    bthid_register_driver(&sinput_ble_driver);
}
