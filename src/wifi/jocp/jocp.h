// jocp.h - Joypad Open Controller Protocol
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith
//
// JOCP is a simple packet protocol for streaming controller input over WiFi.
// Designed for low latency UDP transport with optional TCP control channel.
//
// Protocol version: 0.1
// Reference: .dev/docs/jocp.md

#ifndef JOCP_H
#define JOCP_H

#include <stdint.h>
#include <stdbool.h>
#include "core/output_interface.h"

// ============================================================================
// PROTOCOL CONSTANTS
// ============================================================================

#define JOCP_MAGIC              0x4A50  // "JP" in little-endian
#define JOCP_VERSION            0x01    // Protocol version 0.1

// Default ports (from design doc Appendix A)
#define JOCP_DEFAULT_UDP_PORT   30100   // UDP INPUT packets
#define JOCP_DEFAULT_TCP_PORT   30101   // TCP CONTROL channel

// ============================================================================
// MESSAGE TYPES
// ============================================================================

typedef enum {
    JOCP_MSG_INPUT      = 0x01,     // Controller → Dongle (UDP)
    JOCP_MSG_CAPS_REQ   = 0x02,     // Dongle → Controller (TCP)
    JOCP_MSG_CAPS_RES   = 0x03,     // Controller → Dongle (TCP)
    JOCP_MSG_OUTPUT_CMD = 0x04,     // Dongle → Controller (TCP)
    JOCP_MSG_TIME_SYNC  = 0x05,     // Both directions (TCP)
} jocp_msg_type_t;

// ============================================================================
// PACKET FLAGS (INPUT message)
// ============================================================================

#define JOCP_FLAG_HAS_IMU       (1 << 0)    // Packet contains IMU data
#define JOCP_FLAG_HAS_TOUCH     (1 << 1)    // Packet contains touch data
#define JOCP_FLAG_KEYFRAME      (1 << 2)    // Full state (vs delta, v0.1 always keyframe)
#define JOCP_FLAG_ACK_REQ       (1 << 3)    // Request acknowledgment (unused v0.1)

// ============================================================================
// PACKET HEADER (12 bytes, little-endian)
// ============================================================================

typedef struct __attribute__((packed)) {
    uint16_t magic;             // 0x4A50 ("JP")
    uint8_t  version;           // Protocol version (0x01)
    uint8_t  msg_type;          // Message type (jocp_msg_type_t)
    uint16_t seq;               // Sequence number (increments per packet)
    uint16_t flags;             // Flags bitfield
    uint32_t timestamp_us;      // Controller monotonic timestamp (microseconds)
} jocp_header_t;

_Static_assert(sizeof(jocp_header_t) == 12, "JOCP header must be 12 bytes");

// ============================================================================
// INPUT PAYLOAD (64 bytes, SInput-compatible layout)
// ============================================================================

// Button bit positions (4 bytes = 32 buttons)
// Byte 0: Face buttons + D-pad
#define JOCP_BTN_SOUTH      (1U << 0)   // A/Cross
#define JOCP_BTN_EAST       (1U << 1)   // B/Circle
#define JOCP_BTN_WEST       (1U << 2)   // X/Square
#define JOCP_BTN_NORTH      (1U << 3)   // Y/Triangle
#define JOCP_BTN_DU         (1U << 4)   // D-pad Up
#define JOCP_BTN_DD         (1U << 5)   // D-pad Down
#define JOCP_BTN_DL         (1U << 6)   // D-pad Left
#define JOCP_BTN_DR         (1U << 7)   // D-pad Right

// Byte 1: Shoulders, triggers, sticks
#define JOCP_BTN_L1         (1U << 8)   // Left bumper
#define JOCP_BTN_R1         (1U << 9)   // Right bumper
#define JOCP_BTN_L2         (1U << 10)  // Left trigger (digital)
#define JOCP_BTN_R2         (1U << 11)  // Right trigger (digital)
#define JOCP_BTN_L3         (1U << 12)  // Left stick click
#define JOCP_BTN_R3         (1U << 13)  // Right stick click
#define JOCP_BTN_L_PADDLE1  (1U << 14)  // Left paddle 1
#define JOCP_BTN_R_PADDLE1  (1U << 15)  // Right paddle 1

// Byte 2: System buttons
#define JOCP_BTN_START      (1U << 16)  // Start/Options
#define JOCP_BTN_BACK       (1U << 17)  // Back/Select
#define JOCP_BTN_GUIDE      (1U << 18)  // Home/Guide
#define JOCP_BTN_CAPTURE    (1U << 19)  // Capture/Share
#define JOCP_BTN_L_PADDLE2  (1U << 20)  // Left paddle 2
#define JOCP_BTN_R_PADDLE2  (1U << 21)  // Right paddle 2
#define JOCP_BTN_TOUCHPAD1  (1U << 22)  // Touchpad click 1
#define JOCP_BTN_TOUCHPAD2  (1U << 23)  // Touchpad click 2

// Byte 3: Extended (reserved)
#define JOCP_BTN_POWER      (1U << 24)  // Power button
#define JOCP_BTN_MISC1      (1U << 25)
#define JOCP_BTN_MISC2      (1U << 26)
#define JOCP_BTN_MISC3      (1U << 27)
#define JOCP_BTN_MISC4      (1U << 28)
#define JOCP_BTN_MISC5      (1U << 29)
#define JOCP_BTN_MISC6      (1U << 30)
#define JOCP_BTN_MISC7      (1U << 31)

// Touch contact structure (6 bytes per contact)
typedef struct __attribute__((packed)) {
    uint16_t x;                 // X position (0-1920 typical)
    uint16_t y;                 // Y position (0-1080 typical)
    uint8_t  pressure;          // Pressure (0-255)
    uint8_t  id_active;         // bits 0-3: contact ID, bit 7: active flag
} jocp_touch_t;

// Input payload (64 bytes)
typedef struct __attribute__((packed)) {
    // Buttons (4 bytes)
    uint32_t buttons;           // 32-bit button mask

    // Analog sticks (8 bytes) - signed 16-bit, centered at 0
    int16_t  lx;                // Left stick X (-32768 to 32767)
    int16_t  ly;                // Left stick Y (-32768 to 32767)
    int16_t  rx;                // Right stick X (-32768 to 32767)
    int16_t  ry;                // Right stick Y (-32768 to 32767)

    // Triggers (4 bytes) - unsigned 16-bit, 0-65535
    uint16_t lt;                // Left trigger
    uint16_t rt;                // Right trigger

    // IMU data (12 bytes) - signed 16-bit
    int16_t  accel_x;           // Accelerometer X (mg or raw)
    int16_t  accel_y;           // Accelerometer Y
    int16_t  accel_z;           // Accelerometer Z
    int16_t  gyro_x;            // Gyroscope X (mdps or raw)
    int16_t  gyro_y;            // Gyroscope Y
    int16_t  gyro_z;            // Gyroscope Z

    // IMU timestamp (4 bytes)
    uint32_t imu_timestamp;     // Microseconds, for IMU integration

    // Touchpad contacts (12 bytes) - 2 contacts
    jocp_touch_t touch[2];

    // Battery/status (2 bytes)
    uint8_t  battery_level;     // 0-100 percent
    uint8_t  plug_status;       // bit 0: charging, bit 1: wired

    // Controller ID (1 byte) - for multi-controller support
    uint8_t  controller_id;     // 0-3 for up to 4 controllers

    // Reserved for future use (17 bytes to pad to 64)
    uint8_t  reserved[17];
} jocp_input_t;

_Static_assert(sizeof(jocp_input_t) == 64, "JOCP input payload must be 64 bytes");

// Complete INPUT packet (header + payload)
typedef struct __attribute__((packed)) {
    jocp_header_t header;
    jocp_input_t  payload;
} jocp_input_packet_t;

_Static_assert(sizeof(jocp_input_packet_t) == 76, "JOCP input packet must be 76 bytes");

// ============================================================================
// CAPABILITIES STRUCTURES (TCP)
// ============================================================================

// Capability flags
#define JOCP_CAP_GYRO           (1U << 0)
#define JOCP_CAP_ACCEL          (1U << 1)
#define JOCP_CAP_TOUCH          (1U << 2)
#define JOCP_CAP_RUMBLE         (1U << 3)
#define JOCP_CAP_PLAYER_LED     (1U << 4)
#define JOCP_CAP_RGB_LED        (1U << 5)
#define JOCP_CAP_PADDLES        (1U << 6)
#define JOCP_CAP_ANALOG_TRIGGER (1U << 7)

// Capabilities response (variable length)
typedef struct __attribute__((packed)) {
    uint32_t device_id;         // Unique device identifier
    uint32_t capabilities;      // Capability flags
    uint16_t poll_rate_min;     // Minimum poll rate (Hz)
    uint16_t poll_rate_max;     // Maximum poll rate (Hz)
    uint16_t poll_rate_current; // Current poll rate (Hz)
    uint8_t  firmware_version[16]; // Firmware version string (null-terminated)
} jocp_caps_t;

// ============================================================================
// OUTPUT COMMANDS (TCP, Dongle → Controller)
// ============================================================================

typedef enum {
    JOCP_CMD_RUMBLE      = 0x01,    // Set rumble motors
    JOCP_CMD_PLAYER_LED  = 0x02,    // Set player LED index
    JOCP_CMD_RGB_LED     = 0x03,    // Set RGB LED color
    JOCP_CMD_POLL_RATE   = 0x04,    // Set poll rate
} jocp_output_cmd_t;

// Rumble command payload
typedef struct __attribute__((packed)) {
    uint8_t left_amplitude;     // Left motor (0-255)
    uint8_t left_brake;         // Left brake (bool)
    uint8_t right_amplitude;    // Right motor (0-255)
    uint8_t right_brake;        // Right brake (bool)
    uint16_t duration_ms;       // Duration in milliseconds (0 = until changed)
} jocp_rumble_cmd_t;

// Player LED command payload
typedef struct __attribute__((packed)) {
    uint8_t player_index;       // 1-4 (0 = off)
} jocp_player_led_cmd_t;

// RGB LED command payload
typedef struct __attribute__((packed)) {
    uint8_t r;                  // Red (0-255)
    uint8_t g;                  // Green (0-255)
    uint8_t b;                  // Blue (0-255)
} jocp_rgb_led_cmd_t;

// ============================================================================
// JOCP API
// ============================================================================

// Initialize JOCP subsystem
void jocp_init(void);

// Process incoming UDP packet (called from wifi_transport)
// Returns true if packet was valid and processed
bool jocp_process_input_packet(const uint8_t* data, uint16_t len,
                               uint32_t src_ip, uint16_t src_port);

// Get number of connected controllers
uint8_t jocp_get_connected_count(void);

// Send feedback to all connected controllers
void jocp_send_feedback_all(const output_feedback_t* fb);

// Send feedback to specific controller
void jocp_send_feedback(uint8_t controller_id, const output_feedback_t* fb);

#endif // JOCP_H
