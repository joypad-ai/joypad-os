// ps3_mode.c - PlayStation 3 DualShock 3 USB device mode
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "tusb.h"
#include "../usbd_mode.h"
#include "../usbd.h"
#include "descriptors/ps3_descriptors.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include "platform/platform.h"
#include <string.h>

// ============================================================================
// STATE
// ============================================================================

static ps3_in_report_t ps3_report;
static ps3_out_report_t ps3_output;
static bool ps3_output_available = false;
static uint8_t ps3_ef_byte = 0;  // Mode byte echoed from SET_REPORT(0xEF)
static ps3_pairing_info_t ps3_pairing = {0};  // Pairing info with BT addresses

// ============================================================================
// MODE INTERFACE IMPLEMENTATION
// ============================================================================

static void ps3_mode_init(void)
{
    ps3_init_report(&ps3_report);
    memset(&ps3_output, 0, sizeof(ps3_out_report_t));
    ps3_output_available = false;
    ps3_ef_byte = 0;

    // Generate plausible BT addresses from board unique ID
    memset(&ps3_pairing, 0, sizeof(ps3_pairing));
    uint8_t board_id[8];
    platform_get_unique_id(board_id, sizeof(board_id));
    // Device address: bytes 0-5 of board ID
    ps3_pairing.device_address[0] = 0x00;  // Leading zero
    for (int i = 0; i < 6; i++) {
        ps3_pairing.device_address[1 + i] = board_id[i];
    }
    // Host address: bytes 2-7 XOR'd for differentiation
    ps3_pairing.host_address[0] = 0x00;  // Leading zero
    for (int i = 0; i < 6; i++) {
        ps3_pairing.host_address[1 + i] = board_id[i] ^ 0xAA;
    }
}

static bool ps3_mode_is_ready(void)
{
    return tud_hid_ready();
}

static bool ps3_mode_send_report(uint8_t player_index,
                                  const input_event_t* event,
                                  const profile_output_t* profile_out,
                                  uint32_t buttons)
{
    (void)player_index;

    // Digital buttons byte 0
    ps3_report.buttons[0] = 0;
    if (buttons & JP_BUTTON_S1) ps3_report.buttons[0] |= PS3_BTN_SELECT;
    if (buttons & JP_BUTTON_L3) ps3_report.buttons[0] |= PS3_BTN_L3;
    if (buttons & JP_BUTTON_R3) ps3_report.buttons[0] |= PS3_BTN_R3;
    if (buttons & JP_BUTTON_S2) ps3_report.buttons[0] |= PS3_BTN_START;
    if (buttons & JP_BUTTON_DU) ps3_report.buttons[0] |= PS3_BTN_DPAD_UP;
    if (buttons & JP_BUTTON_DR) ps3_report.buttons[0] |= PS3_BTN_DPAD_RIGHT;
    if (buttons & JP_BUTTON_DD) ps3_report.buttons[0] |= PS3_BTN_DPAD_DOWN;
    if (buttons & JP_BUTTON_DL) ps3_report.buttons[0] |= PS3_BTN_DPAD_LEFT;

    // Digital buttons byte 1
    ps3_report.buttons[1] = 0;
    if (buttons & JP_BUTTON_L2) ps3_report.buttons[1] |= PS3_BTN_L2;
    if (buttons & JP_BUTTON_R2) ps3_report.buttons[1] |= PS3_BTN_R2;
    if (buttons & JP_BUTTON_L1) ps3_report.buttons[1] |= PS3_BTN_L1;
    if (buttons & JP_BUTTON_R1) ps3_report.buttons[1] |= PS3_BTN_R1;
    if (buttons & JP_BUTTON_B4) ps3_report.buttons[1] |= PS3_BTN_TRIANGLE;
    if (buttons & JP_BUTTON_B2) ps3_report.buttons[1] |= PS3_BTN_CIRCLE;
    if (buttons & JP_BUTTON_B1) ps3_report.buttons[1] |= PS3_BTN_CROSS;
    if (buttons & JP_BUTTON_B3) ps3_report.buttons[1] |= PS3_BTN_SQUARE;

    // Digital buttons byte 2 (PS button)
    ps3_report.buttons[2] = 0;
    if (buttons & JP_BUTTON_A1) ps3_report.buttons[2] |= PS3_BTN_PS;

    // Analog sticks (HID convention: 0=up, 255=down - no inversion needed)
    ps3_report.lx = profile_out->left_x;
    ps3_report.ly = profile_out->left_y;
    ps3_report.rx = profile_out->right_x;
    ps3_report.ry = profile_out->right_y;

    // Pressure-sensitive buttons - use actual pressure data if available
    if (profile_out->has_pressure) {
        // D-pad pressure
        ps3_report.pressure_up    = profile_out->pressure[0];
        ps3_report.pressure_right = profile_out->pressure[1];
        ps3_report.pressure_down  = profile_out->pressure[2];
        ps3_report.pressure_left  = profile_out->pressure[3];
        // Triggers/bumpers pressure
        ps3_report.pressure_l2    = profile_out->pressure[4];
        ps3_report.pressure_r2    = profile_out->pressure[5];
        ps3_report.pressure_l1    = profile_out->pressure[6];
        ps3_report.pressure_r1    = profile_out->pressure[7];
        // Face buttons pressure
        ps3_report.pressure_triangle = profile_out->pressure[8];
        ps3_report.pressure_circle   = profile_out->pressure[9];
        ps3_report.pressure_cross    = profile_out->pressure[10];
        ps3_report.pressure_square   = profile_out->pressure[11];
    } else {
        // Fall back to digital (0xFF pressed, 0x00 released)
        ps3_report.pressure_up    = (buttons & JP_BUTTON_DU) ? 0xFF : 0x00;
        ps3_report.pressure_right = (buttons & JP_BUTTON_DR) ? 0xFF : 0x00;
        ps3_report.pressure_down  = (buttons & JP_BUTTON_DD) ? 0xFF : 0x00;
        ps3_report.pressure_left  = (buttons & JP_BUTTON_DL) ? 0xFF : 0x00;
        ps3_report.pressure_l2    = profile_out->l2_analog;
        ps3_report.pressure_r2    = profile_out->r2_analog;
        ps3_report.pressure_l1    = (buttons & JP_BUTTON_L1) ? 0xFF : 0x00;
        ps3_report.pressure_r1    = (buttons & JP_BUTTON_R1) ? 0xFF : 0x00;
        ps3_report.pressure_triangle = (buttons & JP_BUTTON_B4) ? 0xFF : 0x00;
        ps3_report.pressure_circle   = (buttons & JP_BUTTON_B2) ? 0xFF : 0x00;
        ps3_report.pressure_cross    = (buttons & JP_BUTTON_B1) ? 0xFF : 0x00;
        ps3_report.pressure_square   = (buttons & JP_BUTTON_B3) ? 0xFF : 0x00;
    }

    // Motion data (SIXAXIS) - big-endian 16-bit values
    // Internal format is normalized to SInput convention: ±32767 = ±2000 dps (gyro), ±4g (accel)
    // PS3 expects: centered at 512, ±512 range for ±100 dps (gyro), ±2g (accel)
    if (event->has_motion) {
        // De-normalize gyro from SInput (±32767 = ±2000 dps) to DS3 (±512 = ±100 dps, centered at 512)
        // Conversion: raw = (normalized * 10240 / 32767) + 512
        int32_t gyro_raw = ((int32_t)event->gyro[2] * 10240) / 32767 + 512;
        // Clamp to valid DS3 range (10-bit: 0-1023)
        if (gyro_raw < 0) gyro_raw = 0;
        if (gyro_raw > 1023) gyro_raw = 1023;

        // De-normalize accel from SInput (±32767 = ±4g) to DS3 (±512 = ±2g, centered at 512)
        // Conversion: raw = (normalized * 1024 / 32767) + 512
        int32_t accel_x_raw = ((int32_t)event->accel[0] * 1024) / 32767 + 512;
        int32_t accel_y_raw = ((int32_t)event->accel[1] * 1024) / 32767 + 512;
        int32_t accel_z_raw = ((int32_t)event->accel[2] * 1024) / 32767 + 512;
        // Clamp to valid DS3 range (10-bit: 0-1023)
        if (accel_x_raw < 0) accel_x_raw = 0;
        if (accel_x_raw > 1023) accel_x_raw = 1023;
        if (accel_y_raw < 0) accel_y_raw = 0;
        if (accel_y_raw > 1023) accel_y_raw = 1023;
        if (accel_z_raw < 0) accel_z_raw = 0;
        if (accel_z_raw > 1023) accel_z_raw = 1023;

        ps3_report.accel_x = __builtin_bswap16((uint16_t)accel_x_raw);
        ps3_report.accel_y = __builtin_bswap16((uint16_t)accel_y_raw);
        ps3_report.accel_z = __builtin_bswap16((uint16_t)accel_z_raw);
        ps3_report.gyro_z  = __builtin_bswap16((uint16_t)gyro_raw);
    } else {
        // Neutral motion (center at 512 = 0x0200, big-endian = 0x0002)
        ps3_report.accel_x = PS3_SIXAXIS_MID_BE;
        ps3_report.accel_y = PS3_SIXAXIS_MID_BE;
        ps3_report.accel_z = PS3_SIXAXIS_MID_BE;
        ps3_report.gyro_z  = PS3_SIXAXIS_MID_BE;
    }

    // Send full report including report_id
    return tud_hid_report(0, &ps3_report, sizeof(ps3_report));
}

static void ps3_mode_handle_output(uint8_t report_id, const uint8_t* data, uint16_t len)
{
    (void)report_id;

    // Some hosts (like WebHID) may include report ID in buffer, some don't
    // Check if buffer starts with report ID 0x01 and skip it if so
    if (len == 49 && data[0] == 0x01) {
        data = data + 1;
        len = 48;
    }

    if (len >= sizeof(ps3_out_report_t)) {
        memcpy(&ps3_output, data, sizeof(ps3_out_report_t));
        ps3_output_available = true;
    }
}

static uint8_t ps3_mode_get_rumble(void)
{
    // PS3 has left (large) and right (small, on/off only) motors
    return (ps3_output.rumble_left_force > 0) ? ps3_output.rumble_left_force :
           (ps3_output.rumble_right_on > 0) ? 0xFF : 0x00;
}

static bool ps3_mode_get_feedback(output_feedback_t* fb)
{
    if (!ps3_output_available) return false;

    // PS3: left is variable force, right is on/off only
    fb->rumble_left = ps3_output.rumble_left_force;
    fb->rumble_right = ps3_output.rumble_right_on ? 0xFF : 0x00;

    // PS3 LEDs: bitmap in leds_bitmap, shifted by 1 (LED1=0x02..LED4=0x10)
    // Combinations encode players 5-7 via PLAYER_LEDS[] lookup
    uint8_t led_bits = (ps3_output.leds_bitmap >> 1) & 0x0F;
    fb->led_player = 0;
    for (int p = 1; p <= 7; p++) {
        if (led_bits == PLAYER_LEDS[p]) {
            fb->led_player = p;
            break;
        }
    }

    fb->dirty = true;
    ps3_output_available = false;
    return true;
}

void ps3_mode_set_feature_report(uint8_t report_id, const uint8_t* buffer, uint16_t bufsize)
{
    if (report_id == PS3_REPORT_ID_FEATURE_EF && bufsize > 6) {
        ps3_ef_byte = buffer[6];
    }
}

static uint16_t ps3_mode_get_report(uint8_t report_id, hid_report_type_t report_type,
                                     uint8_t* buffer, uint16_t reqlen)
{
    if (report_type != HID_REPORT_TYPE_FEATURE) {
        return 0;
    }

    uint16_t len = 0;
    switch (report_id) {
        case PS3_REPORT_ID_FEATURE_01:
            len = sizeof(ps3_feature_01);
            if (reqlen < len) len = reqlen;
            memcpy(buffer, ps3_feature_01, len);
            return len;

        case PS3_REPORT_ID_PAIRING:
            len = sizeof(ps3_pairing);
            if (reqlen < len) len = reqlen;
            memcpy(buffer, &ps3_pairing, len);
            return len;

        case PS3_REPORT_ID_FEATURE_EF:
            len = sizeof(ps3_feature_ef);
            if (reqlen < len) len = reqlen;
            memcpy(buffer, ps3_feature_ef, len);
            buffer[6] = ps3_ef_byte;  // Echo mode byte from SET_REPORT
            return len;

        case PS3_REPORT_ID_FEATURE_F5:
            len = sizeof(ps3_feature_f5);
            if (reqlen < len) len = reqlen;
            memcpy(buffer, ps3_feature_f5, len);
            // Inject host BT address at offsets 1-6
            for (int i = 0; i < 6; i++) {
                buffer[1 + i] = ps3_pairing.host_address[1 + i];
            }
            return len;

        case PS3_REPORT_ID_FEATURE_F7:
            len = sizeof(ps3_feature_f7);
            if (reqlen < len) len = reqlen;
            memcpy(buffer, ps3_feature_f7, len);
            return len;

        case PS3_REPORT_ID_FEATURE_F8:
            len = sizeof(ps3_feature_f8);
            if (reqlen < len) len = reqlen;
            memcpy(buffer, ps3_feature_f8, len);
            buffer[6] = ps3_ef_byte;  // Echo mode byte from SET_REPORT
            return len;

        default:
            return 0;
    }
}

static const uint8_t* ps3_mode_get_device_descriptor(void)
{
    return (const uint8_t*)&ps3_device_descriptor;
}

static const uint8_t* ps3_mode_get_config_descriptor(void)
{
    return ps3_config_descriptor;
}

static const uint8_t* ps3_mode_get_report_descriptor(void)
{
    return ps3_report_descriptor;
}

// ============================================================================
// MODE EXPORT
// ============================================================================

const usbd_mode_t ps3_mode = {
    .name = "PS3",
    .mode = USB_OUTPUT_MODE_PS3,

    .get_device_descriptor = ps3_mode_get_device_descriptor,
    .get_config_descriptor = ps3_mode_get_config_descriptor,
    .get_report_descriptor = ps3_mode_get_report_descriptor,

    .init = ps3_mode_init,
    .send_report = ps3_mode_send_report,
    .is_ready = ps3_mode_is_ready,

    .handle_output = ps3_mode_handle_output,
    .get_rumble = ps3_mode_get_rumble,
    .get_feedback = ps3_mode_get_feedback,
    .get_report = ps3_mode_get_report,
    .get_class_driver = NULL,
    .task = NULL,
};
