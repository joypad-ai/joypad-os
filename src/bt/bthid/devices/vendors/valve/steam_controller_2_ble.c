// steam_controller_2_ble.c - Valve Steam Controller 2 ("Triton") BLE driver
//
// Parses the assembled Valve input report (see steam_controller_2_ble.h) into a
// joypad-os input_event_t. The BLE transport specifics (Valve GATT service
// discovery, notification subscription, lizard-mode-off + IMU-enable feature
// reports) are handled in bt/btstack/btstack_host.c; this driver is a pure
// bthid_driver_t that runs once the device is routed to it.
//
// SPDX-License-Identifier: Apache-2.0

#include "steam_controller_2_ble.h"
#include "bt/bthid/bthid.h"
#include "bt/transport/bt_transport.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// BUTTON BITFIELD (u32 at report offset 2) — TritonButtons
// ============================================================================

#define SC2_A                0   // South face
#define SC2_B                1   // East face
#define SC2_X                2   // West face
#define SC2_Y                3   // North face
#define SC2_QAM              4   // Quick-access menu ("…")
#define SC2_R3               5   // Right stick click
#define SC2_VIEW             6   // View (left of Steam)
#define SC2_R4               7   // Right upper grip paddle
#define SC2_R5               8   // Right lower grip paddle
#define SC2_RB               9   // Right bumper
#define SC2_DPAD_DOWN        10
#define SC2_DPAD_RIGHT       11
#define SC2_DPAD_LEFT        12
#define SC2_DPAD_UP          13
#define SC2_MENU             14  // Menu (right of Steam)
#define SC2_L3               15  // Left stick click
#define SC2_STEAM            16  // Steam/Guide
#define SC2_L4               17  // Left upper grip paddle
#define SC2_L5               18  // Left lower grip paddle
#define SC2_LB               19  // Left bumper
#define SC2_RSTICK_TOUCH     20
#define SC2_RPAD_TOUCH       21
#define SC2_RPAD_CLICK       22
#define SC2_RTRIGGER_CLICK   23  // Right trigger full-pull
#define SC2_LSTICK_TOUCH     24
#define SC2_LPAD_TOUCH       25
#define SC2_LPAD_CLICK       26
#define SC2_LTRIGGER_CLICK   27  // Left trigger full-pull
#define SC2_RGRIP_TOUCH      28
#define SC2_LGRIP_TOUCH      29

// Report byte offsets (assembled; byte 0 = report id).
#define SC2_OFF_BUTTONS      2
#define SC2_OFF_LTRIGGER     6
#define SC2_OFF_RTRIGGER     8
#define SC2_OFF_LX           10
#define SC2_OFF_LY           12
#define SC2_OFF_RX           14
#define SC2_OFF_RY           16
#define SC2_OFF_ACCEL        34   // X/Y/Z int16
#define SC2_OFF_GYRO         40   // X/Y/Z int16
#define SC2_MIN_REPORT_LEN   18   // buttons + sticks present
#define SC2_MOTION_REPORT_LEN 46  // full report incl. IMU

// ============================================================================
// HELPERS
// ============================================================================

static inline int16_t rd_s16(const uint8_t* r, int off) {
    return (int16_t)((uint16_t)r[off] | ((uint16_t)r[off + 1] << 8));
}
static inline uint16_t rd_u16(const uint8_t* r, int off) {
    return (uint16_t)r[off] | ((uint16_t)r[off + 1] << 8);
}

// Signed 16-bit stick (-32768..32767) → unsigned 8-bit (0..255, 128 = center).
static inline uint8_t stick_to_u8(int16_t v) {
    int32_t s = ((int32_t)v + 32768) >> 8;   // 0..255
    if (s < 0) s = 0;
    if (s > 255) s = 255;
    return (uint8_t)s;
}

// ============================================================================
// DRIVER DATA
// ============================================================================

typedef struct {
    input_event_t event;   // MUST be first (bthid_set_battery_level casts to it)
    bool initialized;
} sc2_ble_data_t;

static sc2_ble_data_t sc2_data[BTHID_MAX_DEVICES];

// ============================================================================
// DRIVER IMPLEMENTATION
// ============================================================================

static bool sc2_ble_match(const char* device_name, const uint8_t* class_of_device,
                          uint16_t vendor_id, uint16_t product_id, bool is_ble)
{
    (void)class_of_device;
    if (!is_ble) return false;   // BLE-only path (wired SC2 is handled by the USB driver)

    // Primary: synthetic VID/PID assigned by the Valve GATT client.
    if (vendor_id == SC2_BLE_VID && product_id == SC2_BLE_PID) return true;

    // Fallback before VID/PID is set: advertised name begins with "Steam".
    if (device_name && strstr(device_name, "Steam") != NULL) return true;

    return false;
}

static bool sc2_ble_init(bthid_device_t* device)
{
    printf("[SC2_BLE] Init for device: %s\n", device->name);

    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (!sc2_data[i].initialized) {
            init_input_event(&sc2_data[i].event);
            sc2_data[i].initialized = true;

            sc2_data[i].event.type = INPUT_TYPE_GAMEPAD;
            sc2_data[i].event.transport = INPUT_TRANSPORT_BT_BLE;
            sc2_data[i].event.layout = LAYOUT_MODERN_4FACE;
            sc2_data[i].event.dev_addr = device->conn_index;
            sc2_data[i].event.instance = 0;
            sc2_data[i].event.button_count = 15;
            sc2_data[i].event.has_motion = true;
            sc2_data[i].event.gyro_range = 2000;   // ±2000 dps
            sc2_data[i].event.accel_range = 2000;  // ±2 g (milli-g)

            device->driver_data = &sc2_data[i];
            return true;
        }
    }
    return false;
}

static void sc2_ble_process_report(bthid_device_t* device, const uint8_t* data, uint16_t len)
{
    sc2_ble_data_t* sc2 = (sc2_ble_data_t*)device->driver_data;
    if (!sc2) return;

    // data[0] is the report id (0x45 or 0x47), prepended by the GATT client.
    if (len < SC2_MIN_REPORT_LEN) return;
    if (data[0] != SC2_BLE_REPORT_45 && data[0] != SC2_BLE_REPORT_47) return;

    uint32_t raw = rd_u16(data, SC2_OFF_BUTTONS) |
                   ((uint32_t)rd_u16(data, SC2_OFF_BUTTONS + 2) << 16);

    uint32_t buttons = 0;
    // Face buttons (W3C: B1=south, B2=east, B3=west, B4=north)
    if (raw & (1u << SC2_A)) buttons |= JP_BUTTON_B1;
    if (raw & (1u << SC2_B)) buttons |= JP_BUTTON_B2;
    if (raw & (1u << SC2_X)) buttons |= JP_BUTTON_B3;
    if (raw & (1u << SC2_Y)) buttons |= JP_BUTTON_B4;
    // Bumpers
    if (raw & (1u << SC2_LB)) buttons |= JP_BUTTON_L1;
    if (raw & (1u << SC2_RB)) buttons |= JP_BUTTON_R1;
    // Triggers (digital on full-pull click; analog set below)
    if (raw & (1u << SC2_LTRIGGER_CLICK)) buttons |= JP_BUTTON_L2;
    if (raw & (1u << SC2_RTRIGGER_CLICK)) buttons |= JP_BUTTON_R2;
    // Center cluster
    // Menu (☰) = Back/Select, View (⧉) = Start — matches SDL's SC2 mapping
    // (Menu->BACK, View->START).
    if (raw & (1u << SC2_MENU)) buttons |= JP_BUTTON_S1;  // Select/Back
    if (raw & (1u << SC2_VIEW)) buttons |= JP_BUTTON_S2;  // Start
    if (raw & (1u << SC2_STEAM)) buttons |= JP_BUTTON_A1; // Guide/Home
    if (raw & (1u << SC2_QAM)) buttons |= JP_BUTTON_A2;   // Quick-access menu
    // Stick clicks
    if (raw & (1u << SC2_L3)) buttons |= JP_BUTTON_L3;
    if (raw & (1u << SC2_R3)) buttons |= JP_BUTTON_R3;
    // D-pad
    if (raw & (1u << SC2_DPAD_UP))    buttons |= JP_BUTTON_DU;
    if (raw & (1u << SC2_DPAD_DOWN))  buttons |= JP_BUTTON_DD;
    if (raw & (1u << SC2_DPAD_LEFT))  buttons |= JP_BUTTON_DL;
    if (raw & (1u << SC2_DPAD_RIGHT)) buttons |= JP_BUTTON_DR;
    // Grip paddles → SInput's four paddle slots: upper pair to paddle 1
    // (L4/R4), lower pair to paddle 2 (L5/R5).
    if (raw & (1u << SC2_L4)) buttons |= JP_BUTTON_L4;
    if (raw & (1u << SC2_R4)) buttons |= JP_BUTTON_R4;
    if (raw & (1u << SC2_L5)) buttons |= JP_BUTTON_L5;
    if (raw & (1u << SC2_R5)) buttons |= JP_BUTTON_R5;

    // Sticks — Valve sends +Y = up, invert to HID convention (0 = up).
    uint8_t lx = stick_to_u8(rd_s16(data, SC2_OFF_LX));
    uint8_t ly = (uint8_t)(255 - stick_to_u8(rd_s16(data, SC2_OFF_LY)));
    uint8_t rx = stick_to_u8(rd_s16(data, SC2_OFF_RX));
    uint8_t ry = (uint8_t)(255 - stick_to_u8(rd_s16(data, SC2_OFF_RY)));

    // Triggers — 0..32767 → 0..255.
    uint16_t lt_raw = rd_u16(data, SC2_OFF_LTRIGGER);
    uint16_t rt_raw = rd_u16(data, SC2_OFF_RTRIGGER);
    uint8_t l2 = (uint8_t)(lt_raw >> 7);
    uint8_t r2 = (uint8_t)(rt_raw >> 7);

    sc2->event.buttons = buttons;
    sc2->event.analog[ANALOG_LX] = lx;
    sc2->event.analog[ANALOG_LY] = ly;
    sc2->event.analog[ANALOG_RX] = rx;
    sc2->event.analog[ANALOG_RY] = ry;
    sc2->event.analog[ANALOG_L2] = l2;
    sc2->event.analog[ANALOG_R2] = r2;

    // IMU (accel/gyro at fixed offsets for both report types).
    if (len >= SC2_MOTION_REPORT_LEN) {
        sc2->event.has_motion = true;
        sc2->event.accel[0] = rd_s16(data, SC2_OFF_ACCEL);
        sc2->event.accel[1] = rd_s16(data, SC2_OFF_ACCEL + 2);
        sc2->event.accel[2] = rd_s16(data, SC2_OFF_ACCEL + 4);
        sc2->event.gyro[0]  = rd_s16(data, SC2_OFF_GYRO);
        sc2->event.gyro[1]  = rd_s16(data, SC2_OFF_GYRO + 2);
        sc2->event.gyro[2]  = rd_s16(data, SC2_OFF_GYRO + 4);
    }

    router_submit_input(&sc2->event);
}

static void sc2_ble_task(bthid_device_t* device)
{
    (void)device;
    // No per-device task: gamepad-mode keepalive + IMU-enable feature reports
    // are driven from the Valve GATT client (valve_periodic) on the BTstack
    // thread, where GATT writes are safe.
}

static void sc2_ble_disconnect(bthid_device_t* device)
{
    printf("[SC2_BLE] Disconnect: %s\n", device->name);

    sc2_ble_data_t* sc2 = (sc2_ble_data_t*)device->driver_data;
    if (sc2) {
        router_device_disconnected(sc2->event.dev_addr, sc2->event.instance);
        remove_players_by_address(sc2->event.dev_addr, sc2->event.instance);
        init_input_event(&sc2->event);
        sc2->initialized = false;
    }
}

// ============================================================================
// DRIVER STRUCT
// ============================================================================

const bthid_driver_t steam_controller_2_ble_driver = {
    .name = "Valve Steam Controller 2 (BLE)",
    .match = sc2_ble_match,
    .init = sc2_ble_init,
    .process_report = sc2_ble_process_report,
    .task = sc2_ble_task,
    .disconnect = sc2_ble_disconnect,
};

void steam_controller_2_ble_register(void)
{
    bthid_register_driver(&steam_controller_2_ble_driver);
}
