// wii_ext_device.c - Wii extension I2C-slave device driver
//
// A Wiimote (the I2C master) polls its extension port by issuing short
// write-then-read transactions at 400 kHz:
//   1. `start | addr<<1|W | reg_to_read | stop`          — set cursor
//   2. `start | addr<<1|R | data*N | stop`               — read N bytes
// The slave just needs a 256-byte register file with auto-incrementing
// reads and minimal handling of a few special writes (0xF0 init,
// 0xFB init-2, 0xFE mode select). We implement exactly that.

#include "wii_ext_device.h"
#include "wii_ext_crypto.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include "core/services/storage/flash.h"
#include "core/output_interface.h"
#include "platform/platform.h"
#include "pico/i2c_slave.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "tusb.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define WII_I2C_ADDR           0x52
#define WII_DEV_I2C_INST       i2c0
#define REG_FILE_SIZE          256

// ---- Register file + I2C slave state ---------------------------------------

static volatile uint8_t  reg_file[REG_FILE_SIZE];
static volatile uint16_t cursor = 0;     // current register pointer
static volatile bool     addr_received = false;  // did master send a reg address this transaction?

static wii_device_emulation_t emulation_kind = WII_DEV_EMULATE_CLASSIC;

// ---- Calibration block ------------------------------------------------------

// Calibration block (16 bytes at reg 0x20, mirrored at 0x30). Values are
// in 8-bit scale matching Dolphin's emulated Classic Controller:
//   CAL_STICK_CENTER 0x80, STICK_GATE_RADIUS 0x61
//   → max = 0xE1, min = 0x1F, center = 0x80 (per stick axis)
// Checksum format (per Dolphin's UpdateCalibrationDataChecksum):
//   byte 14 = (sum of bytes 0..13 + 0x55) & 0xFF
//   byte 15 = (byte 14 + 0x55) & 0xFF
// Both bytes are 8-bit; the 2nd is the 1st plus 0x55. The Wii System
// Menu verifies this checksum and falls back to internal defaults if
// it's wrong, which manifests as analog axes mapping incorrectly even
// though buttons work normally.
static void seed_calibration(void)
{
    uint8_t cal[16] = {
        0xE1, 0x1F, 0x80,  // LX max, min, center
        0xE1, 0x1F, 0x80,  // LY
        0xE1, 0x1F, 0x80,  // RX
        0xE1, 0x1F, 0x80,  // RY
        0x00, 0x00,        // LT, RT neutral
        0x00, 0x00,        // checksum (filled below)
    };
    uint8_t checksum = 0x55;
    for (int i = 0; i < 14; i++) checksum += cal[i];
    cal[14] = checksum;
    cal[15] = (uint8_t)(checksum + 0x55);

    memcpy((void *)&reg_file[0x20], cal, 16);
    memcpy((void *)&reg_file[0x30], cal, 16);  // mirror
}

static void seed_id_bytes(wii_device_emulation_t k)
{
    // [2][3] are the always-0xA4 0x20 "Wii extension" sanity prefix.
    // [4] is the data-type/mode byte — REFLECTS THE CURRENT REPORT FORMAT,
    // not a fixed "Pro magic". Real Classic Pro at rest reports id[4]=0x01
    // (format 1, default). The host writes 0x03 to 0xFE to switch to
    // format 3, and at that point id[4] becomes 0x03. We mirror this
    // behavior — default to 0x01, update on FE write.
    // [5] is the family selector (00=Nunchuck, 01=Classic, 03=Guitar...).
    // [0] distinguishes within a family (Classic Pro reports 0x01).
    uint8_t id[6];
    id[2] = 0xA4;
    id[3] = 0x20;
    id[4] = 0x01;
    switch (k) {
        case WII_DEV_EMULATE_CLASSIC:
            id[0] = 0x00; id[5] = 0x01;
            break;
        case WII_DEV_EMULATE_CLASSIC_PRO:
            // Pro defaults to format 0x01 (6-byte). Both libogc-based
            // homebrew tester apps and the Wii System Menu read us in
            // format 0x01 by default (neither writes 0xFE to switch us).
            // id[0]=0x01 distinguishes Pro within the Classic family.
            id[0] = 0x01; id[5] = 0x01;
            break;
        case WII_DEV_EMULATE_NUNCHUCK:
            id[0] = 0x00; id[4] = 0x00; id[5] = 0x00;
            break;
    }
    id[1] = 0x00;
    memcpy((void *)&reg_file[0xFA], id, 6);
}

// Seed neutral report. Default mode follows id[4] (Pro defaults to 0x03,
// Classic to 0x01). Encryption (if negotiated) wraps this in the read
// handler — the register file itself stays plaintext.
static void seed_neutral_report(void)
{
    uint8_t fmt = reg_file[0xFE];  // already set by seed_id_bytes via id[4]
    if (fmt == 0x03) {
        // Format 0x03: 8-bit per axis, center 0x80, triggers 0x00.
        reg_file[0x00] = 0x80;  // LX
        reg_file[0x01] = 0x80;  // RX
        reg_file[0x02] = 0x80;  // LY
        reg_file[0x03] = 0x80;  // RY
        reg_file[0x04] = 0x00;  // LT
        reg_file[0x05] = 0x00;  // RT
        reg_file[0x06] = 0xFF;  // buttons hi unpressed
        reg_file[0x07] = 0xFF;  // buttons lo unpressed
    } else {
        // Format 0x01: 6-bit packed.
        reg_file[0x00] = 0xA0;  // RX<4:3>=10, LX=0x20
        reg_file[0x01] = 0x20;  // RX<2:1>=00, LY=0x20
        reg_file[0x02] = 0x10;  // RX<0>=0, LT<4:3>=0, RY=0x10
        reg_file[0x03] = 0x00;  // LT<2:0>=0, RT=0
        reg_file[0x04] = 0xFF;  // buttons unpressed
        reg_file[0x05] = 0xFF;
        // Seed bytes 6/7 too in case host reads more than 6 bytes and
        // interprets as buttons (active-low → 0x00 would mean "pressed").
        reg_file[0x06] = 0xFF;
        reg_file[0x07] = 0xFF;
    }

    static const uint8_t init7d[6] = { 0x00, 0x80, 0x14, 0x82, 0x00, 0x08 };
    memcpy((void *)&reg_file[0x7D], init7d, sizeof(init7d));
}

// ---- Event → report packer (format 0x01 default; 0x03 when host requests) --

static void pack_report_from_event(const input_event_t *ev)
{
    // Buttons — active-LOW: 1 = unpressed, 0 = pressed. Same bit layout
    // in both format 0x01 and 0x03; just placed at different offsets.
    uint8_t btn_lo = 0xFF, btn_hi = 0xFF;
    uint32_t btn = ev->buttons;
    if (btn & JP_BUTTON_DR) btn_lo &= ~(1u << 7);
    if (btn & JP_BUTTON_DD) btn_lo &= ~(1u << 6);
    if (btn & JP_BUTTON_L1) btn_lo &= ~(1u << 5);
    if (btn & JP_BUTTON_S1) btn_lo &= ~(1u << 4);
    if (btn & JP_BUTTON_A1) btn_lo &= ~(1u << 3);
    if (btn & JP_BUTTON_S2) btn_lo &= ~(1u << 2);
    if (btn & JP_BUTTON_R1) btn_lo &= ~(1u << 1);
    if (btn & JP_BUTTON_L2) btn_hi &= ~(1u << 7);
    if (btn & JP_BUTTON_B1) btn_hi &= ~(1u << 6);
    if (btn & JP_BUTTON_B3) btn_hi &= ~(1u << 5);
    if (btn & JP_BUTTON_B2) btn_hi &= ~(1u << 4);
    if (btn & JP_BUTTON_B4) btn_hi &= ~(1u << 3);
    if (btn & JP_BUTTON_R2) btn_hi &= ~(1u << 2);
    if (btn & JP_BUTTON_DL) btn_hi &= ~(1u << 1);
    if (btn & JP_BUTTON_DU) btn_hi &= ~(1u << 0);

    if (reg_file[0xFE] == 0x03) {
        // Format 0x03 — 8 bytes, 8-bit per axis. Required by Wii System
        // Menu (per wiibrew: the Wii Main Menu sets the data format byte
        // to 0x03). Empirically the Menu inverts both X and Y from HID
        // convention. X axes use centered signed-delta math (HID 128 →
        // exactly 0x80) which gave clean cursor behavior. Y axes use
        // simple linear inversion (which behaved differently from the
        // centered math even with identical inputs — Y appears to use
        // a different range or deadzone in the Menu's interpretation).
        #define WII_FMT3_X(hid) ({                                         \
            int _delta = 128 - (int)(hid);                                 \
            int _scaled = (_delta * 0x61) / 128;                           \
            int _v = 0x80 + _scaled;                                       \
            if (_v < 0x1F) _v = 0x1F;                                      \
            if (_v > 0xE1) _v = 0xE1;                                      \
            (uint8_t)_v;                                                   \
        })
        uint8_t lx = WII_FMT3_X(ev->analog[ANALOG_LX]);
        uint8_t rx = WII_FMT3_X(ev->analog[ANALOG_RX]);
        uint8_t ly = (uint8_t)(0x1F + ((uint16_t)(255 - ev->analog[ANALOG_LY]) * 0xC2 / 255));
        uint8_t ry = (uint8_t)(0x1F + ((uint16_t)(255 - ev->analog[ANALOG_RY]) * 0xC2 / 255));
        #undef WII_FMT3_X
        uint8_t lt = ev->analog[ANALOG_L2];
        uint8_t rt = ev->analog[ANALOG_R2];

        uint32_t irq_state = save_and_disable_interrupts();
        reg_file[0x00] = lx;
        reg_file[0x01] = rx;
        reg_file[0x02] = ly;
        reg_file[0x03] = ry;
        reg_file[0x04] = lt;
        reg_file[0x05] = rt;
        reg_file[0x06] = btn_lo;
        reg_file[0x07] = btn_hi;
        restore_interrupts(irq_state);
        return;
    }

    // Format 0x01: 6-bit LX/LY + 5-bit RX/RY + 5-bit triggers. The Wii
    // upscales 6-bit values ×4 to compare against the 8-bit cal block,
    // so report values must stay inside (cal_min/4 .. cal_max/4) =
    // (7.75 .. 56.25) — i.e. 8..56 inclusive. Values below cal_min/4
    // make the Wii System Menu's axis math mis-map (wrong direction).
    // Y axes: CC convention is 0=down, 255=up — invert HID Y first.
    uint8_t lx = (uint8_t)(8 + ((uint16_t)ev->analog[ANALOG_LX] * 48 / 255));
    uint8_t ly = (uint8_t)(8 + ((uint16_t)(255 - ev->analog[ANALOG_LY]) * 48 / 255));
    uint8_t rx = (uint8_t)(4 + ((uint16_t)ev->analog[ANALOG_RX] * 24 / 255));
    uint8_t ry = (uint8_t)(4 + ((uint16_t)(255 - ev->analog[ANALOG_RY]) * 24 / 255));
    uint8_t lt = ev->analog[ANALOG_L2] >> 3;
    uint8_t rt = ev->analog[ANALOG_R2] >> 3;

    uint8_t b0 = (uint8_t)((rx & 0x18) << 3) | (lx & 0x3F);
    uint8_t b1 = (uint8_t)((rx & 0x06) << 5) | (ly & 0x3F);
    uint8_t b2 = (uint8_t)((rx & 0x01) << 7) | (uint8_t)((lt & 0x18) << 2) | (ry & 0x1F);
    uint8_t b3 = (uint8_t)((lt & 0x07) << 5) | (rt & 0x1F);

    uint32_t irq_state = save_and_disable_interrupts();
    reg_file[0x00] = b0;
    reg_file[0x01] = b1;
    reg_file[0x02] = b2;
    reg_file[0x03] = b3;
    reg_file[0x04] = btn_lo;
    reg_file[0x05] = btn_hi;
    restore_interrupts(irq_state);
}

// ---- Router tap: called for every input event routed to OUTPUT_TARGET_WII_EXTENSION --

static void wii_device_router_tap(output_target_t output, uint8_t player_index,
                                  const input_event_t *event)
{
    (void)output;
    (void)player_index;
    if (event) pack_report_from_event(event);
}

// ---- Debug log (ring buffer; drained from task, never printed in ISR) ------

typedef struct {
    uint8_t kind;  // 'A'=addr seek, 'W'=write, 'R'=first read of transaction
    uint8_t reg;
    uint8_t val;
} wii_log_event_t;

#define WII_LOG_BUF_SIZE 256
static volatile wii_log_event_t wii_log_buf[WII_LOG_BUF_SIZE];
static volatile uint16_t wii_log_head = 0;
static volatile uint16_t wii_log_tail = 0;
static volatile uint16_t wii_log_dropped = 0;
static volatile bool    tx_first_read = true;

static inline void wii_log_push(uint8_t kind, uint8_t reg, uint8_t val) {
    uint16_t next = (uint16_t)((wii_log_head + 1) % WII_LOG_BUF_SIZE);
    if (next == wii_log_tail) {
        wii_log_dropped++;
        return;
    }
    wii_log_buf[wii_log_head].kind = kind;
    wii_log_buf[wii_log_head].reg  = reg;
    wii_log_buf[wii_log_head].val  = val;
    wii_log_head = next;
}

static void wii_log_drain(void) {
    while (wii_log_tail != wii_log_head) {
        wii_log_event_t e = wii_log_buf[wii_log_tail];
        wii_log_tail = (uint16_t)((wii_log_tail + 1) % WII_LOG_BUF_SIZE);
        switch (e.kind) {
            case 'A': printf("[wii_ext] seek 0x%02X\n", e.reg); break;
            case 'W': printf("[wii_ext] W 0x%02X = 0x%02X\n", e.reg, e.val); break;
            case 'R': printf("[wii_ext] R 0x%02X = 0x%02X\n", e.reg, e.val); break;
            case 'M': printf("[wii_ext] format mode changed: 0x%02X -> 0x%02X\n", e.reg, e.val); break;
        }
    }
    if (wii_log_dropped) {
        printf("[wii_ext] dropped %u log events\n", (unsigned)wii_log_dropped);
        wii_log_dropped = 0;
    }
}

// ---- I2C slave IRQ handler --------------------------------------------------

static void __isr wii_slave_handler(i2c_inst_t *i2c, i2c_slave_event_t ev)
{
    switch (ev) {
        case I2C_SLAVE_RECEIVE: {
            // Master sent us a byte. If this is the first byte of the
            // transaction, it's the register address to seek to.
            // Subsequent bytes are writes to that address with
            // auto-increment.
            uint8_t b = (uint8_t)i2c_read_byte_raw(i2c);
            if (!addr_received) {
                cursor = b;
                addr_received = true;
                // Suppress the very common 0x00 data-poll seek so init reads
                // (0x20, 0x30, 0xFA, 0xFE, ...) are visible in the log.
                if (b != 0x00) wii_log_push('A', b, 0);
            } else {
                // Decrypt incoming write IF encryption is on — but skip
                // the decrypt for control registers (0xF0, 0xFB, 0xFE),
                // which the Wii always sends in plaintext for protocol
                // negotiation. This lets a new session reset stale
                // encryption state from a previous session: when 0x55
                // arrives at 0xF0, we recognize it raw and disable.
                // Encryption-negotiation control registers: 0xF0 (disable/
                // re-enable) and 0xFB (init step 2) are always sent in
                // plaintext by the Wii. Decrypting them with stale cipher
                // state from a previous session would corrupt the values
                // and break re-init, so leave them raw. 0xFE (format mode
                // select) is sent encrypted once encryption is active and
                // MUST be decrypted, otherwise we'd store a garbage byte
                // and silently stay in the wrong format mode.
                bool is_control_reg = (cursor == 0xF0 || cursor == 0xFB);
                uint8_t store_b = b;
                if (wii_ext_crypto_enabled && !is_control_reg) {
                    wii_ext_crypto_decrypt(&store_b, (uint8_t)cursor, 1);
                }
                if (cursor < REG_FILE_SIZE) {
                    reg_file[cursor] = store_b;
                }
                wii_log_push('W', (uint8_t)cursor, store_b);
                // Encryption negotiation. Both 0x55 and 0xAA writes to 0xF0
                // disable encryption — the Wiimote then sends a fresh
                // 16-byte key in plaintext to 0x40-0x4F, and we re-init
                // the cipher when the last key byte (0x4F) lands.
                if (cursor == 0xF0) {
                    if (store_b == 0x55 || store_b == 0xAA) {
                        wii_ext_crypto_enabled = false;
                    }
                } else if (cursor == 0x4F && reg_file[0xF0] == 0xAA) {
                    wii_ext_crypto_init(&reg_file[0x40]);
                } else if (cursor == 0xFE) {
                    // Format-mode switch — mirror into id[4] (the ID byte
                    // and the 0xFE format register must agree per real-CC
                    // behavior). Also seed sensible neutral data bytes
                    // for the new format so a host read between the mode
                    // switch and the next BT input event sees centred
                    // sticks instead of stale bytes from the old format.
                    uint8_t prev = reg_file[0xFE];
                    if (prev != store_b) {
                        wii_log_push('M', prev, store_b);  // 'M' = mode change
                    }
                    reg_file[0xFE] = store_b;
                    reg_file[0xFA + 4] = store_b;
                    if (store_b == 0x03) {
                        // Format 0x03: 8-bit per axis, center 0x80.
                        reg_file[0x00] = 0x80;  // LX
                        reg_file[0x01] = 0x80;  // RX
                        reg_file[0x02] = 0x80;  // LY
                        reg_file[0x03] = 0x80;  // RY
                        reg_file[0x04] = 0x00;  // LT
                        reg_file[0x05] = 0x00;  // RT
                        reg_file[0x06] = 0xFF;  // buttons lo (unpressed)
                        reg_file[0x07] = 0xFF;  // buttons hi (unpressed)
                    } else if (store_b == 0x01) {
                        // Format 0x01: 6-bit packed centered values.
                        reg_file[0x00] = 0xA0;  // RX<4:3>=10, LX=0x20
                        reg_file[0x01] = 0x20;  // RX<2:1>=00, LY=0x20
                        reg_file[0x02] = 0x10;  // RX<0>=0, LT<4:3>=0, RY=0x10
                        reg_file[0x03] = 0x00;  // LT<2:0>=0, RT=0
                        reg_file[0x04] = 0xFF;  // buttons unpressed
                        reg_file[0x05] = 0xFF;
                    }
                }
                cursor = (uint16_t)(cursor + 1) % REG_FILE_SIZE;
            }
            break;
        }
        case I2C_SLAVE_REQUEST: {
            // Master wants to read. Encrypt if encryption is active.
            uint8_t out_byte;
            if (cursor < REG_FILE_SIZE) {
                out_byte = reg_file[cursor];
                if (wii_ext_crypto_enabled) {
                    wii_ext_crypto_encrypt(&out_byte, (uint8_t)cursor, 1);
                }
            } else {
                out_byte = 0xFF;
            }
            // Log the first byte of each read transaction (skip 0x00 polls).
            if (tx_first_read && cursor != 0x00) {
                wii_log_push('R', (uint8_t)cursor, out_byte);
            }
            tx_first_read = false;
            i2c_write_byte_raw(i2c, out_byte);
            cursor = (uint16_t)(cursor + 1) % REG_FILE_SIZE;
            break;
        }
        case I2C_SLAVE_FINISH:
            // STOP or RESTART: end of this transaction. Reset the
            // "next-byte-is-address" flag so the next RECEIVE starts a
            // fresh seek. Keep `cursor` pointing where the last read
            // left off — real Wii extensions auto-increment across
            // back-to-back reads, and some games depend on it.
            addr_received = false;
            tx_first_read = true;
            break;
    }
}

// ---- Native output config (web config: Output > Wii Extension) -------------

static bool wii_json_get_int(const char* json, const char* key, int* out_val) {
    char search[32];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char* start = strstr(json, search);
    if (!start) return false;
    start += strlen(search);
    while (*start == ' ' || *start == '\t') start++;
    if (*start == '-' || (*start >= '0' && *start <= '9')) {
        *out_val = atoi(start);
        return true;
    }
    return false;
}

static uint16_t wii_get_native_config(char* buf, uint16_t buf_size) {
    flash_t* settings = flash_get_settings();
    int sda = WII_DEVICE_PIN_SDA;
    int scl = WII_DEVICE_PIN_SCL;
    int mode = 0;  // classic
    if (settings) {
        // Stored as pin+1 (0=default, 1=GPIO0, 2=GPIO1, etc.)
        if (settings->wii_sda_pin > 0) sda = settings->wii_sda_pin - 1;
        if (settings->wii_scl_pin > 0) scl = settings->wii_scl_pin - 1;
        if (settings->wii_mode > 0) mode = settings->wii_mode - 1;
    }
    int n = snprintf(buf, buf_size,
        "\"type\":\"wii\","
        "\"modes\":[\"classic\",\"classic_pro\",\"nunchuck\",\"guitar\",\"drums\",\"turntable\",\"taiko\",\"udraw\"],"
        "\"disabled_modes\":[\"nunchuck\",\"guitar\",\"drums\",\"turntable\",\"taiko\",\"udraw\"],"
        "\"current_mode\":\"%s\","
        "\"pins\":{"
            "\"sda\":{\"label\":\"SDA\",\"value\":%d,\"min\":0,\"max\":28,\"default\":%d},"
            "\"scl\":{\"label\":\"SCL\",\"value\":%d,\"min\":0,\"max\":28,\"default\":%d}"
        "}",
        mode == 2 ? "nunchuck" : mode == 1 ? "classic_pro" : "classic",
        sda, WII_DEVICE_PIN_SDA,
        scl, WII_DEVICE_PIN_SCL);
    return (n > 0 && n < buf_size) ? (uint16_t)n : 0;
}

static bool wii_set_native_config(const char* json, char* response_buf, uint16_t response_size) {
    flash_t* settings = flash_get_settings();
    if (!settings) {
        snprintf(response_buf, response_size, "{\"ok\":false,\"err\":\"flash not initialized\"}");
        return false;
    }
    int val;
    // Store as pin+1 (0=default, 1=GPIO0, 2=GPIO1, etc.)
    if (wii_json_get_int(json, "sda", &val) && val >= 0 && val <= 28)
        settings->wii_sda_pin = (uint8_t)(val + 1);
    if (wii_json_get_int(json, "scl", &val) && val >= 0 && val <= 28)
        settings->wii_scl_pin = (uint8_t)(val + 1);

    // Mode: "classic"=0, "classic_pro"=1, "nunchuck"=2 → stored as mode+1 (0=default)
    int name_len;
    const char* mode_start = strstr(json, "\"mode\":\"");
    if (mode_start) {
        mode_start += 8;
        if (strncmp(mode_start, "nunchuck", 8) == 0) settings->wii_mode = 3;
        else if (strncmp(mode_start, "classic_pro", 11) == 0) settings->wii_mode = 2;
        else settings->wii_mode = 1;  // classic
    }

    flash_save_force(settings);
    snprintf(response_buf, response_size, "{\"ok\":true,\"reboot\":true}");
    sleep_ms(150);
    platform_reboot();
    return true;
}

// ---- Output interface -------------------------------------------------------

static void wii_device_out_init(void) {
    // Nothing to do here — wii_device_init() did all the work.
}

static void wii_device_out_task(void) {
    wii_log_drain();

    // Run cipher self-test from task context (non-ISR) once encryption
    // has been activated by a real Wii init. Avoids the CDC-TX-FIFO
    // overflow we get when printing from the I2C ISR.
    static bool selftest_done = false;
    if (!selftest_done && wii_ext_crypto_enabled) {
        selftest_done = true;
        wii_ext_crypto_self_test();
    }
}

const OutputInterface wii_output_interface = {
    .name = "Wii Extension",
    .target = OUTPUT_TARGET_WII_EXTENSION,
    .init   = wii_device_out_init,
    .task   = wii_device_out_task,
    .get_rumble     = NULL,
    .get_player_led = NULL,
    .get_native_config = wii_get_native_config,
    .set_native_config = wii_set_native_config,
};

// ---- Public init ------------------------------------------------------------

void wii_device_init(wii_device_emulation_t emulate)
{
    emulation_kind = emulate;

    memset((void *)reg_file, 0, sizeof reg_file);
    seed_id_bytes(emulate);
    seed_calibration();
    seed_neutral_report();

    printf("[wii_ext] boot format mode = 0x%02X (id[4]=0x%02X)\n",
           reg_file[0xFE], reg_file[0xFA + 4]);

    // Register the router tap so events routed to OUTPUT_TARGET_WII_EXTENSION pack
    // into the report register file. Exclusive mode — router skips its
    // own storage for this output since the I2C slave doesn't read it.
    router_set_tap_exclusive(OUTPUT_TARGET_WII_EXTENSION, wii_device_router_tap);

    // Allow runtime pin override from flash (web config)
    uint8_t sda = WII_DEVICE_PIN_SDA;
    uint8_t scl = WII_DEVICE_PIN_SCL;
    flash_t* settings = flash_get_settings();
    if (settings) {
        // Stored as pin+1 (0=default, 1=GPIO0, 2=GPIO1, etc.)
        if (settings->wii_sda_pin > 0) sda = settings->wii_sda_pin - 1;
        if (settings->wii_scl_pin > 0) scl = settings->wii_scl_pin - 1;
    }

    // Set up the I2C0 peripheral as a slave at 0x52.
    gpio_init(sda);
    gpio_init(scl);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_pull_up(sda);
    gpio_pull_up(scl);

    i2c_init(WII_DEV_I2C_INST, WII_DEVICE_I2C_FREQ_HZ);
    i2c_slave_init(WII_DEV_I2C_INST, WII_I2C_ADDR, &wii_slave_handler);

    printf("[wii_device] slave @ 0x%02X on SDA=%d SCL=%d (emulating %s)%s\n",
           WII_I2C_ADDR, sda, scl,
           emulate == WII_DEV_EMULATE_CLASSIC_PRO ? "Classic Pro" :
           emulate == WII_DEV_EMULATE_NUNCHUCK    ? "Nunchuck" : "Classic",
           (sda != WII_DEVICE_PIN_SDA || scl != WII_DEVICE_PIN_SCL) ? " (override)" : "");
}
