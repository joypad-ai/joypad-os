// ds3_bt.c - Sony DualShock 3 Bluetooth Driver
// Handles DS3 controllers over Bluetooth
//
// DS3 BT connection notes:
// - DS3 doesn't use SSP, uses legacy PIN pairing (we reply with "0000")
// - After connecting, DS3 needs an activation report to enable input
// - Report format is same as USB (report ID 0x01)

#include "ds3_bt.h"
#include "bt/bthid/bthid.h"
#include "bt/transport/bt_transport.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "pico/time.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// DS3 REPORT STRUCTURE (same as USB)
// ============================================================================

typedef struct __attribute__((packed)) {
    uint8_t reserved1;           // byte 0

    // Button bytes
    uint8_t select : 1;          // byte 1
    uint8_t l3     : 1;
    uint8_t r3     : 1;
    uint8_t start  : 1;
    uint8_t up     : 1;
    uint8_t right  : 1;
    uint8_t down   : 1;
    uint8_t left   : 1;

    uint8_t l2     : 1;          // byte 2
    uint8_t r2     : 1;
    uint8_t l1     : 1;
    uint8_t r1     : 1;
    uint8_t triangle : 1;
    uint8_t circle : 1;
    uint8_t cross  : 1;
    uint8_t square : 1;

    uint8_t ps     : 1;          // byte 3
    uint8_t reserved2 : 7;

    uint8_t reserved3;           // byte 4

    // Analog sticks
    uint8_t lx;                  // byte 5
    uint8_t ly;                  // byte 6
    uint8_t rx;                  // byte 7
    uint8_t ry;                  // byte 8

    // Pressure sensors (12 values) - bytes 9-20
    // Order: Up, Right, Down, Left, L2, R2, L1, R1, Triangle, Circle, Cross, Square
    uint8_t pressure[12];
    // pressure[8] = L2 pressure, pressure[9] = R2 pressure

    uint8_t reserved4[27];       // bytes 21-47 (battery, accelerometer etc)
} ds3_bt_input_report_t;         // Total: 48 bytes

// DS3 BT output report (for rumble/LED) - matches USB Host Shield PS3_REPORT_BUFFER
// Total: 50 bytes (2 byte header + 48 byte report)
typedef struct __attribute__((packed)) {
    uint8_t transaction_type;   // 0x52 = SET_REPORT Output
    uint8_t report_id;          // 0x01

    // Padding (bytes 2-11)
    uint8_t padding1;           // byte 2
    uint8_t rumble_right_duration;  // byte 3
    uint8_t rumble_right_force;     // byte 4
    uint8_t rumble_left_duration;   // byte 5
    uint8_t rumble_left_force;      // byte 6
    uint8_t padding2[4];        // bytes 7-10

    uint8_t leds_bitmap;        // byte 11 - LED bit mask (bits 1-4 for LEDs 1-4)

    // LED PWM settings (4 LEDs x 5 bytes each) - bytes 12-31
    struct {
        uint8_t time_enabled;   // 0xFF = always on
        uint8_t duty_length;    // 0x27
        uint8_t enabled;        // 0x10
        uint8_t duty_off;       // 0x00
        uint8_t duty_on;        // 0x32
    } led[4];

    uint8_t padding3[18];       // bytes 32-49 (trailing padding)
} ds3_bt_output_report_t;       // Total: 50 bytes

// Driver instance data
typedef struct {
    bool initialized;
    input_event_t event;
    uint8_t player_led;
    uint8_t activation_state;  // 0=idle, 1=enabled, 2=activated
    uint32_t activation_time;  // Time of last state change
} ds3_bt_data_t;

static ds3_bt_data_t ds3_data[BTHID_MAX_DEVICES] = {0};

// ============================================================================
// DRIVER IMPLEMENTATION
// ============================================================================

static bool ds3_match(const char* device_name, const uint8_t* class_of_device,
                      uint16_t vendor_id, uint16_t product_id, bool is_ble)
{
    (void)is_ble;
    // VID/PID match (highest priority) - Sony vendor ID = 0x054C
    // DS3/Sixaxis = 0x0268
    if (vendor_id == 0x054C && product_id == 0x0268) {
        return true;
    }

    // Match known DS3 device names
    if (device_name && device_name[0] != '\0') {
        if (strstr(device_name, "PLAYSTATION(R)3") != NULL) {
            return true;
        }
        if (strstr(device_name, "Sony PLAYSTATION") != NULL) {
            return true;
        }
        if (strstr(device_name, "SIXAXIS") != NULL) {
            return true;
        }
    }

    // DS3 often connects without a name (incoming connection)
    // Match by COD: 0x000508 = Peripheral/Gamepad with no services
    // This is relatively unique to DS3 - most modern gamepads have service bits set
    if (class_of_device) {
        uint32_t cod = class_of_device[0] | (class_of_device[1] << 8) | (class_of_device[2] << 16);
        // COD 0x000508 = DS3 (Peripheral, Gamepad, no services)
        // Note: This may also match some other legacy gamepads
        if (cod == 0x000508 && (!device_name || device_name[0] == '\0')) {
            printf("[DS3_BT] Matched by COD 0x%06X (no name)\n", (unsigned)cod);
            return true;
        }
    }

    return false;
}

static bool ds3_init(bthid_device_t* device)
{
    printf("[DS3_BT] Init for device: %s\n", device->name);

    // Find free data slot
    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (!ds3_data[i].initialized) {
            init_input_event(&ds3_data[i].event);
            ds3_data[i].initialized = true;
            ds3_data[i].activation_state = 0;
            ds3_data[i].activation_time = 0;
            ds3_data[i].player_led = 0;

            ds3_data[i].event.type = INPUT_TYPE_GAMEPAD;
            ds3_data[i].event.transport = INPUT_TRANSPORT_BT_CLASSIC;
            ds3_data[i].event.dev_addr = device->conn_index;
            ds3_data[i].event.instance = 0;
            ds3_data[i].event.button_count = 10;

            device->driver_data = &ds3_data[i];
            printf("[DS3_BT] Init complete, slot %d, driver_data=%p\n", i, device->driver_data);

            // DS3 needs activation report via SET_REPORT on control channel
            // We'll send this in the first task call

            return true;
        }
    }

    printf("[DS3_BT] Init FAILED - no free slots\n");
    return false;
}

// Send LED/rumble output report to DS3
static void ds3_send_output(bthid_device_t* device, uint8_t leds, uint8_t rumble_left, uint8_t rumble_right)
{
    ds3_bt_output_report_t report = {0};

    report.transaction_type = 0x52;  // SET_REPORT | Output
    report.report_id = 0x01;

    // Rumble - DS3 has weak (right) and strong (left) motors
    if (rumble_right) {
        report.rumble_right_duration = 0xFE;
        report.rumble_right_force = rumble_right;
    }
    if (rumble_left) {
        report.rumble_left_duration = 0xFE;
        report.rumble_left_force = rumble_left;
    }

    // LEDs (bits 1-4)
    report.leds_bitmap = leds;

    // LED PWM settings for constant on (matches PS3_REPORT_BUFFER)
    for (int i = 0; i < 4; i++) {
        report.led[i].time_enabled = 0xFF;
        report.led[i].duty_length = 0x27;
        report.led[i].enabled = 0x10;
        report.led[i].duty_off = 0x00;
        report.led[i].duty_on = 0x32;
    }

    printf("[DS3_BT] Sending output report (%d bytes) to conn %d\n", (int)sizeof(report), device->conn_index);

    // Send via control channel
    bt_send_control(device->conn_index, (uint8_t*)&report, sizeof(report));
}

static bool ds3_report_debug_done = false;

static void ds3_process_report(bthid_device_t* device, const uint8_t* data, uint16_t len)
{
    ds3_bt_data_t* ds3 = (ds3_bt_data_t*)device->driver_data;
    if (!ds3) return;

    // BT HID interrupt channel: first byte is report ID (no transaction type header)
    // 50 bytes total: 1 byte report ID + 49 bytes report data
    if (len < 1) return;

    uint8_t report_id = data[0];

    // Report ID 0x01 is the main input report
    if (report_id != 0x01) {
        return;
    }

    // Skip report ID
    data += 1;
    len -= 1;

    if (len < sizeof(ds3_bt_input_report_t)) {
        if (!ds3_report_debug_done) {
            printf("[DS3_BT] Report too small: %d < %d\n", len, (int)sizeof(ds3_bt_input_report_t));
            ds3_report_debug_done = true;
        }
        return;
    }

    const ds3_bt_input_report_t* rpt = (const ds3_bt_input_report_t*)data;

    if (!ds3_report_debug_done) {
        printf("[DS3_BT] Processing report, buttons: %02X %02X\n",
               ((uint8_t*)rpt)[1], ((uint8_t*)rpt)[2]);
        ds3_report_debug_done = true;
    }

    // Build button state
    uint32_t buttons = 0;
    if (rpt->up)       buttons |= JP_BUTTON_DU;
    if (rpt->down)     buttons |= JP_BUTTON_DD;
    if (rpt->left)     buttons |= JP_BUTTON_DL;
    if (rpt->right)    buttons |= JP_BUTTON_DR;
    if (rpt->cross)    buttons |= JP_BUTTON_B1;
    if (rpt->circle)   buttons |= JP_BUTTON_B2;
    if (rpt->square)   buttons |= JP_BUTTON_B3;
    if (rpt->triangle) buttons |= JP_BUTTON_B4;
    if (rpt->l1)       buttons |= JP_BUTTON_L1;
    if (rpt->r1)       buttons |= JP_BUTTON_R1;
    if (rpt->l2)       buttons |= JP_BUTTON_L2;
    if (rpt->r2)       buttons |= JP_BUTTON_R2;
    if (rpt->select)   buttons |= JP_BUTTON_S1;
    if (rpt->start)    buttons |= JP_BUTTON_S2;
    if (rpt->l3)       buttons |= JP_BUTTON_L3;
    if (rpt->r3)       buttons |= JP_BUTTON_R3;
    if (rpt->ps)       buttons |= JP_BUTTON_A1;

    // Analog sticks (HID convention: 0=up, 255=down)
    uint8_t lx = rpt->lx;
    uint8_t ly = rpt->ly;
    uint8_t rx = rpt->rx;
    uint8_t ry = rpt->ry;

    // Use pressure sensors for analog triggers
    uint8_t lt = rpt->pressure[8];  // L2 pressure
    uint8_t rt = rpt->pressure[9];  // R2 pressure

    // Ensure non-zero for centered sticks
    if (lx == 0) lx = 1;
    if (ly == 0) ly = 1;
    if (rx == 0) rx = 1;
    if (ry == 0) ry = 1;

    // Parse motion data (SIXAXIS)
    // Motion at bytes 40-47 of the report data (after report ID stripped)
    int16_t accel_x = 0, accel_y = 0, accel_z = 0, gyro_z = 0;
    bool has_motion = false;
    if (len >= 48) {
        // DS3 accelerometer: big-endian 16-bit values centered at ~512
        // DS3 gyro: 10-bit centered at ~512, range ±100 dps
        int16_t raw_accel_x = (int16_t)((data[40] << 8) | data[41]);
        int16_t raw_accel_y = (int16_t)((data[42] << 8) | data[43]);
        int16_t raw_accel_z = (int16_t)((data[44] << 8) | data[45]);
        int16_t raw_gyro_z  = (int16_t)((data[46] << 8) | data[47]);

        // Normalize gyro to SInput convention: ±32767 = ±2000 dps
        // DS3 gyro: centered at 512, ±512 = ±100 dps
        // Conversion: normalized = (raw - 512) * 32767 / 10240
        // This maps DS3's ±100 dps to ±1638 in SInput units (since 100/2000 * 32767 ≈ 1638)
        gyro_z = (int16_t)(((int32_t)(raw_gyro_z - 512) * 32767) / 10240);

        // Normalize accel to SInput convention: ±32767 = ±4g
        // DS3 accel: centered at ~512, ±512 = ±2g
        // Conversion: normalized = (raw - 512) * 32767 / 1024
        // This maps DS3's ±2g to ±16384 in SInput units (since 2/4 * 32767 ≈ 16384)
        accel_x = (int16_t)(((int32_t)(raw_accel_x - 512) * 32767) / 1024);
        accel_y = (int16_t)(((int32_t)(raw_accel_y - 512) * 32767) / 1024);
        accel_z = (int16_t)(((int32_t)(raw_accel_z - 512) * 32767) / 1024);

        has_motion = true;
    }

    // Update event
    ds3->event.buttons = buttons;
    ds3->event.analog[ANALOG_LX] = lx;
    ds3->event.analog[ANALOG_LY] = ly;
    ds3->event.analog[ANALOG_RX] = rx;
    ds3->event.analog[ANALOG_RY] = ry;
    ds3->event.analog[ANALOG_L2] = lt;
    ds3->event.analog[ANALOG_R2] = rt;

    // Motion data
    ds3->event.has_motion = has_motion;
    ds3->event.accel[0] = accel_x;
    ds3->event.accel[1] = accel_y;
    ds3->event.accel[2] = accel_z;
    ds3->event.gyro[0] = 0;  // DS3 only has Z-axis gyro
    ds3->event.gyro[1] = 0;
    ds3->event.gyro[2] = gyro_z;
    ds3->event.gyro_range = 100;   // DS3 gyro is ±100 dps
    ds3->event.accel_range = 2000; // DS3 accel is ±2g (2000 milli-g)

    // Pressure data (same layout as USB: first 4 bytes are reserved/junk)
    ds3->event.has_pressure = true;
    ds3->event.pressure[0] = rpt->pressure[4];   // up
    ds3->event.pressure[1] = rpt->pressure[5];   // right
    ds3->event.pressure[2] = rpt->pressure[6];   // down
    ds3->event.pressure[3] = rpt->pressure[7];   // left
    ds3->event.pressure[4] = rpt->pressure[8];   // L2
    ds3->event.pressure[5] = rpt->pressure[9];   // R2
    ds3->event.pressure[6] = rpt->pressure[10];  // L1
    ds3->event.pressure[7] = rpt->pressure[11];  // R1
    // Face buttons are in reserved4 (same layout as USB unused[])
    ds3->event.pressure[8] = rpt->reserved4[0];  // triangle
    ds3->event.pressure[9] = rpt->reserved4[1];  // circle
    ds3->event.pressure[10] = rpt->reserved4[2]; // cross
    ds3->event.pressure[11] = rpt->reserved4[3]; // square

    router_submit_input(&ds3->event);
}

static void ds3_disconnect(bthid_device_t* device)
{
    printf("[DS3_BT] Disconnect: %s\n", device->name);

    ds3_bt_data_t* ds3 = (ds3_bt_data_t*)device->driver_data;
    if (ds3) {
        // Clear router state first (sends zeroed input report)
        router_device_disconnected(ds3->event.dev_addr, ds3->event.instance);
        // Remove player assignment
        remove_players_by_address(ds3->event.dev_addr, ds3->event.instance);

        // Reset state
        init_input_event(&ds3->event);
        ds3->initialized = false;
        ds3->activation_state = 0;
        ds3->player_led = 0;
        device->driver_data = NULL;
    }
}

static bool ds3_task_debug_done = false;

// Send the enable_sixaxis command to activate input reporting
static void ds3_enable_sixaxis(bthid_device_t* device)
{
    // DS3 requires a specific Feature report to enable input
    // 0x53 = SET_REPORT | Feature (0x50 | 0x03)
    // 0xF4 = Report ID
    // 0x42 0x03 0x00 0x00 = PS3 enable bytes
    static const uint8_t enable_cmd[] = {
        0x53,  // SET_REPORT | Feature
        0xF4,  // Report ID
        0x42, 0x03, 0x00, 0x00  // Enable bytes
    };

    bt_send_control(device->conn_index, enable_cmd, sizeof(enable_cmd));
}

static void ds3_task(bthid_device_t* device)
{
    if (!ds3_task_debug_done) {
        printf("[DS3_BT] Task called, driver_data=%p\n", device->driver_data);
        ds3_task_debug_done = true;
    }

    ds3_bt_data_t* ds3 = (ds3_bt_data_t*)device->driver_data;
    if (!ds3) return;

    uint32_t now = to_ms_since_boot(get_absolute_time());

    // State machine for activation with delays
    switch (ds3->activation_state) {
        case 0:  // Send enable_sixaxis
            printf("[DS3_BT] Sending enable_sixaxis command\n");
            ds3_enable_sixaxis(device);
            ds3->activation_state = 1;
            ds3->activation_time = now;
            break;

        case 1:  // Wait 150ms then send LED
            if (now - ds3->activation_time >= 150) {
                printf("[DS3_BT] Sending LED command\n");
                ds3_send_output(device, 0x02, 0, 0);  // LED 1 = bit 1
                ds3->player_led = 0x02;
                ds3->activation_state = 2;
            }
            break;

        case 2:  // Activated - monitor player LED and rumble from feedback system
            {
                int player_idx = find_player_index(ds3->event.dev_addr, ds3->event.instance);
                if (player_idx >= 0) {
                    feedback_state_t* fb = feedback_get_state(player_idx);
                    bool need_update = false;

                    // Check LED from feedback system
                    // Feedback pattern: bits 0-3 for players 1-4 (0x01, 0x02, 0x04, 0x08)
                    // DS3 LED bitmap: bits 1-4 for LEDs 1-4 (0x02, 0x04, 0x08, 0x10)
                    // Conversion: shift left by 1
                    uint8_t led;
                    if (fb->led.pattern != 0) {
                        // Use LED from host/feedback system
                        led = fb->led.pattern << 1;
                    } else {
                        // Default to player index-based LED
                        led = PLAYER_LEDS[player_idx + 1] << 1;
                    }
                    if (fb->led_dirty || led != ds3->player_led) {
                        ds3->player_led = led;
                        need_update = true;
                    }

                    // Check rumble
                    if (fb->rumble_dirty) {
                        need_update = true;
                    }

                    if (need_update) {
                        ds3_send_output(device, ds3->player_led, fb->rumble.left, fb->rumble.right);
                        feedback_clear_dirty(player_idx);
                    }
                }
            }
            break;
    }
}

// Driver struct
const bthid_driver_t ds3_bt_driver = {
    .name = "Sony DualShock 3 (BT)",
    .match = ds3_match,
    .init = ds3_init,
    .process_report = ds3_process_report,
    .disconnect = ds3_disconnect,
    .task = ds3_task,
};

void ds3_bt_register(void)
{
    bthid_register_driver(&ds3_bt_driver);
}
