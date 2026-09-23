// switch2_proto.c - Switch 2 Pro Controller protocol engine (see switch2_proto.h)
// SPDX-License-Identifier: Apache-2.0
//
// Protocol facts from ndeadly/switch2_controller_research (commands.md, hid_reports.md,
// memory_layout.md and the decrypted btle_procon2_* captures). Late-init reply bytes
// cross-checked against esp-cpp/espp switch2_pro and zhantss' ESP32 emulator (MIT),
// both verified against a real console. No code copied.

#include "switch2_proto.h"
#include "core/buttons.h"
#include "rijndael.h"
#include <string.h>

// Fixed controller key returned for 0x15/0x04 (identical on every Switch 2 controller)
static const uint8_t k_b1[16] = {
    0x5c, 0xf6, 0xee, 0x79, 0x2c, 0xdf, 0x05, 0xe1,
    0xba, 0x2b, 0x63, 0x25, 0xc4, 0x1a, 0x5f, 0x10,
};

// Command ids
#define CMD_NFC             0x01
#define CMD_FLASH           0x02
#define CMD_INIT            0x03
#define CMD_UNKNOWN_07      0x07
#define CMD_PLAYER_LED      0x09
#define CMD_VIBRATION       0x0A
#define CMD_FEATURE         0x0C
#define CMD_FW_UPDATE       0x0D
#define CMD_FW_INFO         0x10
#define CMD_UNKNOWN_11      0x11
#define CMD_PAIRING         0x15
#define CMD_UNKNOWN_16      0x16
#define CMD_UNKNOWN_18      0x18

// ---------------------------------------------------------------------------
// Emulated flash: blocks captured from a real Pro Controller 2 (btle_procon2_pairing).
// The console validates the serial + VID/PID at 0x13000 before it will pair.
// ---------------------------------------------------------------------------

static const uint8_t k_flash_13000[0x40] = {
    0x01, 0x00, 'H', 'E', 'J', '7', '1', '0', '0', '1', '1', '2', '1', '2', '4', '7',
    0x00, 0x00, 0x7e, 0x05, 0x69, 0x20, 0x01, 0x06, 0x01, 0x23, 0x23, 0x23, 0xa0, 0xa0, 0xa0, 0xe6,
    0xe6, 0xe6, 0x32, 0x32, 0x32, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};
static const uint8_t k_flash_13040[0x10] = {   // gyro bias
    0x3b, 0xe0, 0xd3, 0x41, 0xc6, 0x60, 0x6a, 0xbc, 0x4d, 0xd7, 0xa2, 0xbb, 0x71, 0x1e, 0xdd, 0x37,
};
// Stick factory calibration: 0x28-byte header, then at +0x28 the packed 12-bit
// {neutral, +max, -min} for X/Y. The input encoder below targets these exact values.
static const uint8_t k_flash_13080[0x31] = {
    0x01, 0xad, 0xd9, 0x9a, 0x55, 0x56, 0x65, 0xa0, 0x00, 0x0a, 0xa0, 0x00, 0x0a, 0xe2, 0x20, 0x0e,
    0xe2, 0x20, 0x0e, 0x9a, 0xad, 0xd9, 0x9a, 0xad, 0xd9, 0x0a, 0xa5, 0x50, 0x0a, 0xa5, 0x50, 0x2f,
    0xf6, 0x62, 0x2f, 0xf6, 0x62, 0x0a, 0xff, 0xff,
    0xb3, 0x67, 0x83, 0x2e, 0x66, 0x5e, 0x3a, 0x06, 0x5f,
};
static const uint8_t k_flash_130c0[0x31] = {
    0x01, 0xad, 0xd9, 0x9a, 0x55, 0x56, 0x65, 0xa0, 0x00, 0x0a, 0xa0, 0x00, 0x0a, 0xe2, 0x20, 0x0e,
    0xe2, 0x20, 0x0e, 0x9a, 0xad, 0xd9, 0x9a, 0xad, 0xd9, 0x0a, 0xa5, 0x50, 0x0a, 0xa5, 0x50, 0x2f,
    0xf6, 0x62, 0x2f, 0xf6, 0x62, 0x0a, 0xff, 0xff,
    0x2c, 0x08, 0x84, 0xd1, 0x65, 0x63, 0x2a, 0x26, 0x62,
};
static const uint8_t k_flash_13100[0x18] = {   // accel bias
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xa6, 0xf2, 0x62, 0xbd, 0xa8, 0x00, 0x08, 0x3d, 0x2f, 0xed, 0x20, 0x41,
};

typedef struct { uint32_t addr; const uint8_t *data; uint8_t len; } flash_block_t;
static const flash_block_t k_flash[] = {
    { 0x13000, k_flash_13000, sizeof(k_flash_13000) },
    { 0x13040, k_flash_13040, sizeof(k_flash_13040) },
    { 0x13080, k_flash_13080, sizeof(k_flash_13080) },
    { 0x130C0, k_flash_130c0, sizeof(k_flash_130c0) },
    { 0x13100, k_flash_13100, sizeof(k_flash_13100) },
};

void switch2_flash_read(uint32_t addr, uint8_t len, uint8_t *out)
{
    memset(out, 0xff, len);
    for (size_t b = 0; b < sizeof(k_flash) / sizeof(k_flash[0]); b++) {
        const flash_block_t *blk = &k_flash[b];
        for (uint8_t i = 0; i < len; i++) {
            uint32_t a = addr + i;
            if (a >= blk->addr && a < blk->addr + blk->len) out[i] = blk->data[a - blk->addr];
        }
    }
}

// Axis calibration (from the blocks above): neutral, +range, -range.
typedef struct { uint16_t neutral, max, min; } axis_cal_t;
static const axis_cal_t k_cal_lx = { 0x7b3, 0x62e, 0x63a };
static const axis_cal_t k_cal_ly = { 0x836, 0x5e6, 0x5f0 };
static const axis_cal_t k_cal_rx = { 0x82c, 0x5d1, 0x62a };
static const axis_cal_t k_cal_ry = { 0x848, 0x636, 0x622 };

// ---------------------------------------------------------------------------
// Pairing crypto
// ---------------------------------------------------------------------------

void switch2_derive_ltk(const uint8_t a1[16], uint8_t ltk[16])
{
    for (int i = 0; i < 16; i++) ltk[i] = a1[i] ^ k_b1[i];
}

void switch2_confirm(const uint8_t ltk[16], const uint8_t a2[16], uint8_t b2[16])
{
    uint8_t key[16], pt[16];
    for (int i = 0; i < 16; i++) { key[i] = ltk[15 - i]; pt[i] = a2[15 - i]; }
    uint32_t rk[RKLENGTH(128)];
    int nrounds = rijndaelSetupEncrypt(rk, key, 128);
    rijndaelEncrypt(rk, nrounds, pt, b2);
}

// ---------------------------------------------------------------------------
// Command channel
// ---------------------------------------------------------------------------

void switch2_proto_init(switch2_proto_t *s, const uint8_t local_addr_le[6])
{
    memset(s, 0, sizeof(*s));
    if (local_addr_le) memcpy(s->local_addr, local_addr_le, 6);
    s->feature_mask = 0x2f;
}

static uint16_t reply(const uint8_t *cmd, uint8_t *rsp, const uint8_t *payload, uint16_t n)
{
    rsp[0] = cmd[0];
    rsp[1] = 0x01;          // device -> host
    rsp[2] = cmd[2];        // transport echo (0x01 BT)
    rsp[3] = cmd[3];
    rsp[4] = 0x10;          // every BLE response carries 10 78 here (USB: 00 f8)
    rsp[5] = 0x78;
    rsp[6] = 0x00;
    rsp[7] = 0x00;
    if (n && payload) memcpy(&rsp[SW2_CMD_HEADER_LEN], payload, n);
    return (uint16_t)(SW2_CMD_HEADER_LEN + n);
}

static uint16_t handle_pairing(switch2_proto_t *s, const uint8_t *cmd, const uint8_t *p,
                               uint16_t n, uint8_t *rsp, uint8_t *events)
{
    uint8_t out[17];
    switch (cmd[3]) {
    case 0x01:  // exchange addresses: [00][count][addr1 6][addr2 6] -> [01 04 01][our addr]
        if (n < 8) { s->pair_stage = 0; return 0; }
        memcpy(s->host_addr, &p[2], 6);
        s->pair_stage = 1;
        out[0] = 0x01; out[1] = 0x04; out[2] = 0x01;
        memcpy(&out[3], s->local_addr, 6);
        return reply(cmd, rsp, out, 9);
    case 0x04:  // exchange keys: [00][A1] -> [01][B1]
        if (n < 17) { s->pair_stage = 0; return 0; }
        switch2_derive_ltk(&p[1], s->ltk);
        s->pair_stage = 2;
        out[0] = 0x01;
        memcpy(&out[1], k_b1, 16);
        return reply(cmd, rsp, out, 17);
    case 0x02:  // confirm LTK: [00][A2] -> [01][B2]; the console checks B2
        if (s->pair_stage < 2 || n < 17) { s->pair_stage = 0; return 0; }
        switch2_confirm(s->ltk, &p[1], &out[1]);
        out[0] = 0x01;
        s->pair_stage = 3;
        return reply(cmd, rsp, out, 17);
    case 0x03:  // finalise; the console starts LL encryption with the LTK right after
        if (s->pair_stage < 3) return 0;   // never persist a half-done bond
        s->paired = true;
        *events |= SW2_EVT_PAIRED;
        out[0] = 0x01;
        return reply(cmd, rsp, out, 1);
    default:
        return reply(cmd, rsp, NULL, 0);
    }
}

uint16_t switch2_proto_handle_command(switch2_proto_t *s, const uint8_t *cmd, uint16_t len,
                                      uint8_t *rsp, uint8_t *events)
{
    *events = SW2_EVT_NONE;
    if (len < SW2_CMD_HEADER_LEN || cmd[1] != 0x91) return 0;
    const uint8_t *p = &cmd[SW2_CMD_HEADER_LEN];
    uint16_t n = (uint16_t)(len - SW2_CMD_HEADER_LEN);
    uint8_t sub = cmd[3];

    switch (cmd[0]) {
    case CMD_PAIRING:
        return handle_pairing(s, cmd, p, n, rsp, events);

    case CMD_FLASH:
        if (sub == 0x04 && n >= 8) {
            // [len][7e 00 00][addr LE32] -> [len 00 00 00][addr LE32][data]
            uint8_t rlen = p[0] > 0x40 ? 0x40 : p[0];
            uint32_t addr = (uint32_t)p[4] | ((uint32_t)p[5] << 8) |
                            ((uint32_t)p[6] << 16) | ((uint32_t)p[7] << 24);
            uint8_t out[8 + 0x40];
            memset(out, 0, 8);
            out[0] = rlen;
            memcpy(&out[4], &p[4], 4);
            switch2_flash_read(addr, rlen, &out[8]);
            return reply(cmd, rsp, out, (uint16_t)(8 + rlen));
        }
        return reply(cmd, rsp, NULL, 0);

    case CMD_UNKNOWN_07: {
        static const uint8_t z = 0x00;
        return reply(cmd, rsp, &z, 1);
    }

    case CMD_UNKNOWN_16: {
        static const uint8_t z[24] = {0};
        return (sub == 0x01) ? reply(cmd, rsp, z, sizeof(z)) : reply(cmd, rsp, NULL, 0);
    }

    case CMD_UNKNOWN_11: {
        // Both replies must match a real pad exactly or the console keeps re-probing.
        static const uint8_t r01[4] = { 0x01, 0x00, 0x00, 0x00 };
        static const uint8_t r03[29] = {
            0x01, 0x20, 0x03, 0x00, 0x00, 0x0a, 0xe8, 0x1c, 0x3b, 0x79, 0x7d, 0x8b, 0x3a, 0x0a, 0xe8,
            0x9c, 0x42, 0x58, 0xa0, 0x0b, 0x42, 0x0a, 0xe8, 0x9c, 0x41, 0x58, 0xa0, 0x0b, 0x41,
        };
        if (sub == 0x01) return reply(cmd, rsp, r01, sizeof(r01));
        if (sub == 0x03) return reply(cmd, rsp, r03, sizeof(r03));
        return reply(cmd, rsp, NULL, 0);
    }

    case CMD_UNKNOWN_18: {
        static const uint8_t r01[8] = { 0x00, 0x00, 0x40, 0xf0, 0x00, 0x00, 0x60, 0x00 };
        return (sub == 0x01) ? reply(cmd, rsp, r01, sizeof(r01)) : reply(cmd, rsp, NULL, 0);
    }

    case CMD_NFC: {
        static const uint8_t r0c[4] = { 0x61, 0x12, 0x50, 0x0d };
        return (sub == 0x0c) ? reply(cmd, rsp, r0c, sizeof(r0c)) : reply(cmd, rsp, NULL, 0);
    }

    case CMD_FEATURE: {
        uint8_t out[12] = {0};
        if (sub == 0x01) {                  // query: 4 zero bytes + per-feature info
            out[4] = 0x07; out[5] = 0x07; out[6] = 0x01;
            return reply(cmd, rsp, out, 12);
        }
        if (n >= 1) {
            if (sub == 0x02) {
                // Set mask. On reconnect the console only sends this (never 0x04), so
                // treat it as enabling those features as well.
                s->feature_mask = p[0];
                s->features = p[0];
            } else if (sub == 0x04) {
                s->features |= (uint8_t)(p[0] & s->feature_mask);
            } else if (sub == 0x05) {
                s->features &= (uint8_t)~p[0];
            } else if (sub == 0x03) {
                s->feature_mask = 0;
            }
        }
        return reply(cmd, rsp, out, 4);
    }

    case CMD_FW_INFO: {
        // fw 2.1.4 | type 02 = Pro | BT patch 12.0.0 | pad | DSP 2.3.0 | pad. Current enough
        // not to trigger the update prompt; the DSP version matches the headset attributes.
        static const uint8_t fw[12] = { 0x02, 0x01, 0x04, 0x02, 0x0c, 0x00, 0x00, 0x00,
                                        0x00, 0x02, 0x03, 0x00 };
        return (sub == 0x01) ? reply(cmd, rsp, fw, sizeof(fw)) : reply(cmd, rsp, NULL, 0);
    }

    case CMD_PLAYER_LED:
        if (sub == 0x07 && n >= 1) {
            s->player_leds = p[0] & 0x0f;
            *events |= SW2_EVT_PLAYER_LED;
        } else if (sub >= 0x01 && sub <= 0x04) {
            s->player_leds = (uint8_t)(1u << (sub - 1));
            *events |= SW2_EVT_PLAYER_LED;
        }
        return reply(cmd, rsp, NULL, 0);

    case CMD_INIT:
    case CMD_VIBRATION:
    case CMD_FW_UPDATE:     // header-only ACK: never offer an update
    default:
        return reply(cmd, rsp, NULL, 0);
    }
}

// ---------------------------------------------------------------------------
// Input report 0x09
// ---------------------------------------------------------------------------

// 0..255 (128 = center) -> 12-bit around the factory calibration. `up_positive`
// flips HID Y (0 = up) into Nintendo Y (larger = up).
static uint16_t encode_axis(uint8_t v, const axis_cal_t *cal, bool up_positive)
{
    // 128 is center, so the two halves span 127 (up to 255) and 128 (down to 0) steps.
    int num = (int)v - 128;
    int den = (v >= 128) ? 127 : 128;
    if (up_positive) num = -num;
    int out;
    if (num >= 0) out = cal->neutral + (num * cal->max + den / 2) / den;
    else          out = cal->neutral - ((-num) * cal->min + den / 2) / den;
    if (out < 0) out = 0;
    if (out > 0xfff) out = 0xfff;
    return (uint16_t)out;
}

static void pack_stick(uint8_t *o, uint16_t x, uint16_t y)
{
    o[0] = (uint8_t)(x & 0xff);
    o[1] = (uint8_t)(((x >> 8) & 0x0f) | ((y & 0x0f) << 4));
    o[2] = (uint8_t)(y >> 4);
}

void switch2_proto_build_input(switch2_proto_t *s, const switch2_input_t *in,
                               uint8_t out[SW2_INPUT_REPORT_LEN])
{
    memset(out, 0, SW2_INPUT_REPORT_LEN);
    out[0] = s->counter++;

    uint8_t level = (uint8_t)((in->battery_pct > 100 ? 100 : in->battery_pct) * 9 / 100);
    out[1] = (uint8_t)((level << 2) | (in->charging ? 0x02 : 0x00));

    uint32_t b = in->buttons;
    bool zl = (b & JP_BUTTON_L2) || in->l2 > 64;
    bool zr = (b & JP_BUTTON_R2) || in->r2 > 64;
    // Positional mapping, the inverse of the Switch 2 input driver (B1 = bottom = B).
    if (b & JP_BUTTON_B1) out[2] |= 0x01;   // B
    if (b & JP_BUTTON_B2) out[2] |= 0x02;   // A
    if (b & JP_BUTTON_B3) out[2] |= 0x04;   // Y
    if (b & JP_BUTTON_B4) out[2] |= 0x08;   // X
    if (b & JP_BUTTON_R1) out[2] |= 0x10;   // R
    if (zr)               out[2] |= 0x20;   // ZR
    if (b & JP_BUTTON_S2) out[2] |= 0x40;   // +
    if (b & JP_BUTTON_R3) out[2] |= 0x80;   // RS
    if (b & JP_BUTTON_DD) out[3] |= 0x01;
    if (b & JP_BUTTON_DR) out[3] |= 0x02;
    if (b & JP_BUTTON_DL) out[3] |= 0x04;
    if (b & JP_BUTTON_DU) out[3] |= 0x08;
    if (b & JP_BUTTON_L1) out[3] |= 0x10;   // L
    if (zl)               out[3] |= 0x20;   // ZL
    if (b & JP_BUTTON_S1) out[3] |= 0x40;   // -
    if (b & JP_BUTTON_L3) out[3] |= 0x80;   // LS
    if (b & JP_BUTTON_A1) out[4] |= 0x01;   // Home
    if (b & JP_BUTTON_A2) out[4] |= 0x02;   // Capture
    if (b & JP_BUTTON_R4) out[4] |= 0x04;   // GR
    if (b & JP_BUTTON_L4) out[4] |= 0x08;   // GL
    if (b & JP_BUTTON_A3) out[4] |= 0x10;   // C

    pack_stick(&out[5], encode_axis(in->lx, &k_cal_lx, false), encode_axis(in->ly, &k_cal_ly, true));
    pack_stick(&out[8], encode_axis(in->rx, &k_cal_rx, false), encode_axis(in->ry, &k_cal_ry, true));

    // 0x30, or 0x38 once rumble is enabled — the console drops reports whose flags
    // disagree with the features it enabled.
    out[0x0b] = (s->features & SW2_FEATURE_RUMBLE) ? 0x38 : 0x30;
    // Motion block present (length 0x28) once IMU is enabled; zero motion is accepted.
    if (s->features & SW2_FEATURE_IMU) out[0x0e] = 0x28;
}

// ---------------------------------------------------------------------------
// Advertising / rumble / LEDs
// ---------------------------------------------------------------------------

void switch2_build_mfr_data(switch2_adv_kind_t kind, const uint8_t host_addr_le[6],
                            uint8_t out[SW2_MFR_DATA_LEN])
{
    static const uint8_t base[SW2_MFR_DATA_LEN] = {
        0x53, 0x05, 0x01, 0x00, 0x03, 0x7e, 0x05, 0x69, 0x20, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    memcpy(out, base, SW2_MFR_DATA_LEN);
    if (kind != SW2_ADV_PAIRING && host_addr_le) {
        memcpy(&out[0x0c], host_addr_le, 6);
        if (kind == SW2_ADV_WAKE) out[0x0b] = 0x81;
    }
}

// One LRA block: [state: tid:4 ops:2 en:1 -][3 x op(5)]. op = LE32 {lf_freq:9 lf_en:1
// lf_amp:10 hf_freq:9 hf_en:1 - en:1} + hf_amp byte.
static uint8_t lra_amplitude(const uint8_t *blk)
{
    uint8_t ops = (blk[0] >> 4) & 0x03;
    if (!(blk[0] & 0x40) || ops == 0) return 0;
    uint16_t best = 0;
    for (uint8_t i = 0; i < ops; i++) {
        const uint8_t *op = &blk[1 + i * 5];
        uint32_t w = (uint32_t)op[0] | ((uint32_t)op[1] << 8) | ((uint32_t)op[2] << 16) |
                     ((uint32_t)op[3] << 24);
        uint16_t lf = (uint16_t)(((w >> 10) & 0x3ff) * 255u / 808u);   // 0..808 full scale
        uint16_t hf = (uint16_t)(op[4] * 255u / 95u);                  // 0..95 full scale
        if (lf > best) best = lf;
        if (hf > best) best = hf;
    }
    return best > 255 ? 255 : (uint8_t)best;
}

void switch2_rumble_decode(const uint8_t lra[32], uint8_t *left, uint8_t *right)
{
    *left = lra_amplitude(&lra[0]);
    *right = lra_amplitude(&lra[16]);
}

uint8_t switch2_player_from_leds(uint8_t leds)
{
    switch (leds & 0x0f) {
        case 0x01: return 1; case 0x03: return 2; case 0x07: return 3; case 0x0f: return 4;
        case 0x09: return 5; case 0x05: return 6; case 0x0d: return 7; case 0x06: return 8;
        case 0x02: return 2; case 0x04: return 3; case 0x08: return 4;
        default:   return 0;
    }
}
