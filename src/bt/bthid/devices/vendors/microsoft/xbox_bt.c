// xbox_bt.c - Xbox Bluetooth Controller Driver
// Handles Xbox One/Series controllers over Bluetooth
//
// Reference: Xbox controllers use standard HID over Bluetooth
// Report format is similar to USB but with BT HID report structure

#include "xbox_bt.h"
#include "bt/bthid/bthid.h"
#include "bt/transport/bt_transport.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// XBOX BT CONSTANTS
// ============================================================================

// Xbox controller button masks (from standard HID gamepad report)
#define XBOX_BT_DPAD_UP         0x0001
#define XBOX_BT_DPAD_DOWN       0x0002
#define XBOX_BT_DPAD_LEFT       0x0004
#define XBOX_BT_DPAD_RIGHT      0x0008
#define XBOX_BT_START           0x0010  // Menu button
#define XBOX_BT_BACK            0x0020  // View button
#define XBOX_BT_LEFT_THUMB      0x0040
#define XBOX_BT_RIGHT_THUMB     0x0080
#define XBOX_BT_LEFT_SHOULDER   0x0100
#define XBOX_BT_RIGHT_SHOULDER  0x0200
#define XBOX_BT_GUIDE           0x0400
#define XBOX_BT_A               0x1000
#define XBOX_BT_B               0x2000
#define XBOX_BT_X               0x4000
#define XBOX_BT_Y               0x8000

// ============================================================================
// XBOX BT REPORT STRUCTURE
// ============================================================================

// Xbox BT HID input report (standard gamepad format)
typedef struct __attribute__((packed)) {
    uint8_t report_id;          // Report ID

    int16_t lx;                 // Left stick X (-32768 to 32767)
    int16_t ly;                 // Left stick Y (-32768 to 32767)
    int16_t rx;                 // Right stick X (-32768 to 32767)
    int16_t ry;                 // Right stick Y (-32768 to 32767)

    uint16_t lt;                // Left trigger (0-1023)
    uint16_t rt;                // Right trigger (0-1023)

    uint8_t dpad;               // D-pad as hat (0-7, 8=center)

    uint16_t buttons;           // Button bitfield
} xbox_bt_input_report_t;

// Alternative format some controllers use
typedef struct __attribute__((packed)) {
    uint8_t report_id;

    uint16_t buttons;           // Button bitfield

    uint8_t lt;                 // Left trigger (0-255)
    uint8_t rt;                 // Right trigger (0-255)

    int16_t lx;                 // Left stick X
    int16_t ly;                 // Left stick Y
    int16_t rx;                 // Right stick X
    int16_t ry;                 // Right stick Y
} xbox_bt_input_alt_t;

// ============================================================================
// DRIVER DATA
// ============================================================================

typedef struct {
    input_event_t event;
    bool initialized;
} xbox_bt_data_t;

static xbox_bt_data_t xbox_data[BTHID_MAX_DEVICES];

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Scale 16-bit signed stick value to 8-bit unsigned (1-255, 128 center)
static uint8_t scale_stick_16to8(int16_t val)
{
    // Scale from [-32768, 32767] to [1, 255]
    int32_t scaled = ((int32_t)val + 32768) / 256;
    if (scaled <= 0) return 1;
    if (scaled >= 255) return 255;
    return (uint8_t)scaled;
}

// Scale 10-bit trigger value to 8-bit (0-255)
static uint8_t scale_trigger_10to8(uint16_t val)
{
    // Scale from [0, 1023] to [0, 255]
    return (uint8_t)(val >> 2);
}

// ============================================================================
// DRIVER IMPLEMENTATION
// ============================================================================

static bool xbox_match(const char* device_name, const uint8_t* class_of_device,
                       uint16_t vendor_id, uint16_t product_id, bool is_ble)
{
    (void)class_of_device;
    (void)product_id;
    (void)is_ble;

    // VID match - Microsoft vendor ID = 0x045E
    // Many Xbox controller PIDs exist, so just match VID
    if (vendor_id == 0x045E) {
        return true;
    }

    // Name-based match (fallback)
    if (device_name) {
        if (strstr(device_name, "Xbox Wireless Controller") != NULL) {
            return true;
        }
        if (strstr(device_name, "Xbox Elite") != NULL) {
            return true;
        }
        if (strstr(device_name, "Xbox Adaptive") != NULL) {
            return true;
        }
        if (strstr(device_name, "Microsoft") != NULL &&
            strstr(device_name, "Controller") != NULL) {
            return true;
        }
    }

    return false;
}

static bool xbox_init(bthid_device_t* device)
{
    printf("[XBOX_BT] Init for device: %s\n", device->name);

    // Find free data slot
    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (!xbox_data[i].initialized) {
            init_input_event(&xbox_data[i].event);
            xbox_data[i].initialized = true;

            xbox_data[i].event.type = INPUT_TYPE_GAMEPAD;
            xbox_data[i].event.transport = INPUT_TRANSPORT_BT_CLASSIC;
            xbox_data[i].event.dev_addr = device->conn_index;
            xbox_data[i].event.instance = 0;
            xbox_data[i].event.button_count = 10;

            device->driver_data = &xbox_data[i];

            return true;
        }
    }

    return false;
}

static void xbox_process_report(bthid_device_t* device, const uint8_t* data, uint16_t len)
{
    xbox_bt_data_t* xbox = (xbox_bt_data_t*)device->driver_data;
    if (!xbox || len < 2) return;

    uint32_t buttons = 0x00000000;
    uint8_t lx = 128, ly = 128, rx = 128, ry = 128;
    uint8_t lt = 0, rt = 0;

    // Xbox BT controllers can send different report formats
    // Try to detect based on report length and parse accordingly

    if (len >= sizeof(xbox_bt_input_report_t)) {
        // Standard HID gamepad format (longer report)
        const xbox_bt_input_report_t* rpt = (const xbox_bt_input_report_t*)data;

        // Parse sticks
        lx = scale_stick_16to8(rpt->lx);
        ly = scale_stick_16to8(-rpt->ly);  // Invert Y
        rx = scale_stick_16to8(rpt->rx);
        ry = scale_stick_16to8(-rpt->ry);  // Invert Y

        // Parse triggers
        lt = scale_trigger_10to8(rpt->lt);
        rt = scale_trigger_10to8(rpt->rt);

        // Parse D-pad (hat format)
        uint8_t dpad = rpt->dpad;
        if (dpad == 0 || dpad == 1 || dpad == 7) buttons |= JP_BUTTON_DU;
        if (dpad >= 1 && dpad <= 3) buttons |= JP_BUTTON_DR;
        if (dpad >= 3 && dpad <= 5) buttons |= JP_BUTTON_DD;
        if (dpad >= 5 && dpad <= 7) buttons |= JP_BUTTON_DL;

        // Parse buttons
        uint16_t btn = rpt->buttons;
        if (btn & XBOX_BT_A)              buttons |= JP_BUTTON_B1;
        if (btn & XBOX_BT_B)              buttons |= JP_BUTTON_B2;
        if (btn & XBOX_BT_X)              buttons |= JP_BUTTON_B3;
        if (btn & XBOX_BT_Y)              buttons |= JP_BUTTON_B4;
        if (btn & XBOX_BT_LEFT_SHOULDER)  buttons |= JP_BUTTON_L1;
        if (btn & XBOX_BT_RIGHT_SHOULDER) buttons |= JP_BUTTON_R1;
        if (lt > 10)                      buttons |= JP_BUTTON_L2;
        if (rt > 10)                      buttons |= JP_BUTTON_R2;
        if (btn & XBOX_BT_BACK)           buttons |= JP_BUTTON_S1;
        if (btn & XBOX_BT_START)          buttons |= JP_BUTTON_S2;
        if (btn & XBOX_BT_LEFT_THUMB)     buttons |= JP_BUTTON_L3;
        if (btn & XBOX_BT_RIGHT_THUMB)    buttons |= JP_BUTTON_R3;
        if (btn & XBOX_BT_GUIDE)          buttons |= JP_BUTTON_A1;

    } else if (len >= sizeof(xbox_bt_input_alt_t)) {
        // Alternative format (older controllers or different firmware)
        const xbox_bt_input_alt_t* rpt = (const xbox_bt_input_alt_t*)data;

        // Parse sticks
        lx = scale_stick_16to8(rpt->lx);
        ly = scale_stick_16to8(-rpt->ly);
        rx = scale_stick_16to8(rpt->rx);
        ry = scale_stick_16to8(-rpt->ry);

        // Parse triggers (already 8-bit)
        lt = rpt->lt;
        rt = rpt->rt;

        // Parse buttons (button bitfield in different position)
        uint16_t btn = rpt->buttons;

        // D-pad might be in buttons for this format
        if (btn & XBOX_BT_DPAD_UP)        buttons |= JP_BUTTON_DU;
        if (btn & XBOX_BT_DPAD_DOWN)      buttons |= JP_BUTTON_DD;
        if (btn & XBOX_BT_DPAD_LEFT)      buttons |= JP_BUTTON_DL;
        if (btn & XBOX_BT_DPAD_RIGHT)     buttons |= JP_BUTTON_DR;
        if (btn & XBOX_BT_A)              buttons |= JP_BUTTON_B1;
        if (btn & XBOX_BT_B)              buttons |= JP_BUTTON_B2;
        if (btn & XBOX_BT_X)              buttons |= JP_BUTTON_B3;
        if (btn & XBOX_BT_Y)              buttons |= JP_BUTTON_B4;
        if (btn & XBOX_BT_LEFT_SHOULDER)  buttons |= JP_BUTTON_L1;
        if (btn & XBOX_BT_RIGHT_SHOULDER) buttons |= JP_BUTTON_R1;
        if (lt > 10)                      buttons |= JP_BUTTON_L2;
        if (rt > 10)                      buttons |= JP_BUTTON_R2;
        if (btn & XBOX_BT_BACK)           buttons |= JP_BUTTON_S1;
        if (btn & XBOX_BT_START)          buttons |= JP_BUTTON_S2;
        if (btn & XBOX_BT_LEFT_THUMB)     buttons |= JP_BUTTON_L3;
        if (btn & XBOX_BT_RIGHT_THUMB)    buttons |= JP_BUTTON_R3;
        if (btn & XBOX_BT_GUIDE)          buttons |= JP_BUTTON_A1;

    } else {
        // Unknown format, skip
        return;
    }

    // Update event
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

static void xbox_task(bthid_device_t* device)
{
    (void)device;
    // Xbox BT controllers don't need periodic maintenance
    // Rumble is handled through HID output reports when needed
}

static void xbox_disconnect(bthid_device_t* device)
{
    printf("[XBOX_BT] Disconnect: %s\n", device->name);

    xbox_bt_data_t* xbox = (xbox_bt_data_t*)device->driver_data;
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

const bthid_driver_t xbox_bt_driver = {
    .name = "Xbox Wireless Controller (BT)",
    .match = xbox_match,
    .init = xbox_init,
    .process_report = xbox_process_report,
    .task = xbox_task,
    .disconnect = xbox_disconnect,
};

void xbox_bt_register(void)
{
    bthid_register_driver(&xbox_bt_driver);
}
