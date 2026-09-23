// switch2_proto.h - Switch 2 Pro Controller protocol engine (console-facing)
// SPDX-License-Identifier: Apache-2.0
//
// Transport-agnostic half of the Switch 2 BLE output mode: the 0x15 pairing handshake
// (LTK derivation + AES possession proof), the command channel replies the console
// expects during init, the emulated controller flash, the input report (0x09) and the
// LRA rumble decode. No BTstack / platform dependencies, so it builds for the host
// unit test (tests/switch2_proto).
//
// Protocol facts: ndeadly/switch2_controller_research (decrypted pairing/reconnect
// captures). Reply bytes for the late-init probes follow the HW-verified emulators
// esp-cpp/espp switch2_pro and zhantss/ESP32-BLE5-NSController-Emulator (both MIT).

#ifndef SWITCH2_PROTO_H
#define SWITCH2_PROTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SW2_VID                 0x057E
#define SW2_PID_PRO2            0x2069
#define SW2_COMPANY_ID          0x0553

#define SW2_CMD_HEADER_LEN      8
#define SW2_VIB_CMD_PREFIX_LEN  33   // zero rumble payload before a command on 0x0016 (Pro)
#define SW2_RSP2_PREFIX_LEN     14   // zero header before a response on 0x001e
#define SW2_INPUT_REPORT_LEN    63   // report 0x09 over BLE (no report-id byte)
#define SW2_MFR_DATA_LEN        26
#define SW2_MAX_RSP_LEN         (SW2_CMD_HEADER_LEN + 8 + 0x40)  // flash read of 0x40 is the largest

// Console-side feature bits (command 0x0C)
#define SW2_FEATURE_BUTTONS     0x01
#define SW2_FEATURE_STICKS      0x02
#define SW2_FEATURE_IMU         0x04
#define SW2_FEATURE_MOUSE       0x10
#define SW2_FEATURE_RUMBLE      0x20

// Events reported back to the transport by switch2_proto_handle_command()
#define SW2_EVT_NONE            0x00
#define SW2_EVT_PAIRED          0x01   // 0x15/0x03 finalised: persist host_addr + ltk, arm encryption
#define SW2_EVT_PLAYER_LED      0x02   // console assigned a player (see player_leds)

typedef enum {
    SW2_ADV_PAIRING = 0,    // no bond: console's Change Grip/Order screen finds us
    SW2_ADV_RECONNECT,      // bonded: embeds the console address
    SW2_ADV_WAKE,           // bonded + wake flag 0x81: powers a sleeping console on
} switch2_adv_kind_t;

typedef struct {
    // Pairing (0x15)
    uint8_t  pair_stage;        // 0 none, 1 addresses, 2 keys, 3 confirmed
    uint8_t  host_addr[6];      // console identity address, wire (little-endian) order
    uint8_t  ltk[16];           // A1 ^ B1, wire order (what the controller hands HCI)
    bool     paired;

    // Our radio address, wire order (answer to 0x15/0x01)
    uint8_t  local_addr[6];

    // Feature select (0x0C)
    uint8_t  feature_mask;
    uint8_t  features;

    // Player LEDs (0x09/0x07)
    uint8_t  player_leds;

    uint8_t  counter;           // input report byte 0
} switch2_proto_t;

typedef struct {
    uint32_t buttons;           // JP_BUTTON_*
    uint8_t  lx, ly, rx, ry;    // 0..255, HID convention (0 = up)
    uint8_t  l2, r2;            // analog triggers 0..255 (digital ZL/ZR on a Pro pad)
    uint8_t  battery_pct;       // 0..100
    bool     charging;
} switch2_input_t;

void switch2_proto_init(switch2_proto_t *s, const uint8_t local_addr_le[6]);

// Pairing crypto. ltk = A1 ^ B1; b2 = AES-128-ECB(key = rev(ltk), pt = rev(a2)).
void switch2_derive_ltk(const uint8_t a1[16], uint8_t ltk[16]);
void switch2_confirm(const uint8_t ltk[16], const uint8_t a2[16], uint8_t b2[16]);

// Emulated controller flash (what the console reads with 0x02/0x04). Unmapped = 0xFF.
void switch2_flash_read(uint32_t addr, uint8_t len, uint8_t *out);

// Handle one command. `cmd` starts at the 8-byte header (transport strips the 0x0016
// rumble prefix). Writes the full response (header + payload, no channel prefix) to
// `rsp` (>= SW2_MAX_RSP_LEN) and returns its length, or 0 for no reply. `*events`
// receives SW2_EVT_* flags.
uint16_t switch2_proto_handle_command(switch2_proto_t *s, const uint8_t *cmd, uint16_t len,
                                      uint8_t *rsp, uint8_t *events);

// Build the 63-byte input report (0x09) and advance the counter.
void switch2_proto_build_input(switch2_proto_t *s, const switch2_input_t *in,
                               uint8_t out[SW2_INPUT_REPORT_LEN]);

// Manufacturer-specific advertising payload (company id first), SW2_MFR_DATA_LEN bytes.
void switch2_build_mfr_data(switch2_adv_kind_t kind, const uint8_t host_addr_le[6],
                            uint8_t out[SW2_MFR_DATA_LEN]);

// Decode a Pro Controller 2 rumble payload (two 16-byte LRA blocks: left then right)
// into 0..255 amplitudes.
void switch2_rumble_decode(const uint8_t lra[32], uint8_t *left, uint8_t *right);

// Player number 1..8 from the LED bitmask (0 if unknown)
uint8_t switch2_player_from_leds(uint8_t leds);

#endif // SWITCH2_PROTO_H
