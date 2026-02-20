// bthid_gamepad.c - Generic Bluetooth Gamepad Driver
// Handles basic HID gamepads over Bluetooth
// This is a fallback driver for gamepads without a specific driver
//
// For BLE devices with HID descriptors, uses the same HID report parser
// as the USB path (hid_parser.c) to dynamically extract field locations.
// Falls back to hardcoded 6-byte layout for Classic BT devices without descriptors.

#include "bthid_gamepad.h"
#include "bt/bthid/bthid.h"
#include "bt/transport/bt_transport.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include "usb/usbh/hid/devices/generic/hid_parser.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// REPORT MAP TYPES (mirrors USB hid_gamepad.c dinput_usage_t)
// ============================================================================

#define BLE_MAX_BUTTONS 16

typedef struct {
    uint8_t byteIndex;
    uint16_t bitMask;
    uint32_t max;
} ble_usage_loc_t;

typedef struct {
    ble_usage_loc_t xLoc, yLoc, zLoc, rzLoc, rxLoc, ryLoc;
    ble_usage_loc_t hatLoc;
    ble_usage_loc_t buttonLoc[BLE_MAX_BUTTONS];
    uint8_t buttonCnt;
    bool has_sim_triggers;      // true if triggers use Simulation Controls (Xbox-style)
} ble_report_map_t;

// ============================================================================
// DRIVER DATA
// ============================================================================

typedef struct {
    input_event_t event;        // Current input state
    bool initialized;
    bool has_report_map;        // true if HID descriptor was parsed
    ble_report_map_t map;       // cached field locations from descriptor
} bthid_gamepad_data_t;

static bthid_gamepad_data_t gamepad_data[BTHID_MAX_DEVICES];

// ============================================================================
// HAT SWITCH LOOKUP (same as USB hid_gamepad.c)
// ============================================================================
// hat format: 8 = released, 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW
// Returns packed dpad bits: bit0=up, bit1=right, bit2=down, bit3=left

static const uint8_t HAT_SWITCH_TO_DIRECTION_BUTTONS[] = {
    0b0001, 0b0011, 0b0010, 0b0110, 0b0100, 0b1100, 0b1000, 0b1001, 0b0000
};

// ============================================================================
// BUTTON USAGE MAPPING TABLES
// ============================================================================

// Xbox BT HID: buttons 1-15 with gaps at 3,6,9,10
// A=1, B=2, X=4, Y=5, LB=7, RB=8, View=11, Menu=12, Xbox=13, L3=14, R3=15
static const uint32_t XBOX_BUTTON_MAP[17] = {
    0,                  // usage 0: invalid
    JP_BUTTON_B1,       // usage 1: A
    JP_BUTTON_B2,       // usage 2: B
    0,                  // usage 3: (pad)
    JP_BUTTON_B3,       // usage 4: X
    JP_BUTTON_B4,       // usage 5: Y
    0,                  // usage 6: (pad)
    JP_BUTTON_L1,       // usage 7: LB
    JP_BUTTON_R1,       // usage 8: RB
    0,                  // usage 9: (pad)
    0,                  // usage 10: (pad)
    JP_BUTTON_S1,       // usage 11: View
    JP_BUTTON_S2,       // usage 12: Menu
    JP_BUTTON_A1,       // usage 13: Xbox
    JP_BUTTON_L3,       // usage 14: L3
    JP_BUTTON_R3,       // usage 15: R3
    JP_BUTTON_A2,       // usage 16: Share (Series X/S)
};

// Standard sequential HID gamepads (8BitDo, generic controllers)
// Button 1-12 map directly to face/shoulder/trigger/meta buttons
static const uint32_t SEQ_BUTTON_MAP[16] = {
    0,                  // usage 0: invalid
    JP_BUTTON_B1,       // usage 1: face 1 (A/Cross)
    JP_BUTTON_B2,       // usage 2: face 2 (B/Circle)
    JP_BUTTON_B3,       // usage 3: face 3 (X/Square)
    JP_BUTTON_B4,       // usage 4: face 4 (Y/Triangle)
    JP_BUTTON_L1,       // usage 5: left shoulder
    JP_BUTTON_R1,       // usage 6: right shoulder
    JP_BUTTON_L2,       // usage 7: left trigger (digital)
    JP_BUTTON_R2,       // usage 8: right trigger (digital)
    JP_BUTTON_S1,       // usage 9: select/back
    JP_BUTTON_S2,       // usage 10: start/menu
    JP_BUTTON_L3,       // usage 11: left stick
    JP_BUTTON_R3,       // usage 12: right stick
    JP_BUTTON_A1,       // usage 13: guide/home
    0,                  // usage 14: extra
    0,                  // usage 15: extra
};

// ============================================================================
// ANALOG SCALING (same as USB hid_gamepad.c scale_analog_hid_gamepad)
// ============================================================================

static uint8_t scale_analog(uint16_t value, uint32_t max_value)
{
    int mid = max_value / 2;
    if (value <= (uint16_t)mid) {
        return 1 + (value * 127) / mid;
    }
    return 128 + ((value - mid) * 127) / (max_value - mid);
}

// ============================================================================
// HID DESCRIPTOR PARSING
// ============================================================================

// Extract a field value from report data given byte index and bit mask
static uint16_t extract_field(const uint8_t* data, uint16_t len, ble_usage_loc_t* loc)
{
    if (!loc->bitMask || loc->byteIndex >= len) return 0;

    if (loc->bitMask > 0xFF && (loc->byteIndex + 1) < len) {
        // 16-bit field spanning two bytes (HID reports are little-endian)
        uint16_t combined = (uint16_t)data[loc->byteIndex] | ((uint16_t)data[loc->byteIndex + 1] << 8);
        return (combined & loc->bitMask) >> __builtin_ctz(loc->bitMask);
    }
    return data[loc->byteIndex] & loc->bitMask;
}

void bthid_gamepad_set_descriptor(bthid_device_t* device, const uint8_t* desc, uint16_t desc_len)
{
    bthid_gamepad_data_t* gp = (bthid_gamepad_data_t*)device->driver_data;
    if (!gp) return;

    printf("[BTHID_GAMEPAD] Parsing HID descriptor (%d bytes)\n", desc_len);

    HID_ReportInfo_t* info = NULL;
    uint8_t ret = USB_ProcessHIDReport(0, 0, desc, desc_len, &info);
    if (ret != HID_PARSE_Successful) {
        printf("[BTHID_GAMEPAD] HID parse failed: %d\n", ret);
        return;
    }

    // Clear the map
    memset(&gp->map, 0, sizeof(ble_report_map_t));

    uint8_t btns_count = 0;
    uint8_t idOffset = 0;

    HID_ReportItem_t* item = info->FirstReportItem;

    // Check if report uses report IDs
    if (item && item->ReportID) {
        idOffset = 8;  // Report ID takes first byte (8 bits)
    }

    while (item) {
        uint8_t bitSize = item->Attributes.BitSize;
        uint8_t bitOffset = item->BitOffset + idOffset;
        uint16_t bitMask = ((0xFFFF >> (16 - bitSize)) << (bitOffset % 8));
        uint8_t byteIndex = bitOffset / 8;

        uint8_t report[1] = {0};
        if (USB_GetHIDReportItemInfo(item->ReportID, report, item)) {
            switch (item->Attributes.Usage.Page) {
                case 0x01:  // Generic Desktop
                    switch (item->Attributes.Usage.Usage) {
                        case 0x30:  // X - Left Analog X
                            gp->map.xLoc.byteIndex = byteIndex;
                            gp->map.xLoc.bitMask = bitMask;
                            gp->map.xLoc.max = item->Attributes.Logical.Maximum;
                            break;
                        case 0x31:  // Y - Left Analog Y
                            gp->map.yLoc.byteIndex = byteIndex;
                            gp->map.yLoc.bitMask = bitMask;
                            gp->map.yLoc.max = item->Attributes.Logical.Maximum;
                            break;
                        case 0x32:  // Z - Right Analog X
                            gp->map.zLoc.byteIndex = byteIndex;
                            gp->map.zLoc.bitMask = bitMask;
                            gp->map.zLoc.max = item->Attributes.Logical.Maximum;
                            break;
                        case 0x35:  // RZ - Right Analog Y
                            gp->map.rzLoc.byteIndex = byteIndex;
                            gp->map.rzLoc.bitMask = bitMask;
                            gp->map.rzLoc.max = item->Attributes.Logical.Maximum;
                            break;
                        case 0x33:  // RX - Left Trigger
                            gp->map.rxLoc.byteIndex = byteIndex;
                            gp->map.rxLoc.bitMask = bitMask;
                            gp->map.rxLoc.max = item->Attributes.Logical.Maximum;
                            break;
                        case 0x34:  // RY - Right Trigger
                            gp->map.ryLoc.byteIndex = byteIndex;
                            gp->map.ryLoc.bitMask = bitMask;
                            gp->map.ryLoc.max = item->Attributes.Logical.Maximum;
                            break;
                        case 0x39:  // Hat switch
                            gp->map.hatLoc.byteIndex = byteIndex;
                            gp->map.hatLoc.bitMask = bitMask;
                            break;
                    }
                    break;
                case 0x02:  // Simulation Controls (Xbox-style triggers)
                    switch (item->Attributes.Usage.Usage) {
                        case 0xC5:  // Brake → Left Trigger
                            gp->map.rxLoc.byteIndex = byteIndex;
                            gp->map.rxLoc.bitMask = bitMask;
                            gp->map.rxLoc.max = item->Attributes.Logical.Maximum;
                            gp->map.has_sim_triggers = true;
                            break;
                        case 0xC4:  // Accelerator → Right Trigger
                            gp->map.ryLoc.byteIndex = byteIndex;
                            gp->map.ryLoc.bitMask = bitMask;
                            gp->map.ryLoc.max = item->Attributes.Logical.Maximum;
                            gp->map.has_sim_triggers = true;
                            break;
                    }
                    break;
                case 0x09: {  // Button
                    uint8_t usage = item->Attributes.Usage.Usage;
                    if (usage >= 1 && usage <= BLE_MAX_BUTTONS) {
                        gp->map.buttonLoc[usage - 1].byteIndex = byteIndex;
                        gp->map.buttonLoc[usage - 1].bitMask = bitMask;
                    }
                    btns_count++;
                    break;
                }
            }
        }
        item = item->Next;
    }

    gp->map.buttonCnt = btns_count;

    // Release parser memory
    USB_FreeReportInfo(info);

    gp->has_report_map = true;
    printf("[BTHID_GAMEPAD] Descriptor parsed: %d buttons, X@%d Y@%d Z@%d RZ@%d RX@%d RY@%d hat@%d sim_triggers=%d\n",
           btns_count,
           gp->map.xLoc.byteIndex, gp->map.yLoc.byteIndex,
           gp->map.zLoc.byteIndex, gp->map.rzLoc.byteIndex,
           gp->map.rxLoc.byteIndex, gp->map.ryLoc.byteIndex,
           gp->map.hatLoc.byteIndex, gp->map.has_sim_triggers);
}

// ============================================================================
// DYNAMIC REPORT PROCESSING (from parsed HID descriptor)
// ============================================================================

static void process_report_dynamic(bthid_gamepad_data_t* gp, const uint8_t* data, uint16_t len)
{
    ble_report_map_t* map = &gp->map;
    uint32_t buttons = 0;

    // Extract analog axes
    uint8_t lx = 128, ly = 128, rx = 128, ry = 128;
    uint8_t l2 = 0, r2 = 0;

    if (map->xLoc.max) {
        lx = scale_analog(extract_field(data, len, &map->xLoc), map->xLoc.max);
    }
    if (map->yLoc.max) {
        ly = scale_analog(extract_field(data, len, &map->yLoc), map->yLoc.max);
    }
    if (map->zLoc.max) {
        rx = scale_analog(extract_field(data, len, &map->zLoc), map->zLoc.max);
    }
    if (map->rzLoc.max) {
        ry = scale_analog(extract_field(data, len, &map->rzLoc), map->rzLoc.max);
    }
    if (map->rxLoc.max) {
        l2 = scale_analog(extract_field(data, len, &map->rxLoc), map->rxLoc.max);
    }
    if (map->ryLoc.max) {
        r2 = scale_analog(extract_field(data, len, &map->ryLoc), map->ryLoc.max);
    }

    // Hat switch -> dpad
    if (map->hatLoc.bitMask && map->hatLoc.byteIndex < len) {
        uint8_t hatValue = data[map->hatLoc.byteIndex] & map->hatLoc.bitMask;
        uint8_t direction = hatValue <= 8 ? hatValue : 8;
        uint8_t dpad = HAT_SWITCH_TO_DIRECTION_BUTTONS[direction];
        if (dpad & 0x01) buttons |= JP_BUTTON_DU;
        if (dpad & 0x02) buttons |= JP_BUTTON_DR;
        if (dpad & 0x04) buttons |= JP_BUTTON_DD;
        if (dpad & 0x08) buttons |= JP_BUTTON_DL;
    }

    // Map buttons by HID usage number using descriptor-derived layout detection
    // Simulation Controls triggers (Brake/Accelerator) = Xbox gap pattern
    // Generic Desktop triggers (Rx/Ry) = sequential button layout
    const uint32_t* btn_map;
    uint8_t btn_map_size;
    if (map->has_sim_triggers) {
        btn_map = XBOX_BUTTON_MAP;
        btn_map_size = sizeof(XBOX_BUTTON_MAP) / sizeof(XBOX_BUTTON_MAP[0]);
    } else {
        btn_map = SEQ_BUTTON_MAP;
        btn_map_size = sizeof(SEQ_BUTTON_MAP) / sizeof(SEQ_BUTTON_MAP[0]);
    }

    uint8_t buttonCount = 0;
    for (int i = 0; i < BLE_MAX_BUTTONS; i++) {
        if (map->buttonLoc[i].bitMask) {
            buttonCount++;
            if (map->buttonLoc[i].byteIndex < len &&
                (data[map->buttonLoc[i].byteIndex] & map->buttonLoc[i].bitMask)) {
                uint8_t usage = i + 1;  // usage number = slot index + 1
                if (usage < btn_map_size) {
                    buttons |= btn_map[usage];
                }
            }
        }
    }

    // Keep sticks within range [1-255]
    if (lx == 0) lx = 1;
    if (ly == 0) ly = 1;
    if (rx == 0) rx = 1;
    if (ry == 0) ry = 1;

    gp->event.buttons = buttons;
    gp->event.button_count = buttonCount;
    gp->event.analog[ANALOG_LX] = lx;
    gp->event.analog[ANALOG_LY] = ly;
    gp->event.analog[ANALOG_RX] = rx;
    gp->event.analog[ANALOG_RY] = ry;
    gp->event.analog[ANALOG_L2] = l2;
    gp->event.analog[ANALOG_R2] = r2;

    router_submit_input(&gp->event);
}

// ============================================================================
// DRIVER IMPLEMENTATION
// ============================================================================

static bool gamepad_match(const char* device_name, const uint8_t* class_of_device,
                          uint16_t vendor_id, uint16_t product_id, bool is_ble)
{
    (void)device_name;
    (void)vendor_id;   // Generic driver doesn't use VID/PID
    (void)product_id;

    // BLE devices don't have COD — match any BLE HID device as fallback
    if (is_ble) {
        return true;
    }

    if (!class_of_device) {
        return false;
    }

    // Check for Peripheral major class (0x05)
    uint8_t major_class = (class_of_device[1] >> 0) & 0x1F;
    if (major_class != 0x05) {
        return false;
    }

    // Check for gamepad/joystick in minor class
    uint8_t minor_class = (class_of_device[0] >> 2) & 0x3F;
    uint8_t device_subtype = minor_class & 0x0F;

    // 0x01 = Joystick, 0x02 = Gamepad
    if (device_subtype == 0x01 || device_subtype == 0x02) {
        return true;
    }

    return false;
}

static bool gamepad_init(bthid_device_t* device)
{
    printf("[BTHID_GAMEPAD] Init for device: %s\n", device->name);

    // Find free data slot
    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (!gamepad_data[i].initialized) {
            // Initialize input event with defaults
            init_input_event(&gamepad_data[i].event);
            gamepad_data[i].initialized = true;
            gamepad_data[i].has_report_map = false;
            memset(&gamepad_data[i].map, 0, sizeof(ble_report_map_t));

            // Set device info
            gamepad_data[i].event.type = INPUT_TYPE_GAMEPAD;
            gamepad_data[i].event.transport = device->is_ble ? INPUT_TRANSPORT_BT_BLE : INPUT_TRANSPORT_BT_CLASSIC;
            gamepad_data[i].event.dev_addr = device->conn_index;  // Use conn_index as address
            gamepad_data[i].event.instance = 0;

            device->driver_data = &gamepad_data[i];
            return true;
        }
    }

    return false;
}

static void gamepad_process_report(bthid_device_t* device, const uint8_t* data, uint16_t len)
{
    bthid_gamepad_data_t* gp = (bthid_gamepad_data_t*)device->driver_data;
    if (!gp) {
        return;
    }

    // Dynamic path: use parsed HID descriptor for field extraction
    if (gp->has_report_map) {
        process_report_dynamic(gp, data, len);
        return;
    }

    // Fallback: hardcoded 6-byte layout for Classic BT without descriptors
    if (len < 4) {
        return;
    }

    uint32_t raw_buttons = 0;
    if (len >= 1) raw_buttons |= data[0];
    if (len >= 2) raw_buttons |= (uint32_t)data[1] << 8;

    uint32_t buttons = 0;

    if (raw_buttons & 0x0001) buttons |= JP_BUTTON_B1;  // A/Cross
    if (raw_buttons & 0x0002) buttons |= JP_BUTTON_B2;  // B/Circle
    if (raw_buttons & 0x0004) buttons |= JP_BUTTON_B3;  // X/Square
    if (raw_buttons & 0x0008) buttons |= JP_BUTTON_B4;  // Y/Triangle
    if (raw_buttons & 0x0010) buttons |= JP_BUTTON_L1;  // LB
    if (raw_buttons & 0x0020) buttons |= JP_BUTTON_R1;  // RB
    if (raw_buttons & 0x0040) buttons |= JP_BUTTON_L2;  // LT (digital)
    if (raw_buttons & 0x0080) buttons |= JP_BUTTON_R2;  // RT (digital)
    if (raw_buttons & 0x0100) buttons |= JP_BUTTON_S1;  // Select/Back
    if (raw_buttons & 0x0200) buttons |= JP_BUTTON_S2;  // Start
    if (raw_buttons & 0x0400) buttons |= JP_BUTTON_L3;  // LS
    if (raw_buttons & 0x0800) buttons |= JP_BUTTON_R3;  // RS
    if (raw_buttons & 0x1000) buttons |= JP_BUTTON_A1;  // Home/Guide

    gp->event.buttons = buttons;

    // Axes (using analog[] array indices from input_event.h)
    if (len >= 3) gp->event.analog[ANALOG_LX] = data[2];   // Left stick X
    if (len >= 4) gp->event.analog[ANALOG_LY] = data[3];   // Left stick Y
    if (len >= 5) gp->event.analog[ANALOG_RX] = data[4];   // Right stick X
    if (len >= 6) gp->event.analog[ANALOG_RY] = data[5];  // Right stick Y

    // Submit to router
    router_submit_input(&gp->event);
}

static void gamepad_task(bthid_device_t* device)
{
    (void)device;
    // Nothing periodic for generic gamepad
}

static void gamepad_disconnect(bthid_device_t* device)
{
    printf("[BTHID_GAMEPAD] Disconnect: %s\n", device->name);

    bthid_gamepad_data_t* gp = (bthid_gamepad_data_t*)device->driver_data;
    if (gp) {
        // Clear router state first (sends zeroed input report)
        router_device_disconnected(gp->event.dev_addr, gp->event.instance);
        // Remove player assignment
        remove_players_by_address(gp->event.dev_addr, gp->event.instance);

        init_input_event(&gp->event);
        gp->has_report_map = false;
        gp->initialized = false;
    }
}

// ============================================================================
// DRIVER STRUCT
// ============================================================================

const bthid_driver_t bthid_gamepad_driver = {
    .name = "Generic BT Gamepad",
    .match = gamepad_match,
    .init = gamepad_init,
    .process_report = gamepad_process_report,
    .task = gamepad_task,
    .disconnect = gamepad_disconnect,
};

void bthid_gamepad_register(void)
{
    bthid_register_driver(&bthid_gamepad_driver);
}
