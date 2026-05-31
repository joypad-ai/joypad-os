// joypad/devices/sony/ds3.h
// Sony DualShock 3 / SIXAXIS — pure USB HID input parser.
//
// DS3 is the outlier of the Sony USB family: pressure-sensitive D-pad, face,
// shoulder, and trigger buttons; single-axis (Z) gyro at ±100 dps; big-endian
// motion values centered at 512. The lightbar / touchpad / adaptive triggers
// of newer DualShock generations don't exist here.
//
// The output report (rumble + 4 player LEDs with blink patterns) is *not*
// yet implemented in libjoypad — DS3 output is heavily firmware-specific
// (blink durations, per-player LED tables) and most non-firmware consumers
// only need input. Will be added if game-engine consumers ask for it.

#ifndef JOYPAD_DEVICES_SONY_DS3_H
#define JOYPAD_DEVICES_SONY_DS3_H

#include <stdint.h>
#include <stdbool.h>
#include <joypad/input_event.h>
#include <joypad/capabilities.h>

#ifdef __cplusplus
extern "C" {
#endif

bool joypad_is_sony_ds3(uint16_t vid, uint16_t pid);
void joypad_sony_ds3_caps(joypad_caps_t* out);

#define JOYPAD_SONY_DS3_INPUT_REPORT_ID  0x01

// Parse a DS3 USB input report (including report ID byte). Returns true if
// the report was report ID 0x01 with sufficient length and *out was populated.
//
// On success, fills:
//   - type / transport / layout (MODERN_4FACE) / button_count = 10
//   - buttons (Sony face-button mapping: cross→B1, circle→B2, square→B3,
//     triangle→B4; PS → A1)
//   - analog[LX,LY,RX,RY] raw HID values; analog[L2,R2] = pressure[8/9]
//     (DS3 has digital L2/R2 + pressure sensors but no separate analog
//     trigger byte — we surface the pressure as the analog value)
//   - has_pressure = true, pressure[12] in order
//     {up, right, down, left, L2, R2, L1, R1, triangle, circle, cross, square}
//   - has_motion = true, gyro_range = 100 dps, accel_range = 2000 milli-g,
//     gyro[0..1] = 0 (DS3 has only Z-axis gyro), gyro[2] and accel[0..2]
//     normalized to ±32767 = full-scale
//   - battery_level + battery_charging (DS3 uses 0-5 lookup table + 0xEE/0xEF
//     for charging/full)
bool joypad_parse_sony_ds3(const uint8_t* report, uint16_t len, input_event_t* out);

#ifdef __cplusplus
}
#endif

#endif // JOYPAD_DEVICES_SONY_DS3_H
