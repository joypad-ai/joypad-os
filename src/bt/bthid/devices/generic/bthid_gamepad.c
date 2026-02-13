// bthid_gamepad.c - Generic Bluetooth Gamepad Driver
// Handles basic HID gamepads over Bluetooth
// This is a fallback driver for gamepads without a specific driver

#include "bthid_gamepad.h"
#include "bt/bthid/bthid.h"
#include "bt/transport/bt_transport.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include <string.h>
#include <stdio.h>

// ============================================================================
// DRIVER DATA
// ============================================================================

typedef struct {
    input_event_t event;        // Current input state
    bool initialized;
} bthid_gamepad_data_t;

static bthid_gamepad_data_t gamepad_data[BTHID_MAX_DEVICES];

// ============================================================================
// DRIVER IMPLEMENTATION
// ============================================================================

static bool gamepad_match(const char* device_name, const uint8_t* class_of_device,
                          uint16_t vendor_id, uint16_t product_id, bool is_ble)
{
    (void)device_name;
    (void)vendor_id;   // Generic driver doesn't use VID/PID
    (void)product_id;
    (void)is_ble;

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

            // Set device info
            gamepad_data[i].event.type = INPUT_TYPE_GAMEPAD;
            gamepad_data[i].event.transport = INPUT_TRANSPORT_BT_CLASSIC;
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

    // Generic HID gamepad report parsing
    // Most gamepads follow a similar structure:
    // - First few bytes: buttons (varies)
    // - Following bytes: axes (usually 4 bytes for 2 sticks)
    //
    // This is a basic implementation - specific controllers will
    // have their own drivers with proper parsing

    if (len < 4) {
        return;
    }

    // Try to extract basic button/axis data
    // This is a very generic approach - real gamepads vary widely

    // Assume simple layout:
    // data[0-1]: buttons
    // data[2]: left stick X
    // data[3]: left stick Y
    // data[4]: right stick X (if available)
    // data[5]: right stick Y (if available)

    uint32_t raw_buttons = 0;
    if (len >= 1) raw_buttons |= data[0];
    if (len >= 2) raw_buttons |= (uint32_t)data[1] << 8;

    // Map generic buttons to USBR buttons
    // This is a rough mapping - specific drivers should override
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
