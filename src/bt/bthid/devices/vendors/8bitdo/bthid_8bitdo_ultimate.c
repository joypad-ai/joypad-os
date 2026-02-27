// bthid_8bitdo_ultimate.c - 8BitDo Ultimate 3-mode Xbox Controller (BLE)
//
// Supports: 8BitDo Ultimate 3-mode Controller for Xbox (wired for Xbox Series X|S)
// VID: 0x2DC8  PID: 0x901B  (BLE / Android pairing mode)
//
// HID Report layout (Report ID 0x03, 11 bytes total):
//
//   Byte 0:  Report ID (0x03) — always skipped
//   Byte 1:  Hat switch (bits 0-3, 4-bit, 0=N/1=NE/.../7=NW/8=center) + padding (bits 4-7)
//   Byte 2:  Left Stick X   (0x00–0xFF)
//   Byte 3:  Left Stick Y   (0x00–0xFF)
//   Byte 4:  Right Stick X  (0x00–0xFF)  [HID usage Z]
//   Byte 5:  Right Stick Y  (0x00–0xFF)  [HID usage Rz]
//   Byte 6:  Left Trigger   (0x00–0xFF)  [Simulation Controls: Accelerator 0xC4]
//   Byte 7:  Right Trigger  (0x00–0xFF)  [Simulation Controls: Brake 0xC5]
//   Byte 8:  Buttons 1-8    (1 bit each, LSB = Button 1)
//   Byte 9:  Buttons 9-16   (1 bit each, LSB = Button 9)
//   Byte 10: Battery level  (0x00–0x64)
//
// Button mapping (sequential, no gaps):
//   Bit  0 (Button 1):  A          → JP_BUTTON_B1
//   Bit  1 (Button 2):  B          → JP_BUTTON_B2
//   Bit  2 (Button 3):  X          → JP_BUTTON_B3
//   Bit  3 (Button 4):  Y          → JP_BUTTON_B4
//   Bit  4 (Button 5):  LB         → JP_BUTTON_L1
//   Bit  5 (Button 6):  RB         → JP_BUTTON_R1
//   Bit  6 (Button 7):  LT digital → JP_BUTTON_L2
//   Bit  7 (Button 8):  RT digital → JP_BUTTON_R2
//   Bit  8 (Button 9):  Select     → JP_BUTTON_S1
//   Bit  9 (Button 10): Start      → JP_BUTTON_S2
//   Bit 10 (Button 11): L3         → JP_BUTTON_L3
//   Bit 11 (Button 12): R3         → JP_BUTTON_R3
//   Bit 12 (Button 13): Guide/Home → JP_BUTTON_A1
//   Bit 13 (Button 14): Paddle P1  → JP_BUTTON_A2  (back paddle, top-right)
//   Bit 14 (Button 15): Paddle P2  → JP_BUTTON_A3  (back paddle, top-left)
//   Bit 15 (Button 16): Paddle P3  → JP_BUTTON_A4  (back paddle, bottom — if present)

#include "bthid_8bitdo_ultimate.h"
#include "bt/bthid/bthid.h"
#include "bt/transport/bt_transport.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// CONSTANTS
// ============================================================================

#define BITDO_ULTIMATE_VID          0x2DC8
#define BITDO_ULTIMATE_PID_BLE      0x901B

#define REPORT_ID_GAMEPAD           0x03
#define REPORT_ID_RUMBLE            0x05
#define REPORT_MIN_LEN              10   // Minimum bytes expected (excluding report ID)

// Byte offsets within the report (after the Report ID byte 0 is skipped)
#define OFFSET_HAT                  1
#define OFFSET_LX                   2
#define OFFSET_LY                   3
#define OFFSET_RX                   4
#define OFFSET_RY                   5
#define OFFSET_LT                   6
#define OFFSET_RT                   7
#define OFFSET_BUTTONS_LO           8    // Buttons 1-8
#define OFFSET_BUTTONS_HI           9    // Buttons 9-16
#define OFFSET_BATTERY              10

// Hat switch: 4-bit value, 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW, 8+=center
#define HAT_MASK                    0x0F

// ============================================================================
// HAT SWITCH LOOKUP
// packed dpad bits: bit0=up, bit1=right, bit2=down, bit3=left
// ============================================================================

static const uint8_t HAT_TO_DPAD[] = {
    0b0001,  // 0: N  → Up
    0b0011,  // 1: NE → Up + Right
    0b0010,  // 2: E  → Right
    0b0110,  // 3: SE → Down + Right
    0b0100,  // 4: S  → Down
    0b1100,  // 5: SW → Down + Left
    0b1000,  // 6: W  → Left
    0b1001,  // 7: NW → Up + Left
    0b0000,  // 8: Center (released)
};

// ============================================================================
// DRIVER STATE
// ============================================================================

typedef struct {
    input_event_t event;
    bool initialized;
    uint8_t rumble_left;
    uint8_t rumble_right;
} bitdo_data_t;

static bitdo_data_t device_data[BTHID_MAX_DEVICES];

// ============================================================================
// ANALOG SCALING
// Maps 0x00–0xFF to 1–255 (Joypad OS internal range, 128 = center)
// ============================================================================

static inline uint8_t scale_axis(uint8_t raw)
{
    // Avoid returning 0 (reserved as "no data" in some paths)
    return raw == 0 ? 1 : raw;
}

// ============================================================================
// DRIVER: match
// Only claim this device — don't fall through to generic gamepad driver
// ============================================================================

static bool bitdo_match(const char* device_name, const uint8_t* class_of_device,
                        uint16_t vendor_id, uint16_t product_id, bool is_ble)
{
    (void)device_name;
    (void)class_of_device;

    return (vendor_id == BITDO_ULTIMATE_VID &&
            product_id == BITDO_ULTIMATE_PID_BLE &&
            is_ble);
}

// ============================================================================
// DRIVER: init
// ============================================================================

static bool bitdo_init(bthid_device_t* device)
{
    printf("[8BITDO_ULTIMATE] Init: %s VID=0x%04X PID=0x%04X\n",
           device->name, device->vendor_id, device->product_id);

    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (!device_data[i].initialized) {
            init_input_event(&device_data[i].event);
            device_data[i].initialized   = true;
            device_data[i].rumble_left   = 0;
            device_data[i].rumble_right  = 0;

            device_data[i].event.type      = INPUT_TYPE_GAMEPAD;
            device_data[i].event.transport = INPUT_TRANSPORT_BT_BLE;
            device_data[i].event.dev_addr  = device->conn_index;
            device_data[i].event.instance  = 0;

            device->driver_data = &device_data[i];
            return true;
        }
    }

    printf("[8BITDO_ULTIMATE] No free device slots!\n");
    return false;
}

// ============================================================================
// DRIVER: process_report
// ============================================================================

static void bitdo_process_report(bthid_device_t* device, const uint8_t* data, uint16_t len)
{
    bitdo_data_t* gp = (bitdo_data_t*)device->driver_data;
    if (!gp) return;

    // Must be a gamepad input report
    if (len < 1 || data[0] != REPORT_ID_GAMEPAD) return;

    // Need at least up to the button bytes
    if (len <= OFFSET_BUTTONS_HI) {
        printf("[8BITDO_ULTIMATE] Report too short: %d bytes\n", len);
        return;
    }

    uint32_t buttons = 0;

    // --- Hat switch → D-pad ---
    uint8_t hat = data[OFFSET_HAT] & HAT_MASK;
    uint8_t dpad = hat <= 8 ? HAT_TO_DPAD[hat] : 0;
    if (dpad & 0x01) buttons |= JP_BUTTON_DU;
    if (dpad & 0x02) buttons |= JP_BUTTON_DR;
    if (dpad & 0x04) buttons |= JP_BUTTON_DD;
    if (dpad & 0x08) buttons |= JP_BUTTON_DL;

    // --- Buttons 1-8 (byte 8) ---
    uint8_t lo = data[OFFSET_BUTTONS_LO];
    if (lo & 0x01) buttons |= JP_BUTTON_B1;   // A
    if (lo & 0x02) buttons |= JP_BUTTON_B2;   // B
    if (lo & 0x04) buttons |= JP_BUTTON_B3;   // X
    if (lo & 0x08) buttons |= JP_BUTTON_B4;   // Y
    if (lo & 0x10) buttons |= JP_BUTTON_L1;   // LB
    if (lo & 0x20) buttons |= JP_BUTTON_R1;   // RB
    if (lo & 0x40) buttons |= JP_BUTTON_L2;   // LT (digital)
    if (lo & 0x80) buttons |= JP_BUTTON_R2;   // RT (digital)

    // --- Buttons 9-16 (byte 9) ---
    uint8_t hi = data[OFFSET_BUTTONS_HI];
    if (hi & 0x01) buttons |= JP_BUTTON_S1;   // Select/Back
    if (hi & 0x02) buttons |= JP_BUTTON_S2;   // Start/Menu
    if (hi & 0x04) buttons |= JP_BUTTON_L3;   // L3
    if (hi & 0x08) buttons |= JP_BUTTON_R3;   // R3
    if (hi & 0x10) buttons |= JP_BUTTON_A1;   // Guide/Home
    if (hi & 0x20) buttons |= JP_BUTTON_A2;   // Back Paddle P1 (top-right)
    if (hi & 0x40) buttons |= JP_BUTTON_A3;   // Back Paddle P2 (top-left)
    if (hi & 0x80) buttons |= JP_BUTTON_A4;   // Back Paddle P3 (bottom, if present)

    // --- Analog axes ---
    gp->event.analog[ANALOG_LX] = scale_axis(data[OFFSET_LX]);
    gp->event.analog[ANALOG_LY] = scale_axis(data[OFFSET_LY]);
    gp->event.analog[ANALOG_RX] = scale_axis(data[OFFSET_RX]);
    gp->event.analog[ANALOG_RY] = scale_axis(data[OFFSET_RY]);

    // --- Analog triggers ---
    // Simulation Controls: C4=Accelerator=LT, C5=Brake=RT
    gp->event.analog[ANALOG_L2] = data[OFFSET_RT];
    gp->event.analog[ANALOG_R2] = data[OFFSET_LT];

    gp->event.buttons      = buttons;
    gp->event.button_count = 16;

    router_submit_input(&gp->event);
}

// ============================================================================
// DRIVER: task (rumble output)
// Report ID 0x05, 4 bytes: [strong, weak, left_trigger, right_trigger]
// Values 0x00–0x64 (0–100)
// ============================================================================

static void bitdo_task(bthid_device_t* device)
{
    bitdo_data_t* gp = (bitdo_data_t*)device->driver_data;
    if (!gp) return;

    int player_idx = find_player_index(gp->event.dev_addr, gp->event.instance);
    if (player_idx < 0) return;

    feedback_state_t* fb = feedback_get_state(player_idx);
    if (!fb || !fb->rumble_dirty) return;

    uint8_t left  = fb->rumble.left;
    uint8_t right = fb->rumble.right;

    if (left != gp->rumble_left || right != gp->rumble_right) {
        // Scale 0-255 → 0-100
        uint8_t strong = ((uint16_t)left  * 100) / 255;
        uint8_t weak   = ((uint16_t)right * 100) / 255;

        uint8_t buf[4] = {
            strong,  // Strong motor (left/grip)
            weak,    // Weak motor (right/grip)
            0,       // Left trigger motor (unused)
            0,       // Right trigger motor (unused)
        };
        bthid_send_output_report(device->conn_index, REPORT_ID_RUMBLE, buf, sizeof(buf));

        gp->rumble_left  = left;
        gp->rumble_right = right;
    }

    feedback_clear_dirty(player_idx);
}

// ============================================================================
// DRIVER: disconnect
// ============================================================================

static void bitdo_disconnect(bthid_device_t* device)
{
    printf("[8BITDO_ULTIMATE] Disconnect: %s\n", device->name);

    bitdo_data_t* gp = (bitdo_data_t*)device->driver_data;
    if (gp) {
        router_device_disconnected(gp->event.dev_addr, gp->event.instance);
        remove_players_by_address(gp->event.dev_addr, gp->event.instance);
        init_input_event(&gp->event);
        gp->initialized = false;
    }
}

// ============================================================================
// DRIVER STRUCT + REGISTRATION
// ============================================================================

const bthid_driver_t bthid_8bitdo_ultimate_driver = {
    .name           = "8BitDo Ultimate 3-mode Xbox",
    .match          = bitdo_match,
    .init           = bitdo_init,
    .process_report = bitdo_process_report,
    .task           = bitdo_task,
    .disconnect     = bitdo_disconnect,
};

void bthid_8bitdo_ultimate_register(void)
{
    bthid_register_driver(&bthid_8bitdo_ultimate_driver);
}
