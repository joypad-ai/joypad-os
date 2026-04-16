// wii_ext.h - Wii extension controller protocol library
//
// Portable (no joypad-os deps). Handles init, identification, calibration,
// polling, and per-accessory report parsing. Host firmware provides an I2C
// transport vtable and calls poll() periodically.

#ifndef WII_EXT_H
#define WII_EXT_H

#include "wii_ext_types.h"

// 7-bit extension slave address on the main bus.
#define WII_EXT_I2C_ADDR  0x52

// Protocol instance. Do not inspect fields directly.
typedef struct {
    const wii_ext_transport_t *io;

    // Detection / identification
    wii_ext_type_t type;
    uint8_t  id[6];              // last raw ID read from 0xFA
    bool     ready;              // true once init + ident succeeded
    bool     first_read;         // true until first valid report consumed

    // Calibration (Nunchuck layout; reused for Classic with separate fields)
    bool     calib_valid;
    uint8_t  calib_raw[16];      // raw bytes from 0x20..0x2F

    // Stick center seeded from first successful poll. Used to correct
    // clone / worn sticks whose factory calibration is wrong.
    uint16_t origin[WII_AXIS_COUNT];
} wii_ext_t;

// Attach a transport. Must be called before any other API.
void wii_ext_attach(wii_ext_t *ext, const wii_ext_transport_t *io);

// Run the unencrypted init + identification sequence and prepare for polling.
// Returns true if an extension was found and identified. On failure the
// extension is marked disconnected; call wii_ext_start() again to retry.
bool wii_ext_start(wii_ext_t *ext);

// Poll once. On success, updates `out` with the latest state. On any I2C
// error, marks the extension disconnected and returns false; the caller
// should call wii_ext_start() on a later tick to attempt re-detection.
bool wii_ext_poll(wii_ext_t *ext, wii_ext_state_t *out);

// Mark extension disconnected (e.g. user code detected a cable removal via
// an external DETECT pin). Next poll() will return false.
void wii_ext_mark_disconnected(wii_ext_t *ext);

// Current cached type. WII_EXT_TYPE_NONE if nothing is connected.
static inline wii_ext_type_t wii_ext_type(const wii_ext_t *ext) {
    return ext ? ext->type : WII_EXT_TYPE_NONE;
}

// Per-accessory parsers. Declared here so wii_ext.c can dispatch without
// including each header.
void wii_ext_parse_nunchuck(wii_ext_t *ext, const uint8_t *report, wii_ext_state_t *out);
void wii_ext_parse_classic (wii_ext_t *ext, const uint8_t *report, wii_ext_state_t *out);

#endif // WII_EXT_H
