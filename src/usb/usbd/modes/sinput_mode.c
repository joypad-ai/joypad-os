// sinput_mode.c - SInput USB device mode
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// SInput protocol implementation for SDL/Steam compatibility.
// Based on Handheld Legend's SInput HID specification.

#include "tusb.h"
#include "../usbd_mode.h"
#include "../usbd.h"
#include "descriptors/sinput_descriptors.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include "pico/unique_id.h"
#include <string.h>

#if (defined(CONFIG_USB_HOST) || defined(CONFIG_USB)) && !defined(DISABLE_USB_HOST)
#include "usb/usbh/hid/hid_registry.h"
extern int hid_get_ctrl_type(uint8_t dev_addr, uint8_t instance);
#endif

#ifdef ENABLE_BTSTACK
#include "bt/bthid/bthid.h"
#endif

// ============================================================================
// SINPUT FACE STYLES
// ============================================================================

// Face style values (byte 5, upper 3 bits) - per SDL/SInput spec
#define SINPUT_FACE_XBOX         0  // ABXY (default)
#define SINPUT_FACE_GAMECUBE     2  // AXBY
#define SINPUT_FACE_NINTENDO     3  // BAYX
#define SINPUT_FACE_SONY         4  // Sony (Cross/Circle/Square/Triangle)

// Gamepad physical type values (byte 4) - per SInput spec
#define SINPUT_TYPE_UNKNOWN      0
#define SINPUT_TYPE_STANDARD     1
#define SINPUT_TYPE_XBOX360      2
#define SINPUT_TYPE_XBOXONE      3
#define SINPUT_TYPE_PS3          4
#define SINPUT_TYPE_PS4          5
#define SINPUT_TYPE_PS5          6
#define SINPUT_TYPE_SWITCH_PRO   7
#define SINPUT_TYPE_JOYCON_L     8
#define SINPUT_TYPE_JOYCON_R     9
#define SINPUT_TYPE_JOYCON_PAIR  10
#define SINPUT_TYPE_GAMECUBE     11

// ============================================================================
// STATE
// ============================================================================

static sinput_report_t sinput_report;
static uint8_t rumble_left = 0;
static uint8_t rumble_right = 0;
static bool rumble_dirty = false;  // Only send feedback when changed
static uint8_t player_led = 0;
static bool player_led_dirty = false;
static uint8_t rgb_r = 0;
static uint8_t rgb_g = 0;
static uint8_t rgb_b = 0;
static bool rgb_dirty = false;
static bool feature_request_pending = false;
static uint8_t cached_face_style = SINPUT_FACE_XBOX;
static uint8_t cached_gamepad_type = SINPUT_TYPE_STANDARD;
static int16_t last_dev_addr = -1;  // Track connected device for auto feature report

// ============================================================================
// CONVERSION HELPERS
// ============================================================================

// Convert 8-bit axis (0-255, 128=center) to 16-bit signed (-32768 to 32767)
static inline int16_t convert_axis_to_s16(uint8_t value)
{
    return ((int16_t)value - 128) * 256;
}

// Convert 8-bit trigger (0-255) to 16-bit (0 to 32767)
static inline int16_t convert_trigger_to_s16(uint8_t value)
{
    return ((int16_t)value * 32767) / 255;
}

// Convert Joypad buttons to SInput button mask (32 buttons)
static uint32_t convert_buttons(uint32_t buttons)
{
    uint32_t sinput_buttons = 0;

    // Face buttons (Byte 0)
    if (buttons & JP_BUTTON_B1) sinput_buttons |= SINPUT_MASK_SOUTH;   // Cross/A
    if (buttons & JP_BUTTON_B2) sinput_buttons |= SINPUT_MASK_EAST;    // Circle/B
    if (buttons & JP_BUTTON_B3) sinput_buttons |= SINPUT_MASK_WEST;    // Square/X
    if (buttons & JP_BUTTON_B4) sinput_buttons |= SINPUT_MASK_NORTH;   // Triangle/Y

    // D-pad (Byte 0)
    if (buttons & JP_BUTTON_DU) sinput_buttons |= SINPUT_MASK_DU;
    if (buttons & JP_BUTTON_DD) sinput_buttons |= SINPUT_MASK_DD;
    if (buttons & JP_BUTTON_DL) sinput_buttons |= SINPUT_MASK_DL;
    if (buttons & JP_BUTTON_DR) sinput_buttons |= SINPUT_MASK_DR;

    // Shoulders and triggers (Byte 1)
    if (buttons & JP_BUTTON_L1) sinput_buttons |= SINPUT_MASK_L1;
    if (buttons & JP_BUTTON_R1) sinput_buttons |= SINPUT_MASK_R1;
    if (buttons & JP_BUTTON_L2) sinput_buttons |= SINPUT_MASK_L2;
    if (buttons & JP_BUTTON_R2) sinput_buttons |= SINPUT_MASK_R2;

    // Stick clicks (Byte 1)
    if (buttons & JP_BUTTON_L3) sinput_buttons |= SINPUT_MASK_L3;
    if (buttons & JP_BUTTON_R3) sinput_buttons |= SINPUT_MASK_R3;

    // System buttons (Byte 2)
    if (buttons & JP_BUTTON_S1) sinput_buttons |= SINPUT_MASK_BACK;    // Select/Back
    if (buttons & JP_BUTTON_S2) sinput_buttons |= SINPUT_MASK_START;   // Start/Options
    if (buttons & JP_BUTTON_A1) sinput_buttons |= SINPUT_MASK_GUIDE;   // Home/Guide
    if (buttons & JP_BUTTON_A2) sinput_buttons |= SINPUT_MASK_CAPTURE; // Capture/Share

    // Extended buttons (paddles) - map L4/R4 if available
    if (buttons & JP_BUTTON_L4) sinput_buttons |= SINPUT_MASK_L_PADDLE1;
    if (buttons & JP_BUTTON_R4) sinput_buttons |= SINPUT_MASK_R_PADDLE1;

    return sinput_buttons;
}

// ============================================================================
// DEVICE DETECTION
// ============================================================================

// Determine face style and gamepad type from device address, instance, and transport
static void update_device_info(uint8_t dev_addr, int8_t instance, input_transport_t transport)
{
#if (defined(CONFIG_USB_HOST) || defined(CONFIG_USB)) && !defined(DISABLE_USB_HOST)
    if (transport == INPUT_TRANSPORT_USB) {
        int ctrl_type = hid_get_ctrl_type(dev_addr, instance);
        switch (ctrl_type) {
            case CONTROLLER_DUALSHOCK3:
                cached_face_style = SINPUT_FACE_SONY;
                cached_gamepad_type = SINPUT_TYPE_PS3;
                return;
            case CONTROLLER_DUALSHOCK4:
            case CONTROLLER_PSCLASSIC:
                cached_face_style = SINPUT_FACE_SONY;
                cached_gamepad_type = SINPUT_TYPE_PS4;
                return;
            case CONTROLLER_DUALSENSE:
                cached_face_style = SINPUT_FACE_SONY;
                cached_gamepad_type = SINPUT_TYPE_PS5;
                return;
            case CONTROLLER_SWITCH:
            case CONTROLLER_SWITCH2:
                cached_face_style = SINPUT_FACE_NINTENDO;
                cached_gamepad_type = SINPUT_TYPE_SWITCH_PRO;
                return;
            case CONTROLLER_GAMECUBE:
                cached_face_style = SINPUT_FACE_GAMECUBE;
                cached_gamepad_type = SINPUT_TYPE_GAMECUBE;
                return;
            default:
                cached_face_style = SINPUT_FACE_XBOX;
                cached_gamepad_type = SINPUT_TYPE_STANDARD;
                return;
        }
    }
#endif

#ifdef ENABLE_BTSTACK
    // Try BT lookup — some BT drivers don't set transport on the event,
    // so attempt this for any non-USB transport (including NONE)
    if (transport != INPUT_TRANSPORT_USB) {
        bthid_device_t* bt_dev = bthid_get_device(dev_addr);
        if (bt_dev) {
            switch (bt_dev->vendor_id) {
                case 0x054C:  // Sony
                    cached_face_style = SINPUT_FACE_SONY;
                    if (bt_dev->product_id == 0x0268) {
                        cached_gamepad_type = SINPUT_TYPE_PS3;
                    } else if (bt_dev->product_id == 0x0CE6 ||
                               bt_dev->product_id == 0x0DF2) {
                        cached_gamepad_type = SINPUT_TYPE_PS5;
                    } else {
                        cached_gamepad_type = SINPUT_TYPE_PS4;
                    }
                    return;
                case 0x057E:  // Nintendo
                    cached_face_style = SINPUT_FACE_NINTENDO;
                    cached_gamepad_type = SINPUT_TYPE_SWITCH_PRO;
                    return;
                case 0x045E:  // Microsoft
                    cached_face_style = SINPUT_FACE_XBOX;
                    cached_gamepad_type = SINPUT_TYPE_XBOXONE;
                    return;
                default:
                    cached_face_style = SINPUT_FACE_XBOX;
                    cached_gamepad_type = SINPUT_TYPE_STANDARD;
                    return;
            }
        }
    }
#endif
}

// ============================================================================
// MODE INTERFACE IMPLEMENTATION
// ============================================================================

static void sinput_mode_init(void)
{
    memset(&sinput_report, 0, sizeof(sinput_report));

    // Set report ID
    sinput_report.report_id = SINPUT_REPORT_ID_INPUT;

    // Set neutral analog values (center = 0 for signed 16-bit)
    sinput_report.lx = 0;
    sinput_report.ly = 0;
    sinput_report.rx = 0;
    sinput_report.ry = 0;
    sinput_report.lt = 0;
    sinput_report.rt = 0;

    // Clear rumble state
    rumble_left = 0;
    rumble_right = 0;
}

static bool sinput_mode_is_ready(void)
{
    return tud_hid_ready();
}

static bool sinput_mode_send_report(uint8_t player_index,
                                     const input_event_t* event,
                                     const profile_output_t* profile_out,
                                     uint32_t buttons)
{
    (void)player_index;

    // Update device face style from connected controller
    update_device_info(event->dev_addr, event->instance, event->transport);

    // Send feature report automatically when a new device connects
    if (event->dev_addr != last_dev_addr) {
        last_dev_addr = event->dev_addr;
        feature_request_pending = true;
    }

    // Convert buttons to SInput format (32-bit across 4 bytes)
    uint32_t sinput_buttons = convert_buttons(buttons);
    sinput_report.buttons[0] = (sinput_buttons >>  0) & 0xFF;
    sinput_report.buttons[1] = (sinput_buttons >>  8) & 0xFF;
    sinput_report.buttons[2] = (sinput_buttons >> 16) & 0xFF;
    sinput_report.buttons[3] = (sinput_buttons >> 24) & 0xFF;

    // Convert analog sticks (8-bit 0-255 → 16-bit signed)
    sinput_report.lx = convert_axis_to_s16(profile_out->left_x);
    sinput_report.ly = convert_axis_to_s16(profile_out->left_y);
    sinput_report.rx = convert_axis_to_s16(profile_out->right_x);
    sinput_report.ry = convert_axis_to_s16(profile_out->right_y);

    // Convert triggers (8-bit 0-255 → 16-bit 0-32767)
    sinput_report.lt = convert_trigger_to_s16(profile_out->l2_analog);
    sinput_report.rt = convert_trigger_to_s16(profile_out->r2_analog);

    // IMU timestamp (microseconds since boot)
    sinput_report.imu_timestamp = time_us_32();

    // IMU data - set to neutral (no IMU hardware yet)
    sinput_report.accel_x = 0;
    sinput_report.accel_y = 0;
    sinput_report.accel_z = 0;  // Could set to ~1G if simulating gravity
    sinput_report.gyro_x = 0;
    sinput_report.gyro_y = 0;
    sinput_report.gyro_z = 0;

    // Send report (skip report_id byte since TinyUSB handles it)
    return tud_hid_report(SINPUT_REPORT_ID_INPUT,
                          ((uint8_t*)&sinput_report) + 1,
                          sizeof(sinput_report) - 1);
}

static void sinput_mode_handle_output(uint8_t report_id, const uint8_t* data, uint16_t len)
{
    printf("[sinput] handle_output: report_id=%d len=%d data[0]=%d\n", report_id, len, data[0]);

    // Handle report ID in buffer (interrupt OUT endpoint case)
    // When report_id=0, the actual report ID may be the first byte of data
    if (report_id == 0 && len > 0 && data[0] == SINPUT_REPORT_ID_OUTPUT) {
        // Report ID is in buffer, skip it
        report_id = data[0];
        data = data + 1;
        len = len - 1;
        printf("[sinput] Extracted report_id from buffer: %d\n", report_id);
    }

    // Handle output report (rumble, LEDs)
    if (report_id != SINPUT_REPORT_ID_OUTPUT || len < 2) {
        printf("[sinput] Ignoring: expected report_id=%d\n", SINPUT_REPORT_ID_OUTPUT);
        return;
    }

    uint8_t command = data[0];
    printf("[sinput] command=%d data=[%d,%d,%d,%d,%d,%d]\n",
           command, data[0], data[1], data[2], data[3], data[4], data[5]);

    switch (command) {
        case SINPUT_CMD_HAPTIC:
            // Haptic command format (Type 2):
            // data[1] = type (should be 2)
            // data[2] = left amplitude
            // data[3] = left brake
            // data[4] = right amplitude
            // data[5] = right brake
            if (len >= 6 && data[1] == 2) {
                uint8_t new_left = data[2];
                uint8_t new_right = data[4];
                // Only mark dirty if values actually changed
                if (new_left != rumble_left || new_right != rumble_right) {
                    rumble_left = new_left;
                    rumble_right = new_right;
                    rumble_dirty = true;
                    printf("[sinput] Rumble changed: L=%d R=%d\n", rumble_left, rumble_right);
                }
            }
            break;

        case SINPUT_CMD_PLAYER_LED:
            // Player LED command: data[1] = player number (1-4)
            if (len >= 2) {
                uint8_t new_led = data[1];
                if (new_led != player_led) {
                    player_led = new_led;
                    player_led_dirty = true;
                    printf("[sinput] Player LED changed: %d\n", player_led);
                }
            }
            break;

        case SINPUT_CMD_FEATURES:
            // Feature request - queue a response
            printf("[sinput] Feature request received\n");
            feature_request_pending = true;
            break;

        case SINPUT_CMD_RGB_LED:
            // RGB LED command: data[1] = R, data[2] = G, data[3] = B
            if (len >= 4) {
                if (data[1] != rgb_r || data[2] != rgb_g || data[3] != rgb_b) {
                    rgb_r = data[1];
                    rgb_g = data[2];
                    rgb_b = data[3];
                    rgb_dirty = true;
                    printf("[sinput] RGB LED changed: R=%d G=%d B=%d\n", rgb_r, rgb_g, rgb_b);
                }
            }
            break;

        default:
            break;
    }
}

static uint8_t sinput_mode_get_rumble(void)
{
    // Return max of left/right rumble
    return (rumble_left > rumble_right) ? rumble_left : rumble_right;
}

static bool sinput_mode_get_feedback(output_feedback_t* fb)
{
    if (!fb) return false;
    if (!rumble_dirty && !rgb_dirty && !player_led_dirty) return false;

    fb->rumble_left = rumble_left;
    fb->rumble_right = rumble_right;
    fb->led_player = player_led;
    fb->led_r = rgb_r;
    fb->led_g = rgb_g;
    fb->led_b = rgb_b;
    fb->dirty = true;

    rumble_dirty = false;
    rgb_dirty = false;
    player_led_dirty = false;

    return true;
}

static const uint8_t* sinput_mode_get_device_descriptor(void)
{
    return (const uint8_t*)&sinput_device_descriptor;
}

static const uint8_t* sinput_mode_get_config_descriptor(void)
{
    return sinput_config_descriptor;
}

static const uint8_t* sinput_mode_get_report_descriptor(void)
{
    return sinput_report_descriptor;
}

// Send feature response when pending
static void sinput_mode_task(void)
{
    if (!feature_request_pending) return;
    if (!tud_hid_ready()) return;

    feature_request_pending = false;

    // Refresh device info from player 0 before building response
    if (playersCount > 0 && players[0].dev_addr >= 0) {
        update_device_info((uint8_t)players[0].dev_addr,
                           (int8_t)players[0].instance,
                           players[0].transport);
    }

    // Build feature response (24 bytes per SInput spec)
    // Bytes 0-1:   Protocol version (uint16 LE)
    // Byte 2:      Capability flags 1 (bit 0=rumble, bit 1=player LED, bit 2=accel, bit 3=gyro)
    // Byte 3:      Capability flags 2 (bit 1=RGB LED)
    // Byte 4:      Gamepad type (1=standard)
    // Byte 5:      Upper 3 bits=face style (1=Xbox), lower 5 bits=sub product
    // Bytes 6-7:   Polling rate micros (uint16 LE) - 8000us = 125Hz
    // Bytes 8-9:   Accel range (uint16 LE) - 0 = not supported
    // Bytes 10-11: Gyro range (uint16 LE) - 0 = not supported
    // Bytes 12-15: Button usage masks (1 byte per button byte, bits = active buttons)
    // Byte 16:     Touchpad count
    // Byte 17:     Touchpad finger count
    // Bytes 18-23: MAC address / serial number (6 bytes)
    uint8_t feature_response[24] = {0};

    // Protocol version 1.0
    feature_response[0] = 0x00;
    feature_response[1] = 0x01;

    // Capability flags 1: rumble + player LED supported
    feature_response[2] = 0x03;  // bit 0 = rumble, bit 1 = player LED

    // Capability flags 2: RGB LED supported
    feature_response[3] = 0x02;  // bit 1 = RGB LED

    // Gamepad type (from connected device)
    feature_response[4] = cached_gamepad_type;

    // Face style (from connected device) | sub product (0)
    feature_response[5] = (cached_face_style << 5);

    // Polling rate: 8000 microseconds (125Hz)
    feature_response[6] = 0x40;  // 8000 & 0xFF
    feature_response[7] = 0x1F;  // 8000 >> 8

    // Accel/Gyro ranges: 0 (not supported on adapter)
    feature_response[8] = 0;
    feature_response[9] = 0;
    feature_response[10] = 0;
    feature_response[11] = 0;

    // Button usage masks: which buttons are active per byte
    // Byte 0: EAST|SOUTH|NORTH|WEST|DU|DD|DL|DR = all 8 bits
    feature_response[12] = 0xFF;
    // Byte 1: L3|R3|L1|R1|L2|R2|L_PADDLE1|R_PADDLE1 = all 8 bits
    feature_response[13] = 0xFF;
    // Byte 2: START|BACK|GUIDE|CAPTURE = lower 4 bits
    feature_response[14] = 0x0F;
    // Byte 3: no power/misc buttons
    feature_response[15] = 0x00;

    // Touchpad: not supported
    feature_response[16] = 0;  // touchpad count
    feature_response[17] = 0;  // touchpad finger count

    // Serial number from board unique ID (last 6 bytes of 8-byte ID)
    pico_unique_board_id_t board_id;
    pico_get_unique_board_id(&board_id);
    feature_response[18] = board_id.id[2];
    feature_response[19] = board_id.id[3];
    feature_response[20] = board_id.id[4];
    feature_response[21] = board_id.id[5];
    feature_response[22] = board_id.id[6];
    feature_response[23] = board_id.id[7];

    printf("[sinput] Sending feature response (24 bytes)\n");
    tud_hid_report(SINPUT_REPORT_ID_FEATURES, feature_response, sizeof(feature_response));
}

// ============================================================================
// MODE EXPORT
// ============================================================================

const usbd_mode_t sinput_mode = {
    .name = "SInput",
    .mode = USB_OUTPUT_MODE_SINPUT,

    .get_device_descriptor = sinput_mode_get_device_descriptor,
    .get_config_descriptor = sinput_mode_get_config_descriptor,
    .get_report_descriptor = sinput_mode_get_report_descriptor,

    .init = sinput_mode_init,
    .send_report = sinput_mode_send_report,
    .is_ready = sinput_mode_is_ready,

    .handle_output = sinput_mode_handle_output,
    .get_rumble = sinput_mode_get_rumble,
    .get_feedback = sinput_mode_get_feedback,
    .get_report = NULL,
    .get_class_driver = NULL,
    .task = sinput_mode_task,
};
