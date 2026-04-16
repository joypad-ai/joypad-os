// wii_device.h - Wii extension I2C-slave device driver
//
// Emulates a Wii Classic Controller (or Classic Pro) by sitting as an I2C
// slave at address 0x52. A real Wiimote (or a Wii-connector breakout) can
// plug into the microcontroller's I2C pins and will see a Classic
// Controller responding to the standard unencrypted init sequence.
//
// Consumed input comes from the router tap — whichever input events the
// app routes to OUTPUT_TARGET_WII get packed into the 6-byte Classic
// report format and placed in the register file at addresses 0x00..0x05
// so the Wiimote's next 6-byte read from register 0x00 delivers the
// current state.
//
// Encrypted init (0xF0 = 0xAA with XOR-obfuscated 16-byte key at 0x40)
// is NOT implemented — the unencrypted path works on every modern
// Wiimote and every Wii system-software release since ~2008.
//
// Hardware: RP2040 hardware I2C slave (pico-sdk pico_i2c_slave library).

#ifndef WII_DEVICE_H
#define WII_DEVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "core/output_interface.h"

// Emulated extension type — affects the 6-byte ID bytes returned at 0xFA.
typedef enum {
    WII_DEV_EMULATE_CLASSIC     = 0,  // 00 00 A4 20 01 01
    WII_DEV_EMULATE_CLASSIC_PRO = 1,  // 01 00 A4 20 01 01
    WII_DEV_EMULATE_NUNCHUCK    = 2,  // 00 00 A4 20 00 00 (future)
} wii_device_emulation_t;

// Default pins (Pico W: GP4/GP5 free of CYW43 / joybus / UART conflicts).
#ifndef WII_DEVICE_PIN_SDA
#define WII_DEVICE_PIN_SDA   4
#endif
#ifndef WII_DEVICE_PIN_SCL
#define WII_DEVICE_PIN_SCL   5
#endif
// Wiimote drives the bus at 400 kHz — we're purely reactive, so speed
// comes from the master. This value is only used if we need to re-clock
// the local I2C block; set to match the expected master rate.
#ifndef WII_DEVICE_I2C_FREQ_HZ
#define WII_DEVICE_I2C_FREQ_HZ  400000
#endif

// Initialize the I2C slave at 0x52 with the given emulation personality,
// pre-populate ID bytes + calibration + a neutral 6-byte report. Safe to
// call once during app_init. Also registers as a router tap on
// OUTPUT_TARGET_WII so events routed there update the report in real
// time from whichever input the app is using.
void wii_device_init(wii_device_emulation_t emulate);

// Output interface for registering with the app framework.
extern const OutputInterface wii_output_interface;

#endif // WII_DEVICE_H
