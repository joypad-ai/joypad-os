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
#include "core/services/players/feedback.h"
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
    uint8_t hat_min;            // Logical Minimum of hat (0 or 1)
    ble_usage_loc_t buttonLoc[BLE_MAX_BUTTONS];
    uint8_t buttonCnt;
    uint8_t report_id;          // Expected gamepad input report ID (0 = none)
    bool has_sim_triggers;      // true if triggers use Simulation Controls (Xbox-style)
    bool is_xbox;               // true if Microsoft VID (0x045E) — affects button map
    bool is_8bitdo;             // true if 8BitDo VID (0x2DC8) — paddle button order
    bool digital_shoulder_triggers; // controller has no real analog triggers; its
                                // L2/R2 "trigger" axes are just the digital shoulder
                                // buttons (e.g. 8BitDo M30). Suppress analog L2/R2 so
                                // the digital buttons remain remappable.
} ble_report_map_t;

// ============================================================================
// DRIVER DATA
// ============================================================================

typedef struct {
    input_event_t event;        // Current input state
    bool initialized;
    bool has_report_map;        // true if HID descriptor was parsed
    ble_report_map_t map;       // cached field locations from descriptor
    uint8_t rumble_left;        // Last sent rumble values (for change detection)
    uint8_t rumble_right;
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

// Xbox Classic BT: sequential buttons 1-15 (no gaps like BLE), different order from generic
static const uint32_t XBOX_SEQ_BUTTON_MAP[16] = {
    0,                  // usage 0: invalid
    JP_BUTTON_B1,       // usage 1: A
    JP_BUTTON_B2,       // usage 2: B
    JP_BUTTON_B3,       // usage 3: X
    JP_BUTTON_B4,       // usage 4: Y
    JP_BUTTON_L1,       // usage 5: LB
    JP_BUTTON_R1,       // usage 6: RB
    JP_BUTTON_S1,       // usage 7: Back/View
    JP_BUTTON_S2,       // usage 8: Start/Menu
    JP_BUTTON_L3,       // usage 9: L3
    JP_BUTTON_R3,       // usage 10: R3
    JP_BUTTON_A1,       // usage 11: Guide
    0,                  // usage 12
    0,                  // usage 13
    0,                  // usage 14
    JP_BUTTON_A2,       // usage 15: Share (Series X/S)
};

// Standard sequential HID gamepads: shoulders before triggers
// Used by most generic controllers
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
    JP_BUTTON_A2,       // usage 14: capture/share
    JP_BUTTON_A3,       // usage 15: assistant/mute
};

// 8BitDo controllers with back paddles (Ultimate, Pro 2, etc.)
// VID 0x2DC8. Descriptor order inserts R4 at 3 and L4 at 6:
//   A B R4 X Y L4 LB RB LT RT Select Start L3 R3 Home Capture
static const uint32_t BITDO_BUTTON_MAP[17] = {
    0,                  // usage 0: invalid
    JP_BUTTON_B1,       // usage 1: A
    JP_BUTTON_B2,       // usage 2: B
    JP_BUTTON_R4,       // usage 3: R4 (back paddle)
    JP_BUTTON_B3,       // usage 4: X
    JP_BUTTON_B4,       // usage 5: Y
    JP_BUTTON_L4,       // usage 6: L4 (back paddle)
    JP_BUTTON_L1,       // usage 7: LB
    JP_BUTTON_R1,       // usage 8: RB
    JP_BUTTON_L2,       // usage 9: LT (digital)
    JP_BUTTON_R2,       // usage 10: RT (digital)
    JP_BUTTON_S1,       // usage 11: Select
    JP_BUTTON_S2,       // usage 12: Start
    JP_BUTTON_A1,       // usage 13: Home
    JP_BUTTON_L3,       // usage 14: L3
    JP_BUTTON_R3,       // usage 15: R3
    JP_BUTTON_A2,       // usage 16: Capture
};

// ============================================================================
// ANALOG SCALING (same as USB hid_gamepad.c scale_analog_hid_gamepad)
// ============================================================================

static uint8_t scale_analog(uint16_t value, uint32_t max_value)
{
    if (max_value == 0) return 128;
    return (uint8_t)((uint32_t)value * 255 / max_value);
}

// ============================================================================
// HID DESCRIPTOR PARSING
// ============================================================================

// Extract a field value from report data given byte index and bit mask
static uint16_t extract_field(const uint8_t* data, uint16_t len, ble_usage_loc_t* loc)
{
    if (!loc->bitMask || loc->byteIndex >= len) return 0;

    uint16_t raw;
    if (loc->bitMask > 0xFF && (loc->byteIndex + 1) < len) {
        // 16-bit field spanning two bytes (HID reports are little-endian)
        raw = (uint16_t)data[loc->byteIndex] | ((uint16_t)data[loc->byteIndex + 1] << 8);
    } else {
        raw = data[loc->byteIndex];
    }
    return (raw & loc->bitMask) >> __builtin_ctz(loc->bitMask);
}

static bool device_is_m30(const bthid_device_t* device);

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

    // Pass 1: Find the gamepad report ID (the one containing Generic Desktop X axis)
    // This reliably identifies the gamepad report in multi-report-ID descriptors
    // that may also contain Consumer Control, Keyboard, or other collections.
    uint8_t gamepad_report_id = 0;
    HID_ReportItem_t* scan = info->FirstReportItem;
    while (scan) {
        if (scan->Attributes.Usage.Page == 0x01 && scan->Attributes.Usage.Usage == 0x30) {
            gamepad_report_id = scan->ReportID;
            break;
        }
        scan = scan->Next;
    }

    // Set up report ID offset
    if (gamepad_report_id) {
        idOffset = 8;  // Report ID takes first byte (8 bits)
        gp->map.report_id = gamepad_report_id;
    } else if (info->UsingReportIDs && info->FirstReportItem) {
        // Fallback: use first item's report ID
        idOffset = 8;
        gp->map.report_id = info->FirstReportItem->ReportID;
    }

    // Pass 2: Only process items from the gamepad report
    HID_ReportItem_t* item = info->FirstReportItem;
    while (item) {
        // Skip items from other report IDs
        if (item->ReportID != gamepad_report_id) {
            item = item->Next;
            continue;
        }
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
                            gp->map.hat_min = (uint8_t)item->Attributes.Logical.Minimum;
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

    // Auto-detect swapped Z/RZ vs RX/RY axes.
    // Some controllers (8BitDo, Sony) use RX/RY for right stick and Z/RZ for triggers,
    // while others (Xbox, DirectInput) use Z/RZ for right stick and RX/RY for triggers.
    // Detect by comparing axis resolution: sticks match X/Y resolution, triggers are smaller.
    if (!gp->map.has_sim_triggers &&
        gp->map.zLoc.max && gp->map.rzLoc.max &&
        gp->map.rxLoc.max && gp->map.ryLoc.max &&
        gp->map.xLoc.max) {
        // If RX/RY have same resolution as X/Y (stick-like) and Z/RZ are smaller (trigger-like),
        // swap: RX/RY become right stick, Z/RZ become triggers
        bool rx_is_stick = (gp->map.rxLoc.max == gp->map.xLoc.max);
        bool z_is_trigger = (gp->map.zLoc.max < gp->map.xLoc.max);
        if (rx_is_stick && z_is_trigger) {
            printf("[BTHID_GAMEPAD] Swapping Z/RZ<->RX/RY (RX/RY=stick, Z/RZ=trigger)\n");
            ble_usage_loc_t tmp;
            tmp = gp->map.zLoc;  gp->map.zLoc  = gp->map.rxLoc; gp->map.rxLoc = tmp;
            tmp = gp->map.rzLoc; gp->map.rzLoc = gp->map.ryLoc; gp->map.ryLoc = tmp;
        }
    }

    gp->map.is_xbox = (device->vendor_id == 0x045E);
    gp->map.is_8bitdo = (device->vendor_id == 0x2DC8);
    gp->map.digital_shoulder_triggers = device_is_m30(device);
    gp->has_report_map = true;
    printf("[BTHID_GAMEPAD] Parsed: %d btns, X@%d Y@%d Z@%d RZ@%d RX@%d RY@%d hat@%d(min=%d) sim=%d xbox=%d 8bitdo=%d\n",
           btns_count,
           gp->map.xLoc.byteIndex, gp->map.yLoc.byteIndex,
           gp->map.zLoc.byteIndex, gp->map.rzLoc.byteIndex,
           gp->map.rxLoc.byteIndex, gp->map.ryLoc.byteIndex,
           gp->map.hatLoc.byteIndex, gp->map.hat_min, gp->map.has_sim_triggers,
           gp->map.is_xbox, gp->map.is_8bitdo);
}

// 8BitDo M30: Genesis/Saturn-style pad with NO analog triggers — its L2/R2 are
// digital shoulder buttons that the HID report also exposes as trigger axes.
// Reporting them as analog L2/R2 makes the trigger output bypass button
// remapping (the analog axis isn't remapped), so users can't reassign L2/R2.
// The M30's VID/PID arrives via the SDP Device ID query (deferred to
// HID_SUBEVENT_DESCRIPTOR_AVAILABLE), which then re-runs bthid_gamepad_update_vid.
static bool device_is_m30(const bthid_device_t* device)
{
    return (device->vendor_id == 0x2DC8 &&
            (device->product_id == 0x0651 ||   // M30 over Bluetooth (SDP Device ID)
             device->product_id == 0x5006));   // M30 USB PID (defensive)
}

void bthid_gamepad_update_vid(bthid_device_t* device)
{
    bthid_gamepad_data_t* gp = (bthid_gamepad_data_t*)device->driver_data;
    if (!gp || !gp->has_report_map) return;

    gp->map.is_xbox = (device->vendor_id == 0x045E);
    gp->map.is_8bitdo = (device->vendor_id == 0x2DC8);
    gp->map.digital_shoulder_triggers = device_is_m30(device);
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

    // Controllers whose "triggers" are really digital shoulder buttons (M30):
    // drop the synthesized analog so L2/R2 come only from the digital buttons,
    // which stay subject to button remapping.
    if (map->digital_shoulder_triggers) {
        l2 = 0;
        r2 = 0;
    }

    // Hat switch -> dpad
    // Table is 0-based: [0]=N, [1]=NE, ..., [7]=NW, [8]=center
    // HID descriptors use either min=0 (0=N) or min=1 (1=N, 0=center)
    if (map->hatLoc.bitMask && map->hatLoc.byteIndex < len) {
        uint8_t hatValue = (uint8_t)extract_field(data, len, &map->hatLoc);
        uint8_t direction;
        if (map->hat_min > 0) {
            // 1-based hat: value 0 and values > max are center
            direction = (hatValue >= map->hat_min && hatValue <= map->hat_min + 7)
                        ? (hatValue - map->hat_min) : 8;
        } else {
            direction = hatValue <= 8 ? hatValue : 8;
        }
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
    if (map->is_xbox && map->has_sim_triggers) {
        // Xbox BLE: gap-pattern buttons with Simulation Controls triggers
        btn_map = XBOX_BUTTON_MAP;
        btn_map_size = sizeof(XBOX_BUTTON_MAP) / sizeof(XBOX_BUTTON_MAP[0]);
    } else if (map->is_xbox) {
        // Xbox Classic BT: sequential buttons (no gaps), different order
        btn_map = XBOX_SEQ_BUTTON_MAP;
        btn_map_size = sizeof(XBOX_SEQ_BUTTON_MAP) / sizeof(XBOX_SEQ_BUTTON_MAP[0]);
    } else if (map->is_8bitdo && map->buttonCnt > 14) {
        // 8BitDo with paddles (Ultimate, etc.): R4 at usage 3, L4 at usage 6
        // Models without paddles (SN30 Pro, M30) have ≤14 buttons and use SEQ map
        btn_map = BITDO_BUTTON_MAP;
        btn_map_size = sizeof(BITDO_BUTTON_MAP) / sizeof(BITDO_BUTTON_MAP[0]);
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

    // Xbox extra byte: last byte of report, bit 0 (outside HID buttons bitfield)
    // BLE (Series): Share button → A2
    // Classic BT (One): Back/View button → S1
    if (map->is_xbox && len > 0 && (data[len - 1] & 0x01)) {
        if (gp->event.transport == INPUT_TRANSPORT_BT_BLE) {
            buttons |= JP_BUTTON_A2;
        } else {
            buttons |= JP_BUTTON_S1;
        }
    }

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
        // Filter by report ID — skip non-gamepad reports (battery, feature, etc.)
        // that would otherwise be parsed as gamepad data with wrong byte layout
        if (gp->map.report_id && len > 0 && data[0] != gp->map.report_id) {
            return;
        }
        // One-time hex dump of first gamepad report for debugging
        static bool dumped = false;
        if (!dumped) {
            dumped = true;
            printf("[BTHID_GAMEPAD] Report (%d bytes):", len);
            for (int i = 0; i < len && i < 20; i++) printf(" %02x", data[i]);
            printf("\n");
            printf("[BTHID_GAMEPAD] Map: X@%d/%04x Y@%d/%04x Z@%d/%04x RZ@%d/%04x RX@%d/%04x RY@%d/%04x hat@%d\n",
                   gp->map.xLoc.byteIndex, gp->map.xLoc.bitMask,
                   gp->map.yLoc.byteIndex, gp->map.yLoc.bitMask,
                   gp->map.zLoc.byteIndex, gp->map.zLoc.bitMask,
                   gp->map.rzLoc.byteIndex, gp->map.rzLoc.bitMask,
                   gp->map.rxLoc.byteIndex, gp->map.rxLoc.bitMask,
                   gp->map.ryLoc.byteIndex, gp->map.ryLoc.bitMask,
                   gp->map.hatLoc.byteIndex);
        }
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

// Xbox rumble output report constants
#define XBOX_RUMBLE_REPORT_ID   0x03
#define XBOX_RUMBLE_MOTORS      0x03  // Enable strong (bit 1) + weak (bit 0) main motors

static void gamepad_task(bthid_device_t* device)
{
    bthid_gamepad_data_t* gp = (bthid_gamepad_data_t*)device->driver_data;
    if (!gp) return;

    int player_idx = find_player_index(gp->event.dev_addr, gp->event.instance);
    if (player_idx < 0) return;

    feedback_state_t* fb = feedback_get_state(player_idx);
    if (!fb || !fb->rumble_dirty) return;

    uint8_t left = fb->rumble.left;
    uint8_t right = fb->rumble.right;

    if (left != gp->rumble_left || right != gp->rumble_right) {
        // Xbox controllers (VID 0x045E): Report ID 0x03, 8 bytes
        // [0]=enable_actuators, [1]=lt_trigger, [2]=rt_trigger,
        // [3]=strong_motor, [4]=weak_motor, [5]=duration, [6]=delay, [7]=repeat
        if (device->vendor_id == 0x045E) {
            uint8_t buf[8];
            buf[0] = XBOX_RUMBLE_MOTORS;
            buf[1] = 0;                              // Left trigger (unused)
            buf[2] = 0;                              // Right trigger (unused)
            buf[3] = ((uint16_t)left * 100) / 255;   // Strong motor (0-100)
            buf[4] = ((uint16_t)right * 100) / 255;  // Weak motor (0-100)
            buf[5] = 0xFF;                           // Duration: continuous
            buf[6] = 0x00;                           // Delay: none
            buf[7] = 0x00;                           // Repeat: none
            bthid_send_output_report(device->conn_index, XBOX_RUMBLE_REPORT_ID, buf, sizeof(buf));
        }

        gp->rumble_left = left;
        gp->rumble_right = right;
    }

    feedback_clear_dirty(player_idx);
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
