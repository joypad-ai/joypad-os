// sony_ds4.h
#ifndef SONY_DS4_HEADER_H
#define SONY_DS4_HEADER_H

#include "../../../hid_device.h"
#include "../../../hid_utils.h"
#include "tusb.h"

extern DeviceInterface sony_ds4_interface;

// Sony DS4 report layout detail https://www.psdevwiki.com/ps4/DS4-USB
typedef struct TU_ATTR_PACKED {
  uint8_t x, y, z, rz; // joystick

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
    uint8_t counter : 6; // +1 each report
  };

  uint8_t l2_trigger; // 0 released, 0xff fully pressed
  uint8_t r2_trigger; // as above

  uint16_t timestamp;
  uint8_t  battery;
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

} sony_ds4_report_t;

typedef struct TU_ATTR_PACKED {
  // First 16 bits set what data is pertinent in this structure (1 = set; 0 = not set)
  uint8_t set_rumble : 1;
  uint8_t set_led : 1;
  uint8_t set_led_blink : 1;
  uint8_t set_ext_write : 1;
  uint8_t set_left_volume : 1;
  uint8_t set_right_volume : 1;
  uint8_t set_mic_volume : 1;
  uint8_t set_speaker_volume : 1;
  uint8_t set_flags2;

  uint8_t reserved;

  uint8_t motor_right;
  uint8_t motor_left;

  uint8_t lightbar_red;
  uint8_t lightbar_green;
  uint8_t lightbar_blue;
  uint8_t lightbar_blink_on;
  uint8_t lightbar_blink_off;

  uint8_t ext_data[8];

  uint8_t volume_left;
  uint8_t volume_right;
  uint8_t volume_mic;
  uint8_t volume_speaker;

  uint8_t other[9];
} sony_ds4_output_report_t;

extern int16_t spinner;

// ============================================================================
// PS4 AUTH PASSTHROUGH
// ============================================================================

// Auth report IDs (matching PS4 console expectations)
#define DS4_AUTH_REPORT_NONCE     0xF0  // Console sends nonce to controller
#define DS4_AUTH_REPORT_SIGNATURE 0xF1  // Controller sends signature
#define DS4_AUTH_REPORT_STATUS    0xF2  // Signing status
#define DS4_AUTH_REPORT_RESET     0xF3  // Reset auth state

// Auth passthrough state
typedef enum {
    DS4_AUTH_STATE_IDLE = 0,        // No auth in progress
    DS4_AUTH_STATE_NONCE_PENDING,   // Nonce received, forwarding to DS4
    DS4_AUTH_STATE_SIGNING,         // DS4 is signing
    DS4_AUTH_STATE_READY,           // Signature ready
    DS4_AUTH_STATE_ERROR            // Auth failed
} ds4_auth_state_t;

// Check if a DS4 is available for auth passthrough
bool ds4_auth_is_available(void);

// Get the current auth state
ds4_auth_state_t ds4_auth_get_state(void);

// Forward nonce from PS4 console to connected DS4
// Called when usbd receives 0xF0 feature report
bool ds4_auth_send_nonce(const uint8_t* data, uint16_t len);

// Get cached signature response for a specific page (0xF1)
// page: 0-18 (19 pages total)
// Returns bytes copied
uint16_t ds4_auth_get_signature(uint8_t* buffer, uint16_t max_len, uint8_t page);

// Get next signature page (auto-incrementing, 0-18)
// Console calls GET_REPORT(0xF1) sequentially, this returns pages in order
uint16_t ds4_auth_get_next_signature(uint8_t* buffer, uint16_t max_len);

// Get auth status (0xF2)
// Returns status report data
uint16_t ds4_auth_get_status(uint8_t* buffer, uint16_t max_len);

// Reset auth state (0xF3)
void ds4_auth_reset(void);

// Auth task - called from main loop to handle async operations
void ds4_auth_task(void);

// Register/unregister DS4 for auth passthrough (called on mount/unmount)
void ds4_auth_register(uint8_t dev_addr, uint8_t instance);
void ds4_auth_unregister(uint8_t dev_addr, uint8_t instance);

#endif
