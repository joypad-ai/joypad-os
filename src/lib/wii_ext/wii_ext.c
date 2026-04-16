// wii_ext.c - Wii extension controller protocol (detection, polling, dispatch)

#include "wii_ext.h"
#include <string.h>

// Inter-transaction delay. Clone accessories fail if transactions are too
// close together; 300 µs is the consensus minimum.
#define WII_EXT_DELAY_US         300
// Extra settle time after the init writes.
#define WII_EXT_INIT_SETTLE_US   2000

// Extension ID layout at register 0xFA (6 bytes):
//   [0][1]  vendor-ish prefix (Guitar family uses [0]=0x00/0x01/0x03)
//   [2][3]  must be 0xA4, 0x20 for a Wii extension on the 0x52 bus
//   [4]     "data type" hint (Classic uses 0/1/2/3 to select report format)
//   [5]     family selector: 0x00=Nunchuck, 0x01=Classic, ...

static int io_write(const wii_ext_t *ext, const uint8_t *buf, uint16_t len) {
    return ext->io->write(ext->io->ctx, WII_EXT_I2C_ADDR, buf, len);
}

static int io_read(const wii_ext_t *ext, uint8_t *buf, uint16_t len) {
    return ext->io->read(ext->io->ctx, WII_EXT_I2C_ADDR, buf, len);
}

static void io_delay(const wii_ext_t *ext, uint32_t us) {
    ext->io->delay_us(us);
}

// Write one register byte = value (two-byte I2C transaction).
static int write_reg(const wii_ext_t *ext, uint8_t reg, uint8_t val) {
    uint8_t b[2] = { reg, val };
    return io_write(ext, b, 2);
}

// "Write-then-read" without repeated start — matches how real Wii
// extensions expect transactions (some clones refuse repeated-start).
static int seek_read(const wii_ext_t *ext, uint8_t reg,
                     uint8_t *buf, uint16_t len) {
    if (io_write(ext, &reg, 1) != 0) return -1;
    io_delay(ext, WII_EXT_DELAY_US);
    if (io_read(ext, buf, len) != 0) return -1;
    return 0;
}

// Skip the legacy encrypted (0x40) init entirely. The unencrypted path works
// on every accessory manufactured since roughly 2008 and avoids the XOR
// obfuscation table.
//
// Retry-with-delay loop — mirrors the WiiChuck library's observation that
// Nunchuck clones and certain passive breakouts NACK the very first write
// after power-up and need several tries before the chip is responsive.
#define WII_EXT_INIT_ATTEMPTS    10
#define WII_EXT_INIT_RETRY_US    20000   // 20 ms between retries

static int try_write_reg_once(const wii_ext_t *ext, uint8_t reg, uint8_t val) {
    uint8_t b[2] = { reg, val };
    return io_write(ext, b, 2);
}

static int unencrypted_init(const wii_ext_t *ext) {
    // First write: 0xF0 = 0x55. Clones often NACK the first attempt.
    int rc = -1;
    for (int i = 0; i < WII_EXT_INIT_ATTEMPTS; i++) {
        rc = try_write_reg_once(ext, 0xF0, 0x55);
        if (rc == 0) break;
        io_delay(ext, WII_EXT_INIT_RETRY_US);
    }
    if (rc != 0) return -1;
    io_delay(ext, WII_EXT_DELAY_US);

    // Second write: 0xFB = 0x00.
    for (int i = 0; i < WII_EXT_INIT_ATTEMPTS; i++) {
        rc = try_write_reg_once(ext, 0xFB, 0x00);
        if (rc == 0) break;
        io_delay(ext, WII_EXT_INIT_RETRY_US);
    }
    if (rc != 0) return -1;

    io_delay(ext, WII_EXT_INIT_SETTLE_US);
    return 0;
}

static wii_ext_type_t classify(const uint8_t id[6]) {
    // Sanity prefix expected at [2][3].
    if (id[2] != 0xA4 || id[3] != 0x20) return WII_EXT_TYPE_NONE;

    switch (id[5]) {
        case 0x00:
            return WII_EXT_TYPE_NUNCHUCK;
        case 0x01:
            // Some Classic Pro units report id[0]=0x01; OEM Classic reports
            // 0x00. This is unreliable in the wild (many third-party units
            // lie), but we try it as a hint.
            return (id[0] == 0x01) ? WII_EXT_TYPE_CLASSIC_PRO
                                   : WII_EXT_TYPE_CLASSIC;
        default:
            // Future: 0x03=guitar family, 0x05=MotionPlus, 0x11=taiko, 0x12=udraw
            return WII_EXT_TYPE_NONE;
    }
}

// gp2040-ce-style: some real Nunchucks report id[4]==0 but expect the
// 6-byte report format (dataType == 1). Never lets dataType bleed through
// as 0.
static uint8_t normalize_data_type(uint8_t raw) {
    return raw == 0 ? 1 : raw;
}

static int read_id(wii_ext_t *ext) {
    if (seek_read(ext, 0xFA, ext->id, sizeof ext->id) != 0) return -1;
    (void)normalize_data_type(ext->id[4]);  // reserved for Classic report
    return 0;
}

// 14-byte payload + 2-byte checksum at 0x20. Checksum is the sum of the
// first 14 bytes plus 0x55, split across bytes 14 and 15.
// The calibration is optional: if the checksum fails, we fall back to
// first-read-as-origin.
static void read_calibration(wii_ext_t *ext) {
    ext->calib_valid = false;
    memset(ext->calib_raw, 0, sizeof ext->calib_raw);
    if (seek_read(ext, 0x20, ext->calib_raw, sizeof ext->calib_raw) != 0) {
        return;
    }
    uint16_t sum = 0x55 + 0x55;
    for (int i = 0; i < 14; i++) sum += ext->calib_raw[i];
    uint16_t stored = ((uint16_t)ext->calib_raw[14] << 8) | ext->calib_raw[15];
    ext->calib_valid = (sum == stored);
}

void wii_ext_attach(wii_ext_t *ext, const wii_ext_transport_t *io) {
    memset(ext, 0, sizeof *ext);
    ext->io = io;
    ext->type = WII_EXT_TYPE_NONE;
}

void wii_ext_mark_disconnected(wii_ext_t *ext) {
    ext->type = WII_EXT_TYPE_NONE;
    ext->ready = false;
    ext->first_read = false;
    ext->calib_valid = false;
    memset(ext->origin, 0, sizeof ext->origin);
}

bool wii_ext_start(wii_ext_t *ext) {
    wii_ext_mark_disconnected(ext);

    // Phase-tagged returns so the host adapter can tell the user which step
    // failed (address-NACK at F0=55, ID read, classification, etc.).
    extern int printf(const char *, ...);
    if (unencrypted_init(ext) != 0) {
        printf("[wii_ext] init NACK (F0=55 / FB=00)\n");
        return false;
    }
    if (read_id(ext) != 0) {
        printf("[wii_ext] ID read failed\n");
        return false;
    }
    printf("[wii_ext] ID bytes = %02X %02X %02X %02X %02X %02X\n",
           ext->id[0], ext->id[1], ext->id[2],
           ext->id[3], ext->id[4], ext->id[5]);

    ext->type = classify(ext->id);
    if (ext->type == WII_EXT_TYPE_NONE) {
        printf("[wii_ext] unknown extension type\n");
        return false;
    }

    read_calibration(ext);
    ext->ready = true;
    ext->first_read = true;
    return true;
}

bool wii_ext_poll(wii_ext_t *ext, wii_ext_state_t *out) {
    memset(out, 0, sizeof *out);
    if (!ext->ready) return false;

    uint8_t report[9];
    // Classic Controller's mode-3 report is 8 bytes; Nunchuck is 6.
    // Reading 8 bytes covers both (Classic mode-1 packs into 6 but the
    // chip happily returns zero padding for the extra bytes).
    uint16_t len = (ext->type == WII_EXT_TYPE_NUNCHUCK) ? 6 : 8;
    if (seek_read(ext, 0x00, report, len) != 0) {
        // Any I2C error during poll -> treat as unplugged. Caller re-runs
        // wii_ext_start() on the next tick.
        wii_ext_mark_disconnected(ext);
        out->connected = false;
        out->type = WII_EXT_TYPE_NONE;
        return false;
    }

    // Prime the chip's auto-increment register pointer for the next read.
    // Most Wii extensions auto-advance on successive reads but some clones
    // need a push to byte 0 between polls. write_reg(0x00) is the standard
    // trick — but we already write 0x00 at the top of seek_read(), so
    // nothing extra required here.

    switch (ext->type) {
        case WII_EXT_TYPE_NUNCHUCK:
            wii_ext_parse_nunchuck(ext, report, out);
            break;
        case WII_EXT_TYPE_CLASSIC:
        case WII_EXT_TYPE_CLASSIC_PRO:
            wii_ext_parse_classic(ext, report, out);
            break;
        default:
            out->connected = false;
            return false;
    }

    // First-read-as-origin: seed stick center on the very first successful
    // poll, so clone/worn sticks whose factory calibration is wrong still
    // rest at logical center. The parser fills `out->analog[]` in raw 10-bit
    // space before this runs; we shift the origin if it's far from 512.
    if (ext->first_read) {
        for (int i = 0; i < 4; i++) {        // sticks only; leave triggers
            ext->origin[i] = out->analog[i];
        }
        ext->first_read = false;
    }

    // Re-center sticks around measured origin (clamped to valid range).
    for (int i = 0; i < 4; i++) {
        int32_t val = (int32_t)out->analog[i];
        int32_t o   = (int32_t)ext->origin[i];
        int32_t centered = 512 + (val - o);
        if (centered < 0) centered = 0;
        if (centered > 1023) centered = 1023;
        out->analog[i] = (uint16_t)centered;
    }

    out->type = ext->type;
    out->connected = true;
    return true;
}
