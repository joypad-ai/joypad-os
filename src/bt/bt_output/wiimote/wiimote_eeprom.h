// wiimote_eeprom.h - Minimal Wiimote EEPROM calibration blob
// SPDX-License-Identifier: Apache-2.0
//
// The Wii reads a 2 KB "EEPROM" over 0x17 read-memory requests. Only a few
// regions are actually consulted by games and the system menu:
//
//   0x0016..0x001e : Accelerometer calibration (8 bytes + checksum)
//   0x0020..0x002e : Duplicate accelerometer calibration block
//   0x0fca..0x1290 : Mii slots (24 Miis × 76 bytes each) — we return zeros
//
// We supply sane default accel calibration — zero-g at 512 center, 1g at
// 612 — so IR-less games that still read accel get reasonable values.
// Everything else is 0x00.

#ifndef WIIMOTE_EEPROM_H
#define WIIMOTE_EEPROM_H

#include <stdint.h>

// Produce byte `addr` of the virtual EEPROM. Game-only regions are covered
// by this function; unknown addresses return 0x00.
static inline uint8_t wiimote_eeprom_read_byte(uint32_t addr) {
    // Accelerometer calibration — two identical blocks at 0x0016 and 0x0020.
    // Format: zero-X hi, zero-Y hi, zero-Z hi, (XY)low, one-X hi, one-Y hi,
    //         one-Z hi, (XY)low, checksum.
    // 10-bit values (low bits packed). Zero-g = 512 = 0x200, one-g = 612
    // = 0x264 (standard WiiBrew-documented defaults).
    static const uint8_t accel_calib[9] = {
        0x80, 0x80, 0x80,       // zero X/Y/Z hi (512 >> 2 = 128 = 0x80)
        0x00,                   // XY low nibbles (bit packing)
        0x99, 0x99, 0x99,       // one X/Y/Z hi (612 >> 2 ~= 153 = 0x99)
        0x00,                   // XY low nibbles
        0xB2                    // checksum: sum(bytes) + 0x55 & 0xff
    };
    if (addr >= 0x0016 && addr < 0x0016 + 9) return accel_calib[addr - 0x0016];
    if (addr >= 0x0020 && addr < 0x0020 + 9) return accel_calib[addr - 0x0020];

    // Everything else — blank.
    return 0x00;
}

// Fill `len` bytes starting at `addr` into `buf`.
static inline void wiimote_eeprom_read_block(uint32_t addr, uint8_t* buf, uint16_t len) {
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = wiimote_eeprom_read_byte(addr + i);
    }
}

#endif // WIIMOTE_EEPROM_H
