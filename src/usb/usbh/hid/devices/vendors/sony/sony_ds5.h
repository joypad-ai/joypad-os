// sony_ds5.h
#ifndef SONY_DS5_HEADER_H
#define SONY_DS5_HEADER_H

#include "../../../hid_device.h"
#include "../../../hid_utils.h"
#include "tusb.h"

#define GC_TRIGGER_THRESHOLD 75

extern DeviceInterface sony_ds5_interface;

// Sony DualSens PS5 controller
typedef struct TU_ATTR_PACKED
{
  uint8_t x1, y1, x2, y2, rx, ry, rz;

  struct {
    uint8_t dpad     : 4; // (hat format, 0x08 is released, 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW)
    uint8_t square   : 1; // west
    uint8_t cross    : 1; // south
    uint8_t circle   : 1; // east
    uint8_t triangle : 1; // north
  };

  struct {
    uint8_t l1     : 1;
    uint8_t r1     : 1;
    uint8_t l2     : 1;
    uint8_t r2     : 1;
    uint8_t share  : 1;
    uint8_t option : 1;
    uint8_t l3     : 1;
    uint8_t r3     : 1;
  };

  struct {
    uint8_t ps      : 1; // playstation button
    uint8_t tpad    : 1; // track pad click
    uint8_t mute    : 1; // mute button
    uint8_t counter : 5; // +1 each report
  };

  int16_t  gyro[3];  // x, y, z;
  int16_t  accel[3]; // x, y, z
  int8_t   unknown_a[5]; // who knows?
  uint8_t  headset;
  int8_t   unknown_b[2]; // future use?

  struct {
    uint8_t tpad_event : 4; // track pad event 0x01 = 2 finger tap; 0x02 last on edge?
    uint8_t unknown_c  : 4; // future use?
  };

  uint8_t  tpad_counter;

  struct {
    uint8_t tpad_f1_count : 7;
    uint8_t tpad_f1_down  : 1;
  };

  int8_t tpad_f1_pos[3];

  struct {
    uint8_t tpad_f2_count : 7;
    uint8_t tpad_f2_down  : 1;
  };

  int8_t tpad_f2_pos[3];

} sony_ds5_report_t;

// mode: following bytes
// 0x06: 0: frequency (1-255), 1: off time (1-255)
// 0x23: 0: step1 resistance (0-15), 1: step2 resistance (0-15)
typedef struct {
    uint8_t motor_mode; // 0x2: resistance, 0x6: vibrating, 0x23: 2step
    uint8_t start_resistance;
    uint8_t effect_force;
    uint8_t range_force;
    uint8_t near_release_str;
    uint8_t near_middle_str;
    uint8_t pressed_str;
    uint8_t unk1[2];
    uint8_t actuation_freq;
    uint8_t unk2;
} ds5_trigger_t;

typedef struct {
    uint16_t flags; // @ 0-1. bitfield fedcba9876543210. 012: rumble emulation (seems that the lowest nibble has to be 0x7 (????????????0111) in order to trigger this), 2: trigger_r, 3: trigger_l, 8: mic_led, a: lightbar, c: player_led
    uint8_t rumble_r; // @ 2
    uint8_t rumble_l; // @ 3
    uint8_t unk3[4]; // @ 4-7
    uint8_t mic_led; // @ 8. 0: off, 1: on, 2: pulse
    uint8_t unk9; // @ 9
    ds5_trigger_t trigger_r;// 10-20
    ds5_trigger_t trigger_l; // 21-31
    uint8_t unk28[11]; // @ 32-42
    uint8_t player_led; // @ 43. 5-bit. LSB is left.
    union {
        uint8_t lightbar_rgb[3];
        struct {
            uint8_t lightbar_r;
            uint8_t lightbar_g;
            uint8_t lightbar_b;
        };
    }; // @ 44-46
} ds5_feedback_t;

extern int16_t spinner;

#endif
