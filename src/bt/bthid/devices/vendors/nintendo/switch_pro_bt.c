// switch_pro_bt.c - Nintendo Switch Pro Controller Bluetooth Driver
// Handles Switch Pro and Joy-Con controllers over Bluetooth
//
// Reference: https://github.com/dekuNukem/Nintendo_Switch_Reverse_Engineering

#include "switch_pro_bt.h"
#include "bt/bthid/bthid.h"
#include "bt/transport/bt_transport.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// SWITCH PRO CONSTANTS
// ============================================================================

// Report IDs
#define SWITCH_REPORT_INPUT_STANDARD    0x30    // Standard full input report
#define SWITCH_REPORT_INPUT_SIMPLE      0x3F    // Simple HID mode
#define SWITCH_REPORT_OUTPUT            0x01    // Output report with subcommand
#define SWITCH_REPORT_RUMBLE_ONLY       0x10    // Rumble only (no subcommand)

// Subcommands
#define SWITCH_SUBCMD_SET_INPUT_MODE    0x03
#define SWITCH_SUBCMD_SET_PLAYER_LED    0x30
#define SWITCH_SUBCMD_SET_HOME_LED      0x38
#define SWITCH_SUBCMD_ENABLE_IMU        0x40
#define SWITCH_SUBCMD_ENABLE_VIBRATION  0x48

// Input modes
#define SWITCH_INPUT_MODE_FULL          0x30

// ============================================================================
// SWITCH PRO REPORT STRUCTURE
// ============================================================================

typedef struct __attribute__((packed)) {
    uint8_t report_id;      // 0x30 or 0x3F
    uint8_t timer;          // Increments by 1 per report

    uint8_t battery_conn;   // Battery level + connection info

    // Button byte 1 (right side buttons)
    struct {
        uint8_t y    : 1;
        uint8_t x    : 1;
        uint8_t b    : 1;
        uint8_t a    : 1;
        uint8_t sr_r : 1;
        uint8_t sl_r : 1;
        uint8_t r    : 1;
        uint8_t zr   : 1;
    };

    // Button byte 2 (system buttons)
    struct {
        uint8_t minus  : 1;
        uint8_t plus   : 1;
        uint8_t rstick : 1;
        uint8_t lstick : 1;
        uint8_t home   : 1;
        uint8_t capture: 1;
        uint8_t pad1   : 2;
    };

    // Button byte 3 (left side buttons + dpad)
    struct {
        uint8_t down  : 1;
        uint8_t up    : 1;
        uint8_t right : 1;
        uint8_t left  : 1;
        uint8_t sr_l  : 1;
        uint8_t sl_l  : 1;
        uint8_t l     : 1;
        uint8_t zl    : 1;
    };

    // Analog sticks (12-bit packed, 3 bytes each)
    uint8_t left_stick[3];
    uint8_t right_stick[3];

    // Vibration ack and subcommand data follow...
} switch_input_report_t;

// Simple HID report (0x3F) - used before handshake
typedef struct __attribute__((packed)) {
    uint8_t report_id;      // 0x3F

    struct {
        uint8_t b      : 1;
        uint8_t a      : 1;
        uint8_t y      : 1;
        uint8_t x      : 1;
        uint8_t l      : 1;
        uint8_t r      : 1;
        uint8_t zl     : 1;
        uint8_t zr     : 1;
    };

    struct {
        uint8_t minus  : 1;
        uint8_t plus   : 1;
        uint8_t lstick : 1;
        uint8_t rstick : 1;
        uint8_t home   : 1;
        uint8_t capture: 1;
        uint8_t pad    : 2;
    };

    uint8_t hat;            // D-pad as hat (0-7, 8=center)
    uint8_t lx, ly;         // Left stick (0-255)
    uint8_t rx, ry;         // Right stick (0-255)
} switch_simple_report_t;

// ============================================================================
// DRIVER DATA
// ============================================================================

typedef struct {
    input_event_t event;
    bool initialized;
    bool full_report_mode;
    uint8_t output_seq;     // Sequence counter for output reports
    uint8_t player_led;
} switch_bt_data_t;

static switch_bt_data_t switch_data[BTHID_MAX_DEVICES];

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Unpack 12-bit analog value from 3-byte packed format
static uint16_t unpack_stick_12bit(const uint8_t* data, bool high)
{
    if (high) {
        // High nibble of byte 1 + all of byte 2
        return ((data[1] & 0xF0) >> 4) | (data[2] << 4);
    } else {
        // All of byte 0 + low nibble of byte 1
        return data[0] | ((data[1] & 0x0F) << 8);
    }
}

// Scale 12-bit to 8-bit
static uint8_t scale_12bit_to_8bit(uint16_t val)
{
    if (val == 0) return 1;
    return 1 + ((val * 254) / 4095);
}

static void switch_send_subcommand(bthid_device_t* device, uint8_t subcmd,
                                    const uint8_t* data, uint8_t len)
{
    switch_bt_data_t* sw = (switch_bt_data_t*)device->driver_data;
    if (!sw) return;

    uint8_t buf[50];
    memset(buf, 0, sizeof(buf));

    buf[0] = SWITCH_REPORT_OUTPUT;
    buf[1] = sw->output_seq++ & 0x0F;

    // Neutral rumble data (8 bytes)
    buf[2] = 0x00; buf[3] = 0x01; buf[4] = 0x40; buf[5] = 0x40;
    buf[6] = 0x00; buf[7] = 0x01; buf[8] = 0x40; buf[9] = 0x40;

    buf[10] = subcmd;
    if (data && len > 0 && len < 38) {
        memcpy(&buf[11], data, len);
    }

    bt_send_interrupt(device->conn_index, buf, 11 + len);
}

static void switch_set_player_led(bthid_device_t* device, uint8_t player)
{
    // Player LED patterns: 1=0x01, 2=0x03, 3=0x07, 4=0x0F
    uint8_t pattern = 0;
    if (player >= 1 && player <= 4) {
        pattern = (1 << player) - 1;
    }
    switch_send_subcommand(device, SWITCH_SUBCMD_SET_PLAYER_LED, &pattern, 1);
}

static void switch_enable_full_report_mode(bthid_device_t* device)
{
    uint8_t mode = SWITCH_INPUT_MODE_FULL;
    switch_send_subcommand(device, SWITCH_SUBCMD_SET_INPUT_MODE, &mode, 1);
}

// ============================================================================
// DRIVER IMPLEMENTATION
// ============================================================================

static bool switch_match(const char* device_name, const uint8_t* class_of_device,
                         uint16_t vendor_id, uint16_t product_id)
{
    (void)class_of_device;

    // Match Switch 1 controllers by VID/PID
    // Nintendo VID = 0x057E
    // Switch 1 PIDs: Joy-Con L = 0x2006, Joy-Con R = 0x2007, Pro Controller = 0x2009
    // Do NOT match Switch 2 PIDs (0x2066, 0x2067, 0x2069, 0x2073) - handled by switch2_ble
    if (vendor_id == 0x057E) {
        switch (product_id) {
            case 0x2006:  // Joy-Con L
            case 0x2007:  // Joy-Con R
            case 0x2009:  // Pro Controller
                return true;
        }
        // Don't return true for unknown Nintendo PIDs
        // Let specific drivers handle them
    }

    // Name-based match (fallback for classic BT where VID/PID may be unavailable)
    if (device_name) {
        if (strstr(device_name, "Pro Controller") != NULL) {
            return true;
        }
        if (strstr(device_name, "Joy-Con") != NULL) {
            return true;
        }
    }

    return false;
}

static bool switch_init(bthid_device_t* device)
{
    printf("[SWITCH_BT] Init for device: %s\n", device->name);

    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (!switch_data[i].initialized) {
            init_input_event(&switch_data[i].event);
            switch_data[i].initialized = true;
            switch_data[i].full_report_mode = false;
            switch_data[i].output_seq = 0;
            switch_data[i].player_led = 0;

            switch_data[i].event.type = INPUT_TYPE_GAMEPAD;
            switch_data[i].event.transport = INPUT_TRANSPORT_BT_CLASSIC;
            switch_data[i].event.dev_addr = device->conn_index;
            switch_data[i].event.instance = 0;
            switch_data[i].event.button_count = 10;

            device->driver_data = &switch_data[i];

            // Request full report mode (0x30 reports)
            switch_enable_full_report_mode(device);

            // Set player LED
            switch_set_player_led(device, 1);

            return true;
        }
    }

    return false;
}

static void switch_process_report(bthid_device_t* device, const uint8_t* data, uint16_t len)
{
    switch_bt_data_t* sw = (switch_bt_data_t*)device->driver_data;
    if (!sw || len < 1) return;

    uint8_t report_id = data[0];

    if (report_id == SWITCH_REPORT_INPUT_STANDARD && len >= 13) {
        // Full input report (0x30)
        const switch_input_report_t* rpt = (const switch_input_report_t*)data;

        sw->full_report_mode = true;

        // Build button state
        uint32_t buttons = 0x00000000;

        // Face buttons
        if (rpt->a)      buttons |= JP_BUTTON_B1;
        if (rpt->b)      buttons |= JP_BUTTON_B2;
        if (rpt->x)      buttons |= JP_BUTTON_B3;
        if (rpt->y)      buttons |= JP_BUTTON_B4;

        // Shoulder buttons
        if (rpt->l)      buttons |= JP_BUTTON_L1;
        if (rpt->r)      buttons |= JP_BUTTON_R1;
        if (rpt->zl)     buttons |= JP_BUTTON_L2;
        if (rpt->zr)     buttons |= JP_BUTTON_R2;

        // System buttons
        if (rpt->minus)  buttons |= JP_BUTTON_S1;
        if (rpt->plus)   buttons |= JP_BUTTON_S2;
        if (rpt->lstick) buttons |= JP_BUTTON_L3;
        if (rpt->rstick) buttons |= JP_BUTTON_R3;
        if (rpt->home)   buttons |= JP_BUTTON_A1;

        // D-pad
        if (rpt->up)     buttons |= JP_BUTTON_DU;
        if (rpt->down)   buttons |= JP_BUTTON_DD;
        if (rpt->left)   buttons |= JP_BUTTON_DL;
        if (rpt->right)  buttons |= JP_BUTTON_DR;

        sw->event.buttons = buttons;

        // Unpack 12-bit sticks
        uint16_t lx = unpack_stick_12bit(rpt->left_stick, false);
        uint16_t ly = unpack_stick_12bit(rpt->left_stick, true);
        uint16_t rx = unpack_stick_12bit(rpt->right_stick, false);
        uint16_t ry = unpack_stick_12bit(rpt->right_stick, true);

        // Scale to 8-bit and invert Y (Nintendo: up=high, HID: up=low)
        sw->event.analog[ANALOG_LX] = scale_12bit_to_8bit(lx);
        sw->event.analog[ANALOG_LY] = 255 - scale_12bit_to_8bit(ly);
        sw->event.analog[ANALOG_RX] = scale_12bit_to_8bit(rx);
        sw->event.analog[ANALOG_RY] = 255 - scale_12bit_to_8bit(ry);

        router_submit_input(&sw->event);

    } else if (report_id == SWITCH_REPORT_INPUT_SIMPLE && len >= 12) {
        // Simple HID report (0x3F) - used before full mode enabled
        const switch_simple_report_t* rpt = (const switch_simple_report_t*)data;

        uint32_t buttons = 0x00000000;

        if (rpt->a)      buttons |= JP_BUTTON_B1;
        if (rpt->b)      buttons |= JP_BUTTON_B2;
        if (rpt->x)      buttons |= JP_BUTTON_B3;
        if (rpt->y)      buttons |= JP_BUTTON_B4;
        if (rpt->l)      buttons |= JP_BUTTON_L1;
        if (rpt->r)      buttons |= JP_BUTTON_R1;
        if (rpt->zl)     buttons |= JP_BUTTON_L2;
        if (rpt->zr)     buttons |= JP_BUTTON_R2;
        if (rpt->minus)  buttons |= JP_BUTTON_S1;
        if (rpt->plus)   buttons |= JP_BUTTON_S2;
        if (rpt->lstick) buttons |= JP_BUTTON_L3;
        if (rpt->rstick) buttons |= JP_BUTTON_R3;
        if (rpt->home)   buttons |= JP_BUTTON_A1;

        // Hat to D-pad
        if (rpt->hat == 0 || rpt->hat == 1 || rpt->hat == 7) buttons |= JP_BUTTON_DU;
        if (rpt->hat >= 1 && rpt->hat <= 3) buttons |= JP_BUTTON_DR;
        if (rpt->hat >= 3 && rpt->hat <= 5) buttons |= JP_BUTTON_DD;
        if (rpt->hat >= 5 && rpt->hat <= 7) buttons |= JP_BUTTON_DL;

        sw->event.buttons = buttons;
        sw->event.analog[ANALOG_LX] = rpt->lx;
        sw->event.analog[ANALOG_LY] = 255 - rpt->ly;  // Invert Y (Nintendo: up=high, HID: up=low)
        sw->event.analog[ANALOG_RX] = rpt->rx;
        sw->event.analog[ANALOG_RY] = 255 - rpt->ry; // Invert Y (Nintendo: up=high, HID: up=low)

        router_submit_input(&sw->event);

        // If we're still getting simple reports, request full mode again
        if (!sw->full_report_mode) {
            switch_enable_full_report_mode(device);
        }
    }
}

static void switch_task(bthid_device_t* device)
{
    (void)device;
    // Could send periodic keep-alive or rumble updates here
}

static void switch_disconnect(bthid_device_t* device)
{
    printf("[SWITCH_BT] Disconnect: %s\n", device->name);

    switch_bt_data_t* sw = (switch_bt_data_t*)device->driver_data;
    if (sw) {
        // Clear router state first (sends zeroed input report)
        router_device_disconnected(sw->event.dev_addr, sw->event.instance);
        // Remove player assignment
        remove_players_by_address(sw->event.dev_addr, sw->event.instance);

        init_input_event(&sw->event);
        sw->initialized = false;
    }
}

// ============================================================================
// DRIVER STRUCT
// ============================================================================

const bthid_driver_t switch_pro_bt_driver = {
    .name = "Nintendo Switch Pro (BT)",
    .match = switch_match,
    .init = switch_init,
    .process_report = switch_process_report,
    .task = switch_task,
    .disconnect = switch_disconnect,
};

void switch_pro_bt_register(void)
{
    bthid_register_driver(&switch_pro_bt_driver);
}
