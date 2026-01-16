// raphnet_pce.c - Raphnet PCEngine/TurboGrafx-16 to USB adapter
//
// VID: 0x289b (Raphnet Technologies)
// PID: 0x0050 (PC Engine adapter)
//
// Report format (9 bytes):
//   Byte 0: Report ID (0x01)
//   Bytes 1-2: X axis (16-bit LE, 0-32000, d-pad left/right)
//   Bytes 3-4: Y axis (16-bit LE, 0-32000, d-pad up/down)
//   Bytes 5-6: Z axis (16-bit LE, unused on 2-button, possibly turbo on 6-button)
//   Bytes 7-8: Buttons (16 buttons, only first 8 used for PCE)
//
// PCE 2-button controller: I, II, Select, Run
// PCE 6-button controller: I, II, III, IV, V, VI, Select, Run

#include "raphnet_pce.h"
#include "core/buttons.h"
#include "core/router/router.h"
#include "core/input_event.h"
#include "tusb.h"
#include <string.h>

// Raphnet VID/PID
#define RAPHNET_VID     0x289b
#define RAPHNET_PCE_PID 0x0050

// Axis thresholds (0-32000 range, center at 16000)
#define AXIS_CENTER     16000
#define AXIS_THRESHOLD  8000  // 50% from center

// Report structure (packed, little-endian)
typedef struct __attribute__((packed)) {
    uint8_t  report_id;     // Always 0x01
    uint16_t x;             // D-pad X: 0=Left, 16000=Center, 32000=Right
    uint16_t y;             // D-pad Y: 0=Up, 16000=Center, 32000=Down
    uint16_t z;             // Unused or turbo
    uint16_t buttons;       // 16 button bits
} raphnet_pce_report_t;

// Previous report for change detection
static raphnet_pce_report_t prev_report[5];

// Check if device is Raphnet PCE adapter
static bool is_raphnet_pce(uint16_t vid, uint16_t pid) {
    return (vid == RAPHNET_VID && pid == RAPHNET_PCE_PID);
}

// Process HID report
static void process_raphnet_pce(uint8_t dev_addr, uint8_t instance,
                                 uint8_t const* report, uint16_t len)
{
    if (len < sizeof(raphnet_pce_report_t)) return;

    raphnet_pce_report_t current;
    memcpy(&current, report, sizeof(current));

    // Only process if report changed
    if (memcmp(&prev_report[dev_addr-1], &current, sizeof(current)) == 0) {
        return;
    }
    prev_report[dev_addr-1] = current;

    // Debug: print raw values
    TU_LOG1("[raphnet_pce] X:%u Y:%u Z:%u Btn:0x%04X\n",
            current.x, current.y, current.z, current.buttons);

    // Convert axes to digital d-pad (inverted: 0=pressed direction)
    bool dpad_left  = (current.x < (AXIS_CENTER - AXIS_THRESHOLD));
    bool dpad_right = (current.x > (AXIS_CENTER + AXIS_THRESHOLD));
    bool dpad_up    = (current.y < (AXIS_CENTER - AXIS_THRESHOLD));
    bool dpad_down  = (current.y > (AXIS_CENTER + AXIS_THRESHOLD));

    // Extract buttons from 2-button PCE controller:
    //   Bit 0 = I
    //   Bit 1 = II
    //   Bit 2 = Select
    //   Bit 3 = Run
    // 6-button controller mapping TBD (bits 4-7 likely III-VI)
    bool btn_I      = (current.buttons & (1 << 0));
    bool btn_II     = (current.buttons & (1 << 1));
    bool btn_Select = (current.buttons & (1 << 2));
    bool btn_Run    = (current.buttons & (1 << 3));
    // 6-button (untested - may need adjustment)
    bool btn_III    = (current.buttons & (1 << 4));
    bool btn_IV     = (current.buttons & (1 << 5));
    bool btn_V      = (current.buttons & (1 << 6));
    bool btn_VI     = (current.buttons & (1 << 7));

    // Map to JP buttons (matches PCEngine protocol bit order):
    // PCE I    → B2 - rightmost button
    // PCE II   → B1 - leftmost button
    // PCE III  → B3 - 6-button (bit 4)
    // PCE IV   → B4 - 6-button (bit 5)
    // PCE V    → L1 - 6-button (bit 6)
    // PCE VI   → R1 - 6-button (bit 7)
    // Select   → S1
    // Run      → S2
    uint32_t buttons = 0;
    buttons |= dpad_up    ? JP_BUTTON_DU : 0;
    buttons |= dpad_down  ? JP_BUTTON_DD : 0;
    buttons |= dpad_left  ? JP_BUTTON_DL : 0;
    buttons |= dpad_right ? JP_BUTTON_DR : 0;
    buttons |= btn_I      ? JP_BUTTON_B2 : 0;
    buttons |= btn_II     ? JP_BUTTON_B1 : 0;
    buttons |= btn_III    ? JP_BUTTON_B3 : 0;
    buttons |= btn_IV     ? JP_BUTTON_B4 : 0;
    buttons |= btn_V      ? JP_BUTTON_L1 : 0;
    buttons |= btn_VI     ? JP_BUTTON_R1 : 0;
    buttons |= btn_Select ? JP_BUTTON_S1 : 0;
    buttons |= btn_Run    ? JP_BUTTON_S2 : 0;

    TU_LOG1("[raphnet_pce] D:%c%c%c%c I:%d II:%d Sel:%d Run:%d (raw:0x%04X)\n",
            dpad_up ? 'U' : '-', dpad_down ? 'D' : '-',
            dpad_left ? 'L' : '-', dpad_right ? 'R' : '-',
            btn_I, btn_II, btn_Select, btn_Run, current.buttons);

    input_event_t event = {
        .dev_addr = dev_addr,
        .instance = instance,
        .type = INPUT_TYPE_GAMEPAD,
        .transport = INPUT_TRANSPORT_USB,
        .buttons = buttons,
        .button_count = 8,  // PCE has up to 8 buttons (6 face + Select + Run)
        .analog = {128, 128, 128, 128, 0, 0},  // No analog
        .keys = 0,
    };
    router_submit_input(&event);
}

// Unmount callback
static void unmount_raphnet_pce(uint8_t dev_addr, uint8_t instance)
{
    TU_LOG1("[raphnet_pce] Unmounted addr=%d instance=%d\n", dev_addr, instance);
    if (dev_addr > 0 && dev_addr <= 5) {
        memset(&prev_report[dev_addr-1], 0, sizeof(raphnet_pce_report_t));
    }
}

DeviceInterface raphnet_pce_interface = {
    .name = "Raphnet PCE Adapter",
    .is_device = is_raphnet_pce,
    .process = process_raphnet_pce,
    .unmount = unmount_raphnet_pce,
    .init = NULL,
    .task = NULL,
};
