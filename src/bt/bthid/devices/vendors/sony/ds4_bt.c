// ds4_bt.c - Sony DualShock 4 Bluetooth Driver
// Handles DS4 controllers over Bluetooth
//
// Report format reference: https://www.psdevwiki.com/ps4/DS4-BT
// BT reports have 2-byte offset compared to USB (report ID 0x11 vs 0x01)

#include "ds4_bt.h"
#include "bt/bthid/bthid.h"
#include "bt/transport/bt_transport.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// DS4 REPORT STRUCTURE (same as USB, but BT has 2-byte header offset)
// ============================================================================

typedef struct __attribute__((packed)) {
    uint8_t x, y, z, rz;    // Joysticks (0-255, centered at 128)

    struct {
        uint8_t dpad     : 4;   // Hat: 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW, 8=released
        uint8_t square   : 1;
        uint8_t cross    : 1;
        uint8_t circle   : 1;
        uint8_t triangle : 1;
    };

    struct {
        uint8_t l1     : 1;
        uint8_t r1     : 1;
        uint8_t l2     : 1;
        uint8_t r2     : 1;
        uint8_t share  : 1;
        uint8_t option : 1;
        uint8_t l3     : 1;
        uint8_t r3     : 1;
    };

    struct {
        uint8_t ps      : 1;
        uint8_t tpad    : 1;
        uint8_t counter : 6;
    };

    uint8_t l2_trigger;
    uint8_t r2_trigger;

    // Extended data for motion
    uint16_t timestamp;
    uint8_t battery;
    int16_t gyro[3];    // x, y, z
    int16_t accel[3];   // x, y, z
    // Touchpad etc follows but not parsed
} ds4_input_report_t;

// DS4 BT output report (for rumble/LED)
typedef struct __attribute__((packed)) {
    uint8_t report_id;      // 0x11 for BT
    uint8_t flags1;         // 0x80
    uint8_t flags2;         // 0x00
    uint8_t flags3;         // 0xFF (enable rumble+LED)

    uint8_t reserved1[2];

    uint8_t rumble_right;   // High frequency
    uint8_t rumble_left;    // Low frequency

    uint8_t led_red;
    uint8_t led_green;
    uint8_t led_blue;

    uint8_t flash_on;       // LED flash on duration
    uint8_t flash_off;      // LED flash off duration

    uint8_t reserved2[8];

    // Total: 23 bytes for basic output
} ds4_bt_output_report_t;

// ============================================================================
// DRIVER DATA
// ============================================================================

typedef struct {
    input_event_t event;
    bool initialized;
    bool sixaxis_enabled;
    uint8_t player_led;
    uint8_t rumble_left;
    uint8_t rumble_right;
} ds4_bt_data_t;

static ds4_bt_data_t ds4_data[BTHID_MAX_DEVICES];

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static void ds4_set_led(bthid_device_t* device, uint8_t r, uint8_t g, uint8_t b)
{
    ds4_bt_data_t* ds4 = (ds4_bt_data_t*)device->driver_data;
    if (!ds4) return;

    uint8_t buf[23];
    memset(buf, 0, sizeof(buf));

    // BT output format
    buf[0] = 0x11;  // Report ID
    buf[1] = 0x80;  // Flags
    buf[2] = 0x00;
    buf[3] = 0xFF;  // Enable rumble+LED

    buf[6] = ds4->rumble_right;
    buf[7] = ds4->rumble_left;
    buf[8] = r;
    buf[9] = g;
    buf[10] = b;

    bt_send_interrupt(device->conn_index, buf, sizeof(buf));
}

static void ds4_enable_sixaxis(bthid_device_t* device)
{
    // Send GET_REPORT Feature to enable full reports
    // buf[0] = 0x43 (GET_REPORT | Feature), buf[1] = 0x02 (report ID)
    uint8_t buf[2] = {0x43, 0x02};
    bt_send_control(device->conn_index, buf, sizeof(buf));
}

// ============================================================================
// DRIVER IMPLEMENTATION
// ============================================================================

static bool ds4_match(const char* device_name, const uint8_t* class_of_device)
{
    (void)class_of_device;

    if (!device_name) {
        return false;
    }

    // Match known DS4 device names
    if (strstr(device_name, "Wireless Controller") != NULL) {
        return true;
    }
    if (strstr(device_name, "DUALSHOCK 4") != NULL) {
        return true;
    }

    return false;
}

static bool ds4_init(bthid_device_t* device)
{
    printf("[DS4_BT] Init for device: %s\n", device->name);

    // Find free data slot
    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (!ds4_data[i].initialized) {
            init_input_event(&ds4_data[i].event);
            ds4_data[i].initialized = true;
            ds4_data[i].sixaxis_enabled = false;
            ds4_data[i].player_led = 0;
            ds4_data[i].rumble_left = 0;
            ds4_data[i].rumble_right = 0;

            ds4_data[i].event.type = INPUT_TYPE_GAMEPAD;
            ds4_data[i].event.dev_addr = device->conn_index;
            ds4_data[i].event.instance = 0;
            ds4_data[i].event.button_count = 10;

            device->driver_data = &ds4_data[i];

            // Enable sixaxis to get full reports
            ds4_enable_sixaxis(device);

            // Set initial LED color (blue for player 1)
            ds4_set_led(device, 0, 0, 64);

            return true;
        }
    }

    return false;
}

static void ds4_process_report(bthid_device_t* device, const uint8_t* data, uint16_t len)
{
    ds4_bt_data_t* ds4 = (ds4_bt_data_t*)device->driver_data;
    if (!ds4) {
        return;
    }

    // BT reports have different report IDs:
    // 0x01 = Basic report (no motion/touchpad)
    // 0x11 = Full report (with motion/touchpad)

    const uint8_t* report_data = NULL;
    uint16_t report_len = 0;

    if (len >= 1 && data[0] == 0x11 && len >= 12) {
        // Full BT report - skip 3 bytes (report ID + 2 header bytes)
        report_data = data + 3;
        report_len = len - 3;
        ds4->sixaxis_enabled = true;
    } else if (len >= 1 && data[0] == 0x01 && len >= 10) {
        // Basic report - skip 1 byte (report ID)
        report_data = data + 1;
        report_len = len - 1;
    } else {
        // Unknown report format
        return;
    }

    if (report_len < sizeof(ds4_input_report_t)) {
        return;
    }

    const ds4_input_report_t* rpt = (const ds4_input_report_t*)report_data;

    // Parse D-pad (hat format)
    bool dpad_up    = (rpt->dpad == 0 || rpt->dpad == 1 || rpt->dpad == 7);
    bool dpad_right = (rpt->dpad >= 1 && rpt->dpad <= 3);
    bool dpad_down  = (rpt->dpad >= 3 && rpt->dpad <= 5);
    bool dpad_left  = (rpt->dpad >= 5 && rpt->dpad <= 7);

    // Build button state (inverted: 0 = pressed in USBR convention)
    uint32_t buttons = 0x00000000;  // All released (active-high)

    if (dpad_up)       buttons |= JP_BUTTON_DU;
    if (dpad_down)     buttons |= JP_BUTTON_DD;
    if (dpad_left)     buttons |= JP_BUTTON_DL;
    if (dpad_right)    buttons |= JP_BUTTON_DR;
    if (rpt->cross)    buttons |= JP_BUTTON_B1;
    if (rpt->circle)   buttons |= JP_BUTTON_B2;
    if (rpt->square)   buttons |= JP_BUTTON_B3;
    if (rpt->triangle) buttons |= JP_BUTTON_B4;
    if (rpt->l1)       buttons |= JP_BUTTON_L1;
    if (rpt->r1)       buttons |= JP_BUTTON_R1;
    if (rpt->l2)       buttons |= JP_BUTTON_L2;
    if (rpt->r2)       buttons |= JP_BUTTON_R2;
    if (rpt->share)    buttons |= JP_BUTTON_S1;
    if (rpt->option)   buttons |= JP_BUTTON_S2;
    if (rpt->l3)       buttons |= JP_BUTTON_L3;
    if (rpt->r3)       buttons |= JP_BUTTON_R3;
    if (rpt->ps)       buttons |= JP_BUTTON_A1;
    if (rpt->tpad)     buttons |= JP_BUTTON_A2;

    // Update event
    ds4->event.buttons = buttons;

    // Analog sticks (HID convention: 0=up, 255=down)
    ds4->event.analog[ANALOG_X] = rpt->x;
    ds4->event.analog[ANALOG_Y] = rpt->y;
    ds4->event.analog[ANALOG_Z] = rpt->z;
    ds4->event.analog[ANALOG_RX] = rpt->rz;

    // Triggers
    ds4->event.analog[ANALOG_RZ] = rpt->l2_trigger;
    ds4->event.analog[ANALOG_SLIDER] = rpt->r2_trigger;

    // Motion data (DS4 has full 3-axis gyro and accel)
    // Check if we have enough data for motion (full report is 78 bytes)
    if (len >= sizeof(ds4_input_report_t)) {
        ds4->event.has_motion = true;
        ds4->event.accel[0] = rpt->accel[0];
        ds4->event.accel[1] = rpt->accel[1];
        ds4->event.accel[2] = rpt->accel[2];
        ds4->event.gyro[0] = rpt->gyro[0];
        ds4->event.gyro[1] = rpt->gyro[1];
        ds4->event.gyro[2] = rpt->gyro[2];
    } else {
        ds4->event.has_motion = false;
    }

    // Submit to router
    router_submit_input(&ds4->event);
}

static void ds4_task(bthid_device_t* device)
{
    ds4_bt_data_t* ds4 = (ds4_bt_data_t*)device->driver_data;
    if (!ds4) return;

    // Could send periodic LED/rumble updates here if needed
    // For now, handled on state change
}

static void ds4_disconnect(bthid_device_t* device)
{
    printf("[DS4_BT] Disconnect: %s\n", device->name);

    ds4_bt_data_t* ds4 = (ds4_bt_data_t*)device->driver_data;
    if (ds4) {
        // Remove player assignment
        remove_players_by_address(ds4->event.dev_addr, ds4->event.instance);

        init_input_event(&ds4->event);
        ds4->initialized = false;
    }
}

// ============================================================================
// DRIVER STRUCT
// ============================================================================

const bthid_driver_t ds4_bt_driver = {
    .name = "Sony DualShock 4 (BT)",
    .match = ds4_match,
    .init = ds4_init,
    .process_report = ds4_process_report,
    .task = ds4_task,
    .disconnect = ds4_disconnect,
};

void ds4_bt_register(void)
{
    bthid_register_driver(&ds4_bt_driver);
}
