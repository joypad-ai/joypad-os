// wii_device.c - Wii extension I2C-slave device driver
//
// A Wiimote (the I2C master) polls its extension port by issuing short
// write-then-read transactions at 400 kHz:
//   1. `start | addr<<1|W | reg_to_read | stop`          — set cursor
//   2. `start | addr<<1|R | data*N | stop`               — read N bytes
// The slave just needs a 256-byte register file with auto-incrementing
// reads and minimal handling of a few special writes (0xF0 init,
// 0xFB init-2, 0xFE mode select). We implement exactly that.

#include "wii_device.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "core/buttons.h"
#include "pico/i2c_slave.h"
#include "hardware/gpio.h"
#include <string.h>
#include <stdio.h>

#define WII_I2C_ADDR           0x52
#define WII_DEV_I2C_INST       i2c0
#define REG_FILE_SIZE          256

// ---- Register file + I2C slave state ---------------------------------------

static volatile uint8_t  reg_file[REG_FILE_SIZE];
static volatile uint16_t cursor = 0;     // current register pointer
static volatile bool     addr_received = false;  // did master send a reg address this transaction?

static wii_device_emulation_t emulation_kind = WII_DEV_EMULATE_CLASSIC;

// ---- Calibration block ------------------------------------------------------

// A valid Classic calibration with the `0x55`-seeded sum checksum that
// real Wiimotes validate. Values chosen to give centered sticks and
// reasonable ranges — these are close to OEM factory defaults.
static void seed_calibration(void)
{
    // 14 bytes of calibration, then 2-byte sum checksum (seed = 0x55).
    // Layout follows wiibrew's Classic calibration block:
    //   LX max, LX min, LX center,
    //   LY max, LY min, LY center,
    //   RX max, RX min, RX center,
    //   RY max, RY min, RY center,
    //   LT neutral,  RT neutral,
    //   checksum MSB, checksum LSB
    uint8_t cal[16];
    cal[0]  = 0x3F;  // LX max
    cal[1]  = 0x00;  // LX min
    cal[2]  = 0x20;  // LX center
    cal[3]  = 0x3F;  // LY max
    cal[4]  = 0x00;  // LY min
    cal[5]  = 0x20;  // LY center
    cal[6]  = 0x1F;  // RX max
    cal[7]  = 0x00;  // RX min
    cal[8]  = 0x10;  // RX center
    cal[9]  = 0x1F;  // RY max
    cal[10] = 0x00;  // RY min
    cal[11] = 0x10;  // RY center
    cal[12] = 0x00;  // LT neutral
    cal[13] = 0x00;  // RT neutral

    uint16_t sum = 0x55 + 0x55;
    for (int i = 0; i < 14; i++) sum += cal[i];
    cal[14] = (uint8_t)((sum >> 8) & 0xFF);
    cal[15] = (uint8_t)(sum & 0xFF);

    memcpy((void *)&reg_file[0x20], cal, 16);
    memcpy((void *)&reg_file[0x30], cal, 16);  // mirror — hardware does it
}

static void seed_id_bytes(wii_device_emulation_t k)
{
    // [2][3] are the always-0xA4 0x20 "Wii extension" sanity prefix.
    // [4] is the data-type/mode byte (1 = 6-byte format).
    // [5] is the family selector (00=Nunchuck, 01=Classic, 03=Guitar...).
    // [0] distinguishes within a family (Classic Pro reports 0x01).
    uint8_t id[6];
    id[2] = 0xA4;
    id[3] = 0x20;
    id[4] = 0x01;
    switch (k) {
        case WII_DEV_EMULATE_CLASSIC:     id[0] = 0x00; id[5] = 0x01; break;
        case WII_DEV_EMULATE_CLASSIC_PRO: id[0] = 0x01; id[5] = 0x01; break;
        case WII_DEV_EMULATE_NUNCHUCK:    id[0] = 0x00; id[4] = 0x00;
                                          id[5] = 0x00; break;
    }
    id[1] = 0x00;
    memcpy((void *)&reg_file[0xFA], id, 6);
}

// Initial 6-byte report — neutral sticks / no buttons pressed / report
// mode 1. Updated live from the router tap callback.
static void seed_neutral_report(void)
{
    // Neutral sticks: LX/LY center = 0x20, RX/RY center = 0x10.
    // Classic report format 1 (6 bytes, active-low buttons):
    //   [0] RX_high2 << 6 | LX[5:0]
    //   [1] RX_mid2  << 6 | LY[5:0]
    //   [2] RX_low1  << 7 | LT[4:3] << 5 | RY[4:0]
    //   [3] LT[2:0]  << 5 | RT[4:0]
    //   [4] 0xFF (all bits = not-pressed)
    //   [5] 0xFF
    reg_file[0x00] = (uint8_t)(0x20);                           // LX=32, RX bits zeroed
    reg_file[0x01] = (uint8_t)(0x20);                           // LY=32
    reg_file[0x02] = (uint8_t)((0x10) | ((0x10 & 0x01) << 7));  // RY=16, RX low bit
    reg_file[0x03] = 0x00;                                      // LT=RT=0
    reg_file[0x04] = 0xFF;
    reg_file[0x05] = 0xFF;
    // Report mode register.
    reg_file[0xFE] = 0x01;
}

// ---- Event → 6-byte report packer (inverse of ext_classic.c parser) --------

static void pack_report_from_event(const input_event_t *ev)
{
    // Stick / trigger down-shifts:
    // Our internal format: 0..255 with 128 center for sticks, 0..255 for triggers.
    // Wii Classic format: LX/LY = 6-bit (0..63, ~32 center),
    //                     RX/RY = 5-bit (0..31, ~16 center),
    //                     LT/RT = 5-bit (0..31, 0 = released).
    uint8_t lx = ev->analog[ANALOG_LX] >> 2;
    // Y-axis inversion: internal HID convention is 0=up, Wii native is 255=up.
    uint8_t ly = (uint8_t)(255 - ev->analog[ANALOG_LY]) >> 2;
    uint8_t rx = ev->analog[ANALOG_RX] >> 3;
    uint8_t ry = (uint8_t)(255 - ev->analog[ANALOG_RY]) >> 3;
    uint8_t lt = ev->analog[ANALOG_L2] >> 3;
    uint8_t rt = ev->analog[ANALOG_R2] >> 3;

    if (lx > 0x3F) lx = 0x3F;
    if (ly > 0x3F) ly = 0x3F;
    if (rx > 0x1F) rx = 0x1F;
    if (ry > 0x1F) ry = 0x1F;
    if (lt > 0x1F) lt = 0x1F;
    if (rt > 0x1F) rt = 0x1F;

    uint8_t b0 = (uint8_t)((rx & 0x18) << 3) | (lx & 0x3F);
    uint8_t b1 = (uint8_t)((rx & 0x06) << 5) | (ly & 0x3F);
    uint8_t b2 = (uint8_t)((rx & 0x01) << 7) | (uint8_t)((lt & 0x18) << 2) | (ry & 0x1F);
    uint8_t b3 = (uint8_t)((lt & 0x07) << 5) | (rt & 0x1F);

    // Buttons: active-LOW in bytes 4 and 5. Start with all bits set (no
    // button pressed) and clear per pressed button.
    uint8_t b4 = 0xFF;
    uint8_t b5 = 0xFF;
    uint32_t btn = ev->buttons;
    if (btn & JP_BUTTON_DR) b4 &= ~(1u << 7);
    if (btn & JP_BUTTON_DD) b4 &= ~(1u << 6);
    if (btn & JP_BUTTON_L1) b4 &= ~(1u << 5);
    if (btn & JP_BUTTON_S1) b4 &= ~(1u << 4);
    if (btn & JP_BUTTON_A1) b4 &= ~(1u << 3);
    if (btn & JP_BUTTON_S2) b4 &= ~(1u << 2);
    if (btn & JP_BUTTON_R1) b4 &= ~(1u << 1);

    if (btn & JP_BUTTON_L2) b5 &= ~(1u << 7);  // ZL
    if (btn & JP_BUTTON_B1) b5 &= ~(1u << 6);  // south = Wii B
    if (btn & JP_BUTTON_B3) b5 &= ~(1u << 5);  // west  = Wii Y
    if (btn & JP_BUTTON_B2) b5 &= ~(1u << 4);  // east  = Wii A
    if (btn & JP_BUTTON_B4) b5 &= ~(1u << 3);  // north = Wii X
    if (btn & JP_BUTTON_R2) b5 &= ~(1u << 2);  // ZR
    if (btn & JP_BUTTON_DL) b5 &= ~(1u << 1);
    if (btn & JP_BUTTON_DU) b5 &= ~(1u << 0);

    // Atomic-ish update: I2C ISR reads these bytes on the next Wiimote
    // poll. On a 32-bit ARM the individual byte writes are atomic, so
    // we at worst tear between two neighbouring fields. Acceptable for
    // 100 Hz poll rate — next poll picks up the coherent value.
    reg_file[0x00] = b0;
    reg_file[0x01] = b1;
    reg_file[0x02] = b2;
    reg_file[0x03] = b3;
    reg_file[0x04] = b4;
    reg_file[0x05] = b5;
}

// ---- Router tap: called for every input event routed to OUTPUT_TARGET_WII --

static void wii_device_router_tap(output_target_t output, uint8_t player_index,
                                  const input_event_t *event)
{
    (void)output;
    (void)player_index;
    if (event) pack_report_from_event(event);
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
            } else {
                if (cursor < REG_FILE_SIZE) {
                    reg_file[cursor] = b;
                }
                // Special writes the Wiimote issues during init:
                //   0xF0 = 0x55 + 0xFB = 0x00  (unencrypted init)
                //   0xFE = 0x01/0x03/0x07      (report mode select)
                // The register file naturally records the written byte;
                // we don't need to do anything beyond that — the ID bytes
                // stay consistent and the report mode byte is read back
                // at 0xFE when the Wiimote asks.
                cursor = (uint16_t)(cursor + 1) % REG_FILE_SIZE;
            }
            break;
        }
        case I2C_SLAVE_REQUEST: {
            // Master wants to read. Push one byte at a time (slave FIFO
            // is a few bytes deep; Wiimote pulls one at a time).
            if (cursor < REG_FILE_SIZE) {
                i2c_write_byte_raw(i2c, reg_file[cursor]);
            } else {
                i2c_write_byte_raw(i2c, 0xFF);
            }
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
            break;
    }
}

// ---- Output interface -------------------------------------------------------

static void wii_device_out_init(void) {
    // Nothing to do here — wii_device_init() did all the work.
}

static void wii_device_out_task(void) {
    // No periodic work; updates flow via the router tap.
}

const OutputInterface wii_output_interface = {
    .name = "Wii",
    .target = OUTPUT_TARGET_WII,
    .init   = wii_device_out_init,
    .task   = wii_device_out_task,
    .get_rumble     = NULL,
    .get_player_led = NULL,
};

// ---- Public init ------------------------------------------------------------

void wii_device_init(wii_device_emulation_t emulate)
{
    emulation_kind = emulate;

    memset((void *)reg_file, 0, sizeof reg_file);
    seed_id_bytes(emulate);
    seed_calibration();
    seed_neutral_report();

    // Register the router tap so events routed to OUTPUT_TARGET_WII pack
    // into the report register file. Exclusive mode — router skips its
    // own storage for this output since the I2C slave doesn't read it.
    router_set_tap_exclusive(OUTPUT_TARGET_WII, wii_device_router_tap);

    // Set up the I2C0 peripheral as a slave at 0x52. pico_i2c_slave
    // configures the pins, enables the IRQ, and attaches our handler.
    gpio_init(WII_DEVICE_PIN_SDA);
    gpio_init(WII_DEVICE_PIN_SCL);
    gpio_set_function(WII_DEVICE_PIN_SDA, GPIO_FUNC_I2C);
    gpio_set_function(WII_DEVICE_PIN_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(WII_DEVICE_PIN_SDA);
    gpio_pull_up(WII_DEVICE_PIN_SCL);

    i2c_init(WII_DEV_I2C_INST, WII_DEVICE_I2C_FREQ_HZ);
    i2c_slave_init(WII_DEV_I2C_INST, WII_I2C_ADDR, &wii_slave_handler);

    printf("[wii_device] slave @ 0x%02X on SDA=%d SCL=%d (emulating %s)\n",
           WII_I2C_ADDR, WII_DEVICE_PIN_SDA, WII_DEVICE_PIN_SCL,
           emulate == WII_DEV_EMULATE_CLASSIC_PRO ? "Classic Pro" :
           emulate == WII_DEV_EMULATE_NUNCHUCK    ? "Nunchuck" : "Classic");
}
