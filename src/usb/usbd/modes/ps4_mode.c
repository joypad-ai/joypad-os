// ps4_mode.c - PlayStation 4 DualShock 4 USB device mode
// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Robert Dale Smith

#include "tusb.h"
#include "../usbd_mode.h"
#include "../usbd.h"
#include "descriptors/ps4_descriptors.h"
#include "core/buttons.h"
#include <string.h>
#include <stdint.h>
#include "pico/rand.h"
#include "mbedtls/pk.h"
#include "mbedtls/rsa.h"
#include "mbedtls/sha256.h"

extern const unsigned char key_pem_start[] asm("_binary_key_pem_start");
extern const unsigned char key_pem_end[] asm("_binary_key_pem_end");
extern const unsigned char ps4_serial_start[] asm("_binary_serial_txt_start");
extern const unsigned char ps4_serial_end[] asm("_binary_serial_txt_end");
extern const unsigned char ps4_signature_start[] asm("_binary_sig_bin_start");
extern const unsigned char ps4_signature_end[] asm("_binary_sig_bin_end");

// 计算大小的宏
#define KEY_PEM_SIZE (key_pem_end - key_pem_start)
#define PS4_SERIAL_SIZE (ps4_serial_end - ps4_serial_start)
#define PS4_SIGNATURE_SIZE (ps4_signature_end - ps4_signature_start)

// 认证状态枚举
typedef enum {
    PS4_AUTH_NO_NONCE = 0,
    PS4_AUTH_RECEIVING_NONCE = 1,
    PS4_AUTH_NONCE_READY = 2,
    PS4_AUTH_SIGNED_READY = 3
} ps4_auth_state_t;

// 认证相关静态变量
static ps4_auth_state_t ps4_auth_state;
static uint8_t ps4_auth_buffer[1064];  // 签名结果缓冲区
static uint8_t ps4_auth_nonce_buffer[256];  // nonce缓冲区
static uint8_t cur_nonce_id = 1;
static uint8_t send_nonce_part = 0;

// 存储转换后的二进制序列号
static uint8_t ps4_serial_binary[16] = {};

// mbedtls上下文
static mbedtls_pk_context pk;

// ============================================================================
// STATE
// ============================================================================

// Using raw byte buffer approach to avoid struct bitfield packing issues
static uint8_t ps4_report_buffer[64];
static ps4_out_report_t ps4_output;
static bool ps4_output_available = false;
static uint8_t ps4_report_counter = 0;

void pico_rand(void *buf, size_t len) {
    uint8_t *p = (uint8_t *)buf;
    while (len) {
        uint32_t r = get_rand_32();
        size_t n = len < 4 ? len : 4;
        memcpy(p, &r, n);
        p += n;
        len -= n;
    }
}

// CRC32实现 (IEEE 802.3标准)
static uint32_t ps4_crc32(uint32_t crc, const void *buf, size_t size) {
    static const uint32_t crc32_table[256] = {
        0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f, 0xe963a535, 0x9e6495a3,
        0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988, 0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91,
        0x1db71064, 0x6ab020f2, 0xf3b97148, 0x84be41de, 0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
        0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec, 0x14015c4f, 0x63066cd9, 0xfa0f3d63, 0x8d080df5,
        0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172, 0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,
        0x35b5a8fa, 0x42b2986c, 0xdbbbc9d6, 0xacbcf940, 0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
        0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423, 0xcfba9599, 0xb8bda50f,
        0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924, 0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,
        0x76dc4190, 0x01db7106, 0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
        0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d, 0x91646c97, 0xe6635c01,
        0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e, 0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457,
        0x65b0d9c6, 0x12b7e950, 0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
        0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7, 0xa4d1c46d, 0xd3d6f4fb,
        0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0, 0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9,
        0x5005713c, 0x270241aa, 0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
        0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81, 0xb7bd5c3b, 0xc0ba6cad,
        0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a, 0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683,
        0xe3630b12, 0x94643b84, 0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
        0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb, 0x196c3671, 0x6e6b06e7,
        0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc, 0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5,
        0xd6d6a3e8, 0xa1d1937e, 0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
        0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55, 0x316e8eef, 0x4669be79,
        0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236, 0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f,
        0xc5ba3bbe, 0xb2bd0b28, 0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
        0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f, 0x72076785, 0x05005713,
        0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38, 0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21,
        0x86d3d2d4, 0xf1d4e242, 0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
        0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69, 0x616bffd3, 0x166ccf45,
        0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2, 0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db,
        0xaed16a4a, 0xd9d65adc, 0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
        0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693, 0x54de5729, 0x23d967bf,
        0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94, 0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
    };
    
    const uint8_t *p = (const uint8_t *)buf;
    crc = ~crc;
    
    for (size_t i = 0; i < size; i++) {
        crc = crc32_table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    }
    
    return ~crc;
}

// Helper function to convert a hex character to its integer value
static uint8_t hex_char_to_int(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    } else if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return 0; // Invalid hex character
}

// Helper function to convert hex string to binary data
static void hex_string_to_binary(const unsigned char *hex_str, size_t hex_len, uint8_t *output, size_t output_size)
{
    size_t output_idx = 0;
    size_t i = 0;
    
    // Process the hex string in pairs of characters
    while (i < hex_len && output_idx < output_size) {
        // Skip non-hex characters (like spaces or newlines)
        if (!((hex_str[i] >= '0' && hex_str[i] <= '9') || 
              (hex_str[i] >= 'A' && hex_str[i] <= 'F') || 
              (hex_str[i] >= 'a' && hex_str[i] <= 'f'))) {
            i++;
            continue;
        }
        
        // Make sure we have a pair of hex characters
        if (i + 1 < hex_len) {
            uint8_t high_nibble = hex_char_to_int(hex_str[i]);
            uint8_t low_nibble = hex_char_to_int(hex_str[i + 1]);
            output[output_idx] = (high_nibble << 4) | low_nibble;
            output_idx++;
            i += 2; // Move to next pair
        } else {
            // Odd number of hex characters - treat as error, skip
            i++;
        }
    }
    
    // If we have space left in the output buffer, pad with zeros at the beginning
    if (output_idx < output_size) {
        // Shift the data to the right and fill the beginning with zeros
        size_t data_start = output_size - output_idx;
        // Move existing data to the right
        for (int j = output_idx - 1; j >= 0; j--) {
            output[data_start + j] = output[j];
        }
        // Fill the beginning with zeros
        for (size_t j = 0; j < data_start; j++) {
            output[j] = 0;
        }
    }
}

// 随机数生成函数
static int ps4_rng(void *p_rng, unsigned char *p, size_t len) {
    (void)p_rng;
    pico_rand(p, len);
    return 0;
}

// RSA-PSS签名函数
static void ps4_sign_nonce(void) {
    if (ps4_auth_state == PS4_AUTH_NONCE_READY) {
        uint8_t hashed_nonce[32];
        uint8_t nonce_signature[256];
        
        // SHA256哈希
        if (mbedtls_sha256(ps4_auth_nonce_buffer, 256, hashed_nonce, 0) != 0) {
            printf("PS4: SHA256 failed\n");
            return;
        }
        
        // RSA-PSS签名 (mbedtls v3.x API)
        mbedtls_rsa_context *rsa = mbedtls_pk_rsa(pk);
        int ret = mbedtls_rsa_rsassa_pss_sign(rsa, ps4_rng, NULL, 
                                            MBEDTLS_MD_SHA256, 32, hashed_nonce,
                                            nonce_signature);
        if (ret != 0) {
            printf("PS4: RSA signature failed: %d\n", ret);
            return;
        }
        
        // 构建完整签名数据包
        int offset = 0;
        memcpy(&ps4_auth_buffer[offset], nonce_signature, 256);
        offset += 256;
        memcpy(&ps4_auth_buffer[offset], ps4_serial_binary, 16);
        offset += 16;
        
        // 导出RSA参数 - 使用mbedtls_rsa_export_raw (mbedTLS v3.x兼容)
        unsigned char n_buf[256] = {0};
        unsigned char e_buf[256] = {0};
        
        // int export_ret = mbedtls_rsa_export_raw(rsa, NULL, 0, NULL, 0, NULL, 0, d_buf, 256, e_buf, 256);
        int export_ret = mbedtls_rsa_export_raw(rsa, 
                                                n_buf, 256,  // N - 模数
                                                NULL, 0,     // P - 不需要
                                                NULL, 0,     // Q - 不需要
                                                NULL, 0,     // D - 不需要
                                                e_buf, 256); // E - 公钥指数
        
        if (export_ret == 0) {
            memcpy(&ps4_auth_buffer[offset], n_buf, 256);
            offset += 256;
            memcpy(&ps4_auth_buffer[offset], e_buf, 256);
            offset += 256;
        } else {
            // 如果导出失败，填充零
            memset(&ps4_auth_buffer[offset], 0, 256);
            offset += 256;
            memset(&ps4_auth_buffer[offset], 0, 256);
            offset += 256;
            printf("PS4: RSA export failed: %d\n", export_ret);
        }
        memcpy(&ps4_auth_buffer[offset], ps4_signature_start, 256);
        offset += 256;
        memset(&ps4_auth_buffer[offset], 0, 24);
        
        ps4_auth_state = PS4_AUTH_SIGNED_READY;
        printf("PS4: Authentication signature ready\n");
    }
}

// Nonce处理函数
static void ps4_save_nonce(uint8_t nonce_id, uint8_t nonce_page, 
                          uint8_t *buffer, uint16_t buflen) {
    if (nonce_page != 0 && nonce_id != cur_nonce_id) {
        ps4_auth_state = PS4_AUTH_NO_NONCE;
        return;
    }
    
    memcpy(&ps4_auth_nonce_buffer[nonce_page * 56], buffer, buflen);
    
    if (nonce_page == 4) {
        ps4_auth_state = PS4_AUTH_NONCE_READY;
        ps4_sign_nonce();
    } else if (nonce_page == 0) {
        cur_nonce_id = nonce_id;
        ps4_auth_state = PS4_AUTH_RECEIVING_NONCE;
    }
}

// 认证初始化函数
static void ps4_auth_init(void) {
    ps4_auth_state = PS4_AUTH_NO_NONCE;
    memset(ps4_auth_buffer, 0, sizeof(ps4_auth_buffer));
    memset(ps4_auth_nonce_buffer, 0, sizeof(ps4_auth_nonce_buffer));
    
    // Convert hex string from serial.txt to binary and store in global variable
    size_t serial_len = ps4_serial_end - ps4_serial_start;
    hex_string_to_binary(ps4_serial_start, serial_len, ps4_serial_binary, 16);
    
    // 初始化mbedtls和私钥
    mbedtls_pk_init(&pk);
    int ret = mbedtls_pk_parse_key(&pk, (uint8_t *)key_pem_start, 
                                   key_pem_end - key_pem_start, NULL, 0, 
                                   ps4_rng, NULL);
    if (ret != 0) {
        printf("PS4 auth init failed: -0x%04x\n", (unsigned int)-ret);
    } else {
        printf("PS4 auth initialized successfully\n");
    }
}

// ============================================================================
// MODE INTERFACE IMPLEMENTATION
// ============================================================================

static void ps4_mode_init(void)
{
    // Initialize PS4 report to neutral state (raw buffer approach)
    memset(ps4_report_buffer, 0, sizeof(ps4_report_buffer));
    ps4_report_buffer[0] = 0x01;  // Report ID
    ps4_report_buffer[1] = 0x80;  // LX center
    ps4_report_buffer[2] = 0x80;  // LY center
    ps4_report_buffer[3] = 0x80;  // RX center
    ps4_report_buffer[4] = 0x80;  // RY center
    ps4_report_buffer[5] = PS4_HAT_NOTHING;  // D-pad neutral (0x0F), no buttons
    // Bytes 6-9: buttons and triggers already 0
    // Set touchpad fingers to unpressed
    ps4_report_buffer[35] = 0x80;  // touchpad p1 unpressed
    ps4_report_buffer[39] = 0x80;  // touchpad p2 unpressed
    memset(&ps4_output, 0, sizeof(ps4_out_report_t));
    ps4_report_counter = 0;
    
    // 初始化PS4认证
    ps4_auth_init();
}

static bool ps4_mode_is_ready(void)
{
    return tud_hid_ready();
}

// Send PS4 report (PlayStation 4 DualShock 4 mode)
// Uses raw byte array approach to avoid struct bitfield packing issues
//
// PS4 Report Layout (64 bytes):
//   Byte 0:    Report ID (0x01)
//   Byte 1:    Left stick X (0x00-0xFF, 0x80 center)
//   Byte 2:    Left stick Y (0x00-0xFF, 0x80 center)
//   Byte 3:    Right stick X (0x00-0xFF, 0x80 center)
//   Byte 4:    Right stick Y (0x00-0xFF, 0x80 center)
//   Byte 5:    D-pad (bits 0-3) + Square/Cross/Circle/Triangle (bits 4-7)
//   Byte 6:    L1/R1/L2/R2/Share/Options/L3/R3 (bits 0-7)
//   Byte 7:    PS (bit 0) + Touchpad (bit 1) + Counter (bits 2-7)
//   Byte 8:    Left trigger analog (0x00-0xFF)
//   Byte 9:    Right trigger analog (0x00-0xFF)
//   Bytes 10-63: Timestamp, sensor data, touchpad data, padding
static bool ps4_mode_send_report(uint8_t player_index,
                                  const input_event_t* event,
                                  const profile_output_t* profile_out,
                                  uint32_t buttons)
{
    (void)player_index;
    (void)event;

    // Byte 0: Report ID
    ps4_report_buffer[0] = 0x01;

    // Bytes 1-4: Analog sticks (HID convention: 0=up, 255=down - no inversion needed)
    ps4_report_buffer[1] = profile_out->left_x;          // LX
    ps4_report_buffer[2] = profile_out->left_y;          // LY
    ps4_report_buffer[3] = profile_out->right_x;         // RX
    ps4_report_buffer[4] = profile_out->right_y;         // RY

    // Byte 5: D-pad (bits 0-3) + face buttons (bits 4-7)
    uint8_t up = (buttons & JP_BUTTON_DU) ? 1 : 0;
    uint8_t down = (buttons & JP_BUTTON_DD) ? 1 : 0;
    uint8_t left = (buttons & JP_BUTTON_DL) ? 1 : 0;
    uint8_t right = (buttons & JP_BUTTON_DR) ? 1 : 0;

    uint8_t dpad;
    if (up && right)        dpad = PS4_HAT_UP_RIGHT;
    else if (up && left)    dpad = PS4_HAT_UP_LEFT;
    else if (down && right) dpad = PS4_HAT_DOWN_RIGHT;
    else if (down && left)  dpad = PS4_HAT_DOWN_LEFT;
    else if (up)            dpad = PS4_HAT_UP;
    else if (down)          dpad = PS4_HAT_DOWN;
    else if (left)          dpad = PS4_HAT_LEFT;
    else if (right)         dpad = PS4_HAT_RIGHT;
    else                    dpad = PS4_HAT_NOTHING;

    uint8_t face_buttons = 0;
    if (buttons & JP_BUTTON_B3) face_buttons |= 0x10;  // Square
    if (buttons & JP_BUTTON_B1) face_buttons |= 0x20;  // Cross
    if (buttons & JP_BUTTON_B2) face_buttons |= 0x40;  // Circle
    if (buttons & JP_BUTTON_B4) face_buttons |= 0x80;  // Triangle

    ps4_report_buffer[5] = dpad | face_buttons;

    // Byte 6: Shoulder buttons + other buttons
    uint8_t byte6 = 0;
    if (buttons & JP_BUTTON_L1) byte6 |= 0x01;  // L1
    if (buttons & JP_BUTTON_R1) byte6 |= 0x02;  // R1
    if (buttons & JP_BUTTON_L2) byte6 |= 0x04;  // L2 (digital)
    if (buttons & JP_BUTTON_R2) byte6 |= 0x08;  // R2 (digital)
    if (buttons & JP_BUTTON_S1) byte6 |= 0x10;  // Share
    if (buttons & JP_BUTTON_S2) byte6 |= 0x20;  // Options
    if (buttons & JP_BUTTON_L3) byte6 |= 0x40;  // L3
    if (buttons & JP_BUTTON_R3) byte6 |= 0x80;  // R3
    ps4_report_buffer[6] = byte6;

    // Byte 7: PS + Touchpad + Counter (6-bit)
    uint8_t byte7 = 0;
    if (buttons & JP_BUTTON_A1) byte7 |= 0x01;  // PS button
    if (buttons & JP_BUTTON_A2) byte7 |= 0x02;  // Touchpad click
    byte7 |= ((ps4_report_counter++ & 0x3F) << 2);       // Counter in bits 2-7
    ps4_report_buffer[7] = byte7;

    // Bytes 8-9: Analog triggers
    ps4_report_buffer[8] = profile_out->l2_analog;  // Left trigger
    ps4_report_buffer[9] = profile_out->r2_analog;  // Right trigger

    // Bytes 10-11: Timestamp (we can just increment)
    // Bytes 12-63: Leave as initialized (sensor data, touchpad, padding)

    // Send with report_id=0x01, letting TinyUSB prepend it
    // Skip byte 0 of buffer (our report_id) and send 63 bytes of data
    return tud_hid_report(0x01, &ps4_report_buffer[1], 63);
}

static void ps4_mode_handle_output(uint8_t report_id, const uint8_t* data, uint16_t len)
{
    // PS4 output report (rumble/LED) - Report ID 5
    if (report_id == PS4_REPORT_ID_OUTPUT && len >= sizeof(ps4_out_report_t)) {
        memcpy(&ps4_output, data, sizeof(ps4_out_report_t));
        ps4_output_available = true;
    }

    // PS4 auth feature reports (set)
    // Note: Feature reports are typically handled via tud_hid_set_report_cb
    // This handle_output is for interrupt OUT endpoint reports
}

static uint8_t ps4_mode_get_rumble(void)
{
    // PS4 has motor_left (large) and motor_right (small) 8-bit values
    return (ps4_output.motor_left > ps4_output.motor_right)
           ? ps4_output.motor_left : ps4_output.motor_right;
}

static bool ps4_mode_get_feedback(output_feedback_t* fb)
{
    if (!ps4_output_available) return false;

    // PS4 has two 8-bit motors and RGB lightbar
    fb->rumble_left = ps4_output.motor_left;
    fb->rumble_right = ps4_output.motor_right;
    fb->led_r = ps4_output.lightbar_red;
    fb->led_g = ps4_output.lightbar_green;
    fb->led_b = ps4_output.lightbar_blue;

    fb->dirty = true;
    ps4_output_available = false;
    return true;
}

static uint16_t ps4_mode_get_report(uint8_t report_id, hid_report_type_t report_type,
                                     uint8_t* buffer, uint16_t reqlen)
{
    if (report_type != HID_REPORT_TYPE_FEATURE) {
        return 0;
    }

    uint16_t len = 0;
    switch (report_id) {
        case PS4_REPORT_ID_FEATURE_03:
            // Controller definition report - return GP2040-CE compatible data
            len = sizeof(ps4_feature_03);
            if (reqlen < len) len = reqlen;
            memcpy(buffer, ps4_feature_03, len);
            return len;

        case PS4_REPORT_ID_AUTH_RESPONSE:   // 0xF1 - Signature from DS4
            // 使用本地认证逻辑，不再依赖passthrough
            len = 64;
            if (reqlen < len) len = reqlen;
            
            if (ps4_auth_state == PS4_AUTH_SIGNED_READY) {
                uint8_t data[64] = {};
                uint32_t crc32 = 0;
                
                data[0] = 0xF1;
                data[1] = cur_nonce_id;
                data[2] = send_nonce_part;
                data[3] = 0;
                
                memcpy(&data[4], &ps4_auth_buffer[send_nonce_part * 56], 56);
                crc32 = ps4_crc32(crc32, data, 60);
                memcpy(&data[60], &crc32, sizeof(uint32_t));
                memcpy(buffer, &data[1], 63);
                
                if ((++send_nonce_part) == 19) {
                    ps4_auth_state = PS4_AUTH_NO_NONCE;
                    send_nonce_part = 0;
                }
                return 63;
            } else {
                memset(buffer, 0, len);
                return len;
            }

        case PS4_REPORT_ID_AUTH_STATUS:     // 0xF2 - Signing status
            // 使用本地认证状态
            len = 16;
            if (reqlen < len) len = reqlen;
            
            uint8_t data[16] = {};
            data[0] = 0xF2;
            data[1] = cur_nonce_id;
            data[2] = ps4_auth_state == PS4_AUTH_SIGNED_READY ? 0 : 16;
            memset(&data[3], 0, 9);
            uint32_t crc32 = 0;
            crc32 = ps4_crc32(crc32, data, 12);
            memcpy(&data[12], &crc32, sizeof(uint32_t));
            memcpy(buffer, &data[1], 15);
            return 15;

        case PS4_REPORT_ID_AUTH_PAYLOAD:    // 0xF0 - handled in set_report
            len = 64;
            if (reqlen < len) len = reqlen;
            memset(buffer, 0, len);
            return len;

        case PS4_REPORT_ID_AUTH_RESET:      // 0xF3 - Return page size info
            // 重置本地认证状态
            ps4_auth_state = PS4_AUTH_NO_NONCE;
            send_nonce_part = 0;
            len = sizeof(ps4_feature_f3);
            if (reqlen < len) len = reqlen;
            memcpy(buffer, ps4_feature_f3, len);
            return len;

        default:
            return 0;
    }
}

// Handle PS4 auth SET_REPORT (nonce from console, etc.)
// This is called from usbd.c's tud_hid_set_report_cb for feature reports
void ps4_mode_set_feature_report(uint8_t report_id, const uint8_t* buffer, uint16_t bufsize)
{
    switch (report_id) {
        case PS4_REPORT_ID_AUTH_PAYLOAD:    // 0xF0 - Nonce from console
            // 处理来自控制台的nonce，使用本地认证
            if (bufsize == 63) {
                uint8_t temp_buffer[64];
                temp_buffer[0] = report_id;
                memcpy(&temp_buffer[1], buffer, bufsize);
                
                uint32_t crc32 = ps4_crc32(0, temp_buffer, bufsize + 1 - sizeof(uint32_t));
                if (crc32 == *((uint32_t *)&temp_buffer[bufsize + 1 - sizeof(uint32_t)])) {
                    uint8_t nonce_id = buffer[0];
                    uint8_t nonce_page = buffer[1];
                    
                    // 计算nonce数据长度
                    uint16_t noncelen;
                    if (nonce_page == 4) {
                        noncelen = 32;  // 最后一页可能只有32字节数据
                    } else {
                        noncelen = 56;  // 其他页56字节数据
                    }
                    
                    uint8_t nonce[56];
                    memcpy(nonce, &buffer[4], noncelen);
                    ps4_save_nonce(nonce_id, nonce_page, nonce, noncelen);
                } else {
                    printf("PS4: CRC32 failed on set report\n");
                }
            }
            break;

        case PS4_REPORT_ID_AUTH_RESET:      // 0xF3 - Reset auth
            ps4_auth_state = PS4_AUTH_NO_NONCE;
            send_nonce_part = 0;
            break;

        default:
            break;
    }
}

static const uint8_t* ps4_mode_get_device_descriptor(void)
{
    return (const uint8_t*)&ps4_device_descriptor;
}

static const uint8_t* ps4_mode_get_config_descriptor(void)
{
    return ps4_config_descriptor;
}

static const uint8_t* ps4_mode_get_report_descriptor(void)
{
    return ps4_report_descriptor;
}

// ============================================================================
// MODE EXPORT
// ============================================================================

const usbd_mode_t ps4_mode = {
    .name = "PS4",
    .mode = USB_OUTPUT_MODE_PS4,

    .get_device_descriptor = ps4_mode_get_device_descriptor,
    .get_config_descriptor = ps4_mode_get_config_descriptor,
    .get_report_descriptor = ps4_mode_get_report_descriptor,

    .init = ps4_mode_init,
    .send_report = ps4_mode_send_report,
    .is_ready = ps4_mode_is_ready,

    .handle_output = ps4_mode_handle_output,
    .get_rumble = ps4_mode_get_rumble,
    .get_feedback = ps4_mode_get_feedback,
    .get_report = ps4_mode_get_report,
    .get_class_driver = NULL,
    .task = NULL,
};
