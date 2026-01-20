// jocp_input.c - JOCP Input Packet Processing
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// Parses JOCP INPUT packets and converts them to Joypad OS input events.

#include "jocp.h"
#include "wifi_transport.h"
#include "core/router/router.h"
#include "core/buttons.h"
#include "core/input_event.h"

#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>

// ============================================================================
// STATE
// ============================================================================

#define MAX_CONTROLLERS 4

typedef struct {
    bool active;
    uint32_t ip;
    uint16_t port;
    uint16_t last_seq;
    uint32_t last_seen_ms;
    uint32_t packet_count;
    uint32_t drop_count;
} jocp_controller_t;

static jocp_controller_t controllers[MAX_CONTROLLERS];
static uint8_t connected_count = 0;

// Timeout for considering a controller disconnected (ms)
#define CONTROLLER_TIMEOUT_MS 5000

// ============================================================================
// BUTTON CONVERSION
// ============================================================================

// Convert JOCP buttons to Joypad OS buttons
static uint32_t convert_buttons(uint32_t jocp_buttons)
{
    uint32_t jp_buttons = 0;

    // Face buttons
    if (jocp_buttons & JOCP_BTN_SOUTH)  jp_buttons |= JP_BUTTON_B1;  // A/Cross
    if (jocp_buttons & JOCP_BTN_EAST)   jp_buttons |= JP_BUTTON_B2;  // B/Circle
    if (jocp_buttons & JOCP_BTN_WEST)   jp_buttons |= JP_BUTTON_B3;  // X/Square
    if (jocp_buttons & JOCP_BTN_NORTH)  jp_buttons |= JP_BUTTON_B4;  // Y/Triangle

    // D-pad
    if (jocp_buttons & JOCP_BTN_DU)     jp_buttons |= JP_BUTTON_DU;
    if (jocp_buttons & JOCP_BTN_DD)     jp_buttons |= JP_BUTTON_DD;
    if (jocp_buttons & JOCP_BTN_DL)     jp_buttons |= JP_BUTTON_DL;
    if (jocp_buttons & JOCP_BTN_DR)     jp_buttons |= JP_BUTTON_DR;

    // Shoulders and triggers
    if (jocp_buttons & JOCP_BTN_L1)     jp_buttons |= JP_BUTTON_L1;
    if (jocp_buttons & JOCP_BTN_R1)     jp_buttons |= JP_BUTTON_R1;
    if (jocp_buttons & JOCP_BTN_L2)     jp_buttons |= JP_BUTTON_L2;
    if (jocp_buttons & JOCP_BTN_R2)     jp_buttons |= JP_BUTTON_R2;

    // Stick clicks
    if (jocp_buttons & JOCP_BTN_L3)     jp_buttons |= JP_BUTTON_L3;
    if (jocp_buttons & JOCP_BTN_R3)     jp_buttons |= JP_BUTTON_R3;

    // System buttons
    if (jocp_buttons & JOCP_BTN_START)  jp_buttons |= JP_BUTTON_S2;  // Start
    if (jocp_buttons & JOCP_BTN_BACK)   jp_buttons |= JP_BUTTON_S1;  // Select/Back
    if (jocp_buttons & JOCP_BTN_GUIDE)  jp_buttons |= JP_BUTTON_A1;  // Home/Guide
    if (jocp_buttons & JOCP_BTN_CAPTURE) jp_buttons |= JP_BUTTON_A2; // Capture

    // Paddles
    if (jocp_buttons & JOCP_BTN_L_PADDLE1) jp_buttons |= JP_BUTTON_L4;
    if (jocp_buttons & JOCP_BTN_R_PADDLE1) jp_buttons |= JP_BUTTON_R4;

    return jp_buttons;
}

// Convert signed 16-bit axis to unsigned 8-bit (0-255, 128=center)
static uint8_t convert_axis_s16_to_u8(int16_t value)
{
    // -32768 to 32767 → 0 to 255
    int32_t scaled = ((int32_t)value + 32768) >> 8;
    if (scaled < 0) scaled = 0;
    if (scaled > 255) scaled = 255;
    return (uint8_t)scaled;
}

// Convert unsigned 16-bit trigger to unsigned 8-bit (0-255)
static uint8_t convert_trigger_u16_to_u8(uint16_t value)
{
    // 0 to 65535 → 0 to 255
    return (uint8_t)(value >> 8);
}

// ============================================================================
// CONTROLLER TRACKING
// ============================================================================

static int find_controller_by_ip(uint32_t ip, uint16_t port)
{
    for (int i = 0; i < MAX_CONTROLLERS; i++) {
        if (controllers[i].active && controllers[i].ip == ip) {
            return i;
        }
    }
    return -1;
}

static int find_free_controller_slot(void)
{
    for (int i = 0; i < MAX_CONTROLLERS; i++) {
        if (!controllers[i].active) return i;
    }
    return -1;
}

static int find_or_create_controller(uint32_t ip, uint16_t port)
{
    int slot = find_controller_by_ip(ip, port);
    if (slot >= 0) return slot;

    slot = find_free_controller_slot();
    if (slot < 0) {
        printf("[jocp] Max controllers reached, ignoring new connection\n");
        return -1;
    }

    controllers[slot].active = true;
    controllers[slot].ip = ip;
    controllers[slot].port = port;
    controllers[slot].last_seq = 0;
    controllers[slot].last_seen_ms = to_ms_since_boot(get_absolute_time());
    controllers[slot].packet_count = 0;
    controllers[slot].drop_count = 0;
    connected_count++;

    printf("[jocp] New controller connected: slot %d, IP %08lX:%d\n",
           slot, (unsigned long)ip, port);

    return slot;
}

static void check_controller_timeouts(void)
{
    uint32_t now = to_ms_since_boot(get_absolute_time());

    for (int i = 0; i < MAX_CONTROLLERS; i++) {
        if (controllers[i].active) {
            if (now - controllers[i].last_seen_ms > CONTROLLER_TIMEOUT_MS) {
                printf("[jocp] Controller %d timed out (IP %08lX)\n",
                       i, (unsigned long)controllers[i].ip);
                controllers[i].active = false;
                connected_count--;
            }
        }
    }
}

// ============================================================================
// PACKET PROCESSING
// ============================================================================

void jocp_init(void)
{
    memset(controllers, 0, sizeof(controllers));
    connected_count = 0;
    printf("[jocp] JOCP subsystem initialized\n");
}

bool jocp_process_input_packet(const uint8_t* data, uint16_t len,
                               uint32_t src_ip, uint16_t src_port)
{
    // Check timeouts periodically
    static uint32_t last_timeout_check = 0;
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if (now - last_timeout_check > 1000) {
        check_controller_timeouts();
        last_timeout_check = now;
    }

    // Validate minimum packet size
    if (len < sizeof(jocp_header_t)) {
        printf("[jocp] Packet too short: %d bytes\n", len);
        return false;
    }

    // Parse header
    const jocp_header_t* header = (const jocp_header_t*)data;

    // Validate magic
    if (header->magic != JOCP_MAGIC) {
        printf("[jocp] Invalid magic: 0x%04X (expected 0x%04X)\n",
               header->magic, JOCP_MAGIC);
        return false;
    }

    // Validate version
    if (header->version != JOCP_VERSION) {
        printf("[jocp] Unsupported version: 0x%02X (expected 0x%02X)\n",
               header->version, JOCP_VERSION);
        return false;
    }

    // Check message type
    if (header->msg_type != JOCP_MSG_INPUT) {
        printf("[jocp] Unexpected message type: 0x%02X\n", header->msg_type);
        return false;
    }

    // Validate payload size
    if (len < sizeof(jocp_input_packet_t)) {
        printf("[jocp] INPUT packet too short: %d bytes (expected %zu)\n",
               len, sizeof(jocp_input_packet_t));
        return false;
    }

    // Find or create controller slot
    int slot = find_or_create_controller(src_ip, src_port);
    if (slot < 0) return false;

    // Check sequence number (detect packet loss)
    uint16_t expected_seq = controllers[slot].last_seq + 1;
    if (controllers[slot].packet_count > 0 && header->seq != expected_seq) {
        uint16_t dropped = header->seq - expected_seq;
        controllers[slot].drop_count += dropped;
        // Only log occasionally to avoid spam
        if (controllers[slot].drop_count % 100 == 1) {
            printf("[jocp] Controller %d: dropped %u packets (total %lu)\n",
                   slot, dropped, (unsigned long)controllers[slot].drop_count);
        }
    }

    controllers[slot].last_seq = header->seq;
    controllers[slot].last_seen_ms = now;
    controllers[slot].packet_count++;

    // Parse input payload
    const jocp_input_t* input = (const jocp_input_t*)(data + sizeof(jocp_header_t));

    // Convert to Joypad OS input event
    input_event_t event = {0};

    // Use a unique dev_addr for WiFi controllers (0xE0 + slot)
    event.dev_addr = 0xE0 + slot;
    event.instance = 0;
    event.type = INPUT_TYPE_GAMEPAD;

    // Convert buttons
    event.buttons = convert_buttons(input->buttons);

    // Convert analog sticks (signed 16-bit → unsigned 8-bit)
    event.analog[0] = convert_axis_s16_to_u8(input->lx);  // Left X
    event.analog[1] = convert_axis_s16_to_u8(input->ly);  // Left Y
    event.analog[2] = convert_axis_s16_to_u8(input->rx);  // Right X
    event.analog[3] = convert_axis_s16_to_u8(input->ry);  // Right Y

    // Convert triggers (unsigned 16-bit → unsigned 8-bit)
    event.analog[4] = convert_trigger_u16_to_u8(input->lt);  // Left trigger
    event.analog[5] = convert_trigger_u16_to_u8(input->rt);  // Right trigger

    // Submit to router
    router_submit_input(&event);

    return true;
}

uint8_t jocp_get_connected_count(void)
{
    return connected_count;
}

// ============================================================================
// OUTPUT FEEDBACK
// ============================================================================

void jocp_send_feedback_all(const output_feedback_t* fb)
{
    for (int i = 0; i < MAX_CONTROLLERS; i++) {
        if (controllers[i].active) {
            jocp_send_feedback(i, fb);
        }
    }
}

void jocp_send_feedback(uint8_t controller_id, const output_feedback_t* fb)
{
    if (controller_id >= MAX_CONTROLLERS) return;
    if (!controllers[controller_id].active) return;
    if (!fb) return;

    // Build OUTPUT_CMD packet for rumble
    if (fb->rumble_left > 0 || fb->rumble_right > 0) {
        uint8_t packet[32];
        jocp_header_t* header = (jocp_header_t*)packet;

        header->magic = JOCP_MAGIC;
        header->version = JOCP_VERSION;
        header->msg_type = JOCP_MSG_OUTPUT_CMD;
        header->seq = 0;  // Not used for output
        header->flags = 0;
        header->timestamp_us = time_us_32();

        // Rumble command
        uint8_t* cmd = packet + sizeof(jocp_header_t);
        cmd[0] = JOCP_CMD_RUMBLE;
        jocp_rumble_cmd_t* rumble = (jocp_rumble_cmd_t*)(cmd + 1);
        rumble->left_amplitude = fb->rumble_left;
        rumble->left_brake = 0;
        rumble->right_amplitude = fb->rumble_right;
        rumble->right_brake = 0;
        rumble->duration_ms = 0;  // Until changed

        uint16_t len = sizeof(jocp_header_t) + 1 + sizeof(jocp_rumble_cmd_t);

        // Send via UDP (faster than TCP for haptics)
        wifi_transport_send_udp(controllers[controller_id].ip,
                                controllers[controller_id].port,
                                packet, len);
    }

    // TODO: Send RGB LED and player LED commands
}
