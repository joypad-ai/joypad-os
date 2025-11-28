// ds5_bt.c - Sony DualSense Bluetooth Driver
// Handles PS5 DualSense controllers over Bluetooth
//
// Reference: https://controllers.fandom.com/wiki/Sony_DualSense
// BT reports have similar structure to USB but with different report IDs

#include "ds5_bt.h"
#include "bt/bthid/bthid.h"
#include "bt/transport/bt_transport.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "core/buttons.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// DS5 CONSTANTS
// ============================================================================

// Report IDs
#define DS5_REPORT_BT_INPUT     0x31    // Full BT input report
#define DS5_REPORT_USB_INPUT    0x01    // USB input report (fallback)
#define DS5_REPORT_BT_OUTPUT    0x31    // BT output report

// ============================================================================
// DS5 REPORT STRUCTURE
// ============================================================================

typedef struct __attribute__((packed)) {
    uint8_t x1, y1;         // Left stick
    uint8_t x2, y2;         // Right stick
    uint8_t l2_trigger;     // L2 analog
    uint8_t r2_trigger;     // R2 analog
    uint8_t counter;        // Report counter

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
        uint8_t create : 1;     // Share/Create button
        uint8_t option : 1;
        uint8_t l3     : 1;
        uint8_t r3     : 1;
    };

    struct {
        uint8_t ps      : 1;    // PlayStation button
        uint8_t tpad    : 1;    // Touchpad click
        uint8_t mute    : 1;    // Mute button
        uint8_t pad     : 5;
    };

    // More fields follow (gyro, accel, touchpad) but we don't need them
} ds5_input_report_t;

// DS5 BT output report for LED/rumble
typedef struct __attribute__((packed)) {
    uint8_t report_id;          // 0x31
    uint8_t seq_tag;            // Sequence tag (upper nibble)
    uint8_t tag;                // 0x10 for BT

    uint8_t valid_flag0;        // Feature flags
    uint8_t valid_flag1;
    uint8_t valid_flag2;

    uint8_t rumble_right;       // High frequency motor
    uint8_t rumble_left;        // Low frequency motor

    uint8_t headphone_volume;
    uint8_t speaker_volume;
    uint8_t mic_volume;

    uint8_t audio_flags;
    uint8_t mute_flags;

    uint8_t trigger_r[11];      // Right trigger haptics
    uint8_t trigger_l[11];      // Left trigger haptics

    uint8_t reserved1[6];

    uint8_t valid_flag3;

    uint8_t reserved2[2];

    uint8_t lightbar_setup;     // LED setup flag
    uint8_t led_brightness;
    uint8_t player_led;         // Player indicator LEDs

    uint8_t lightbar_r;
    uint8_t lightbar_g;
    uint8_t lightbar_b;
} ds5_bt_output_report_t;

// ============================================================================
// DRIVER DATA
// ============================================================================

typedef struct {
    input_event_t event;
    bool initialized;
    uint8_t output_seq;
    uint8_t player_led;
    uint8_t rumble_left;
    uint8_t rumble_right;
} ds5_bt_data_t;

static ds5_bt_data_t ds5_data[BTHID_MAX_DEVICES];

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static void ds5_set_led(bthid_device_t* device, uint8_t r, uint8_t g, uint8_t b, uint8_t player)
{
    ds5_bt_data_t* ds5 = (ds5_bt_data_t*)device->driver_data;
    if (!ds5) return;

    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));

    ds5_bt_output_report_t* out = (ds5_bt_output_report_t*)buf;

    out->report_id = DS5_REPORT_BT_OUTPUT;
    out->seq_tag = (ds5->output_seq++ << 4) | 0x00;
    out->tag = 0x10;

    // Enable rumble and LED
    out->valid_flag0 = 0x03;    // Rumble motors
    out->valid_flag1 = 0x04 | 0x08 | 0x10;  // Player LED, lightbar, LED setup
    out->valid_flag2 = 0x00;

    out->rumble_right = ds5->rumble_right;
    out->rumble_left = ds5->rumble_left;

    out->lightbar_setup = 0x02;  // Enable LED changes
    out->led_brightness = 0x02;  // Full brightness

    // Player LED pattern (5 LEDs)
    out->player_led = player;

    out->lightbar_r = r;
    out->lightbar_g = g;
    out->lightbar_b = b;

    bt_send_interrupt(device->conn_index, buf, sizeof(ds5_bt_output_report_t));
}

// ============================================================================
// DRIVER IMPLEMENTATION
// ============================================================================

static bool ds5_match(const char* device_name, const uint8_t* class_of_device)
{
    (void)class_of_device;

    if (!device_name) {
        return false;
    }

    // Match known DualSense device names
    if (strstr(device_name, "DualSense") != NULL) {
        return true;
    }
    // Some controllers might report different names
    if (strstr(device_name, "PS5 Controller") != NULL) {
        return true;
    }

    return false;
}

static bool ds5_init(bthid_device_t* device)
{
    printf("[DS5_BT] Init for device: %s\n", device->name);

    // Find free data slot
    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (!ds5_data[i].initialized) {
            init_input_event(&ds5_data[i].event);
            ds5_data[i].initialized = true;
            ds5_data[i].output_seq = 0;
            ds5_data[i].player_led = 0x04;  // Center LED for player 1
            ds5_data[i].rumble_left = 0;
            ds5_data[i].rumble_right = 0;

            ds5_data[i].event.type = INPUT_TYPE_GAMEPAD;
            ds5_data[i].event.dev_addr = device->conn_index;
            ds5_data[i].event.instance = 0;
            ds5_data[i].event.button_count = 10;

            device->driver_data = &ds5_data[i];

            // Set initial LED color (blue for player 1)
            ds5_set_led(device, 0, 0, 64, 0x04);

            return true;
        }
    }

    return false;
}

static void ds5_process_report(bthid_device_t* device, const uint8_t* data, uint16_t len)
{
    ds5_bt_data_t* ds5 = (ds5_bt_data_t*)device->driver_data;
    if (!ds5 || len < 1) return;

    uint8_t report_id = data[0];
    const uint8_t* report_data = NULL;
    uint16_t report_len = 0;

    if (report_id == DS5_REPORT_BT_INPUT && len >= 12) {
        // Full BT report: report_id (1) + header (1) = skip 2 bytes
        report_data = data + 2;
        report_len = len - 2;
    } else if (report_id == DS5_REPORT_USB_INPUT && len >= 10) {
        // USB-style report: skip just report_id
        report_data = data + 1;
        report_len = len - 1;
    } else {
        // Unknown report format
        return;
    }

    if (report_len < sizeof(ds5_input_report_t)) {
        return;
    }

    const ds5_input_report_t* rpt = (const ds5_input_report_t*)report_data;

    // Parse D-pad (hat format)
    bool dpad_up    = (rpt->dpad == 0 || rpt->dpad == 1 || rpt->dpad == 7);
    bool dpad_right = (rpt->dpad >= 1 && rpt->dpad <= 3);
    bool dpad_down  = (rpt->dpad >= 3 && rpt->dpad <= 5);
    bool dpad_left  = (rpt->dpad >= 5 && rpt->dpad <= 7);

    // Build button state (inverted: 0 = pressed in USBR convention)
    uint32_t buttons = 0x00000000;

    if (dpad_up)       buttons |= USBR_BUTTON_DU;
    if (dpad_down)     buttons |= USBR_BUTTON_DD;
    if (dpad_left)     buttons |= USBR_BUTTON_DL;
    if (dpad_right)    buttons |= USBR_BUTTON_DR;
    if (rpt->cross)    buttons |= USBR_BUTTON_B1;
    if (rpt->circle)   buttons |= USBR_BUTTON_B2;
    if (rpt->square)   buttons |= USBR_BUTTON_B3;
    if (rpt->triangle) buttons |= USBR_BUTTON_B4;
    if (rpt->l1)       buttons |= USBR_BUTTON_L1;
    if (rpt->r1)       buttons |= USBR_BUTTON_R1;
    if (rpt->l2)       buttons |= USBR_BUTTON_L2;
    if (rpt->r2)       buttons |= USBR_BUTTON_R2;
    if (rpt->create)   buttons |= USBR_BUTTON_S1;
    if (rpt->option)   buttons |= USBR_BUTTON_S2;
    if (rpt->l3)       buttons |= USBR_BUTTON_L3;
    if (rpt->r3)       buttons |= USBR_BUTTON_R3;
    if (rpt->ps)       buttons |= USBR_BUTTON_A1;
    if (rpt->tpad)     buttons |= USBR_BUTTON_A2;

    // Update event
    ds5->event.buttons = buttons;

    // Analog sticks (Y-axis inverted to match convention)
    ds5->event.analog[ANALOG_X] = rpt->x1;
    ds5->event.analog[ANALOG_Y] = 255 - rpt->y1;
    ds5->event.analog[ANALOG_Z] = rpt->x2;
    ds5->event.analog[ANALOG_RX] = 255 - rpt->y2;

    // Triggers
    ds5->event.analog[ANALOG_RZ] = rpt->l2_trigger;
    ds5->event.analog[ANALOG_SLIDER] = rpt->r2_trigger;

    // Submit to router
    router_submit_input(&ds5->event);
}

static void ds5_task(bthid_device_t* device)
{
    ds5_bt_data_t* ds5 = (ds5_bt_data_t*)device->driver_data;
    if (!ds5) return;

    // Could send periodic LED/rumble updates here if needed
}

static void ds5_disconnect(bthid_device_t* device)
{
    printf("[DS5_BT] Disconnect: %s\n", device->name);

    ds5_bt_data_t* ds5 = (ds5_bt_data_t*)device->driver_data;
    if (ds5) {
        init_input_event(&ds5->event);
        ds5->initialized = false;
    }
}

// ============================================================================
// DRIVER STRUCT
// ============================================================================

const bthid_driver_t ds5_bt_driver = {
    .name = "Sony DualSense (BT)",
    .match = ds5_match,
    .init = ds5_init,
    .process_report = ds5_process_report,
    .task = ds5_task,
    .disconnect = ds5_disconnect,
};

void ds5_bt_register(void)
{
    bthid_register_driver(&ds5_bt_driver);
}
