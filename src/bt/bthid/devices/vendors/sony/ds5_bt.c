// ds5_bt.c - Sony DualSense Bluetooth Driver
// Handles PS5 DualSense controllers over Bluetooth
//
// Reference: https://controllers.fandom.com/wiki/Sony_DualSense
// BT reports have similar structure to USB but with different report IDs
// BT output reports require CRC32

#include "ds5_bt.h"
#include "bt/bthid/bthid.h"
#include "bt/transport/bt_transport.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "pico/time.h"
#include <string.h>
#include <stdio.h>

// Player LED colors (RGB values) - same as DS4
static const uint8_t PLAYER_COLORS[][3] = {
    {  0,   0,  64 },   // Player 1: Blue
    { 64,   0,   0 },   // Player 2: Red
    {  0,  64,   0 },   // Player 3: Green
    { 64,   0,  64 },   // Player 4: Pink/Fuchsia
    { 64,  64,   0 },   // Player 5: Yellow
    {  0,  64,  64 },   // Player 6: Cyan
    { 64,  32,   0 },   // Player 7: Orange
};

// Player LED patterns for DS5 (5 LEDs in a row)
// Pattern is a bitmask: bit 0=leftmost, bit 4=rightmost
static const uint8_t PLAYER_LED_PATTERNS[] = {
    0x04,   // Player 1: center LED (--*--)
    0x0A,   // Player 2: left+right of center (-*-*-)
    0x15,   // Player 3: outer + center (*-*-*)
    0x1B,   // Player 4: all but center (**-**)
    0x1F,   // Player 5: all LEDs (*****)
};

// ============================================================================
// DS5 CONSTANTS
// ============================================================================

// Report IDs
#define DS5_REPORT_BT_INPUT     0x31    // Full BT input report
#define DS5_REPORT_USB_INPUT    0x01    // USB input report (fallback)
#define DS5_REPORT_BT_OUTPUT    0x31    // BT output report

// ============================================================================
// DS5 REPORT STRUCTURE
// ============================================================================

typedef struct __attribute__((packed)) {
    uint8_t x1, y1;         // Left stick
    uint8_t x2, y2;         // Right stick
    uint8_t l2_trigger;     // L2 analog
    uint8_t r2_trigger;     // R2 analog
    uint8_t counter;        // Report counter / sequence number

    struct {
        uint8_t dpad     : 4;   // Hat: 0=N, 1=NE, 2=E, 3=SE, 4=S, 5=SW, 6=W, 7=NW, 8=released
        uint8_t square   : 1;
        uint8_t cross    : 1;
        uint8_t circle   : 1;
        uint8_t triangle : 1;
    };

    struct {
        uint8_t l1     : 1;
        uint8_t r1     : 1;
        uint8_t l2     : 1;
        uint8_t r2     : 1;
        uint8_t create : 1;     // Share/Create button
        uint8_t option : 1;
        uint8_t l3     : 1;
        uint8_t r3     : 1;
    };

    struct {
        uint8_t ps      : 1;    // PlayStation button
        uint8_t tpad    : 1;    // Touchpad click
        uint8_t mute    : 1;    // Mute button
        uint8_t pad     : 5;
    };

    uint8_t reserved1;          // 4th button byte

    // Extended data for motion (matches Linux kernel hid-playstation.c)
    uint8_t reserved2[4];       // Timestamp/padding bytes
    int16_t gyro[3];            // x, y, z (pitch, yaw, roll)
    int16_t accel[3];           // x, y, z
    // Touchpad etc follows but not parsed
} ds5_input_report_t;

// DS5 BT output report for LED/rumble
typedef struct __attribute__((packed)) {
    uint8_t report_id;          // 0x31
    uint8_t seq_tag;            // Sequence tag (upper nibble)
    uint8_t tag;                // 0x10 for BT

    uint8_t valid_flag0;        // Feature flags
    uint8_t valid_flag1;
    uint8_t valid_flag2;

    uint8_t rumble_right;       // High frequency motor
    uint8_t rumble_left;        // Low frequency motor

    uint8_t headphone_volume;
    uint8_t speaker_volume;
    uint8_t mic_volume;

    uint8_t audio_flags;
    uint8_t mute_flags;

    uint8_t trigger_r[11];      // Right trigger haptics
    uint8_t trigger_l[11];      // Left trigger haptics

    uint8_t reserved1[6];

    uint8_t valid_flag3;

    uint8_t reserved2[2];

    uint8_t lightbar_setup;     // LED setup flag
    uint8_t led_brightness;
    uint8_t player_led;         // Player indicator LEDs

    uint8_t lightbar_r;
    uint8_t lightbar_g;
    uint8_t lightbar_b;
} ds5_bt_output_report_t;

// ============================================================================
// CRC32 for DS5 BT output reports
// ============================================================================

// Core CRC32 calculation (returns raw CRC, no inversion)
static uint32_t ds5_crc32_raw(uint32_t seed, const uint8_t* data, size_t len)
{
    uint32_t crc = seed;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return crc;
}

// DS5 BT output CRC - matches Linux kernel hid-playstation driver
// Two-step calculation: first hash seed (0xA2), then hash report data
static uint32_t ds5_bt_crc32(const uint8_t* report_data, size_t len)
{
    const uint8_t seed = 0xA2;  // PS_OUTPUT_CRC32_SEED

    // Step 1: Hash the seed byte
    uint32_t crc = ds5_crc32_raw(0xFFFFFFFF, &seed, 1);

    // Step 2: Continue hashing with report data (use intermediate CRC as seed)
    crc = ds5_crc32_raw(crc, report_data, len);

    // Final inversion
    return ~crc;
}

// ============================================================================
// DRIVER DATA
// ============================================================================

typedef struct {
    input_event_t event;
    bool initialized;
    uint8_t activation_state;
    uint32_t activation_time;
    uint8_t output_seq;

    // Current feedback state (for change detection)
    uint8_t rumble_left;
    uint8_t rumble_right;
    uint8_t led_r, led_g, led_b;
    uint8_t player_led;
} ds5_bt_data_t;

static ds5_bt_data_t ds5_data[BTHID_MAX_DEVICES];

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static void ds5_send_output(bthid_device_t* device, uint8_t rumble_left, uint8_t rumble_right,
                            uint8_t r, uint8_t g, uint8_t b, uint8_t player_led)
{
    ds5_bt_data_t* ds5 = (ds5_bt_data_t*)device->driver_data;
    if (!ds5) return;

    // DS5 BT output report - matches Linux kernel hid-playstation.c dualsense_output_report_common
    // Report structure: report_id(1) + seq_tag(1) + tag(1) + common(47) + reserved(24) + crc(4) = 78 bytes
    // Buffer: 0xA2 header + 78-byte report = 79 bytes total
    // Static: BTstack stores pointer to report data for deferred L2CAP send
    static uint8_t buf[79];
    memset(buf, 0, sizeof(buf));

    // BT HID header
    buf[0] = 0xA2;  // DATA | OUTPUT (BT HID transaction header)

    // Report header (report bytes 0-2, buf offset 1-3)
    buf[1] = 0x31;  // Report ID
    buf[2] = (ds5->output_seq++ << 4);  // Sequence tag (upper nibble)
    buf[3] = 0x10;  // Tag: 0x10 for BT

    // Common struct starts at report byte 3 (buf offset 4)
    // Linux kernel offsets within common:
    // 0: valid_flag0, 1: valid_flag1, 2: motor_right, 3: motor_left
    // 4-7: audio volumes, 8: mute_led, 9: power_save, 10-36: reserved2
    // 37: audio_control2, 38: valid_flag2, 39-40: reserved3
    // 41: lightbar_setup, 42: led_brightness, 43: player_leds
    // 44: red, 45: green, 46: blue

    // Valid flags
    buf[4] = 0x03;   // common[0] valid_flag0: COMPATIBLE_VIBRATION | HAPTICS_SELECT
    buf[5] = 0x14;   // common[1] valid_flag1: LIGHTBAR_CONTROL(0x04) | PLAYER_INDICATOR_CONTROL(0x10)

    // Rumble motors (common offsets 2-3)
    buf[6] = rumble_right;   // common[2] motor_right (high frequency)
    buf[7] = rumble_left;    // common[3] motor_left (low frequency)

    // common[4-9]: audio volumes, mute_led, power_save - leave as 0
    // common[10-36]: reserved2 - leave as 0
    // common[37]: audio_control2 - leave as 0

    // common[38] = buf[42]: valid_flag2
    buf[42] = 0x02;  // LIGHTBAR_SETUP_CONTROL

    // common[39-40] = buf[43-44]: reserved3 - leave as 0

    // common[41] = buf[45]: lightbar_setup
    buf[45] = 0x02;  // LIGHTBAR_SETUP_LIGHT_OUT

    // common[42] = buf[46]: led_brightness
    buf[46] = 0x01;  // Full brightness

    // common[43] = buf[47]: player_leds
    buf[47] = player_led;

    // common[44-46] = buf[48-50]: lightbar RGB
    buf[48] = r;
    buf[49] = g;
    buf[50] = b;

    // buf[51-74]: reserved[24] - leave as 0

    // CRC32 calculated over report data only (buf[1..74] = 74 bytes)
    // The 0xA2 seed is handled internally by ds5_bt_crc32
    uint32_t crc = ds5_bt_crc32(&buf[1], 74);

    // Append CRC (little-endian) at bytes 75-78
    buf[75] = (crc >> 0) & 0xFF;
    buf[76] = (crc >> 8) & 0xFF;
    buf[77] = (crc >> 16) & 0xFF;
    buf[78] = (crc >> 24) & 0xFF;

    // Send on interrupt channel (79 bytes: 0xA2 + 78-byte report including CRC)
    bt_send_interrupt(device->conn_index, buf, 79);
    printf("[DS5_BT] Output: rumble L=%d R=%d, LED=%02X, RGB=%d/%d/%d\n",
           rumble_left, rumble_right, player_led, r, g, b);

    // Update cached state
    ds5->rumble_left = rumble_left;
    ds5->rumble_right = rumble_right;
    ds5->led_r = r;
    ds5->led_g = g;
    ds5->led_b = b;
    ds5->player_led = player_led;
}

// ============================================================================
// DRIVER IMPLEMENTATION
// ============================================================================

static bool ds5_match(const char* device_name, const uint8_t* class_of_device,
                      uint16_t vendor_id, uint16_t product_id, bool is_ble)
{
    (void)class_of_device;
    (void)is_ble;

    // VID/PID match (highest priority) - Sony vendor ID = 0x054C
    // DualSense = 0x0CE6, DualSense Edge = 0x0DF2
    if (vendor_id == 0x054C && (product_id == 0x0CE6 || product_id == 0x0DF2)) {
        return true;
    }

    // Name-based match (fallback if SDP query didn't return VID/PID)
    if (device_name) {
        if (strstr(device_name, "DualSense") != NULL) {
            return true;
        }
        if (strstr(device_name, "PS5 Controller") != NULL) {
            return true;
        }
    }

    return false;
}

static bool ds5_init(bthid_device_t* device)
{
    printf("[DS5_BT] Init for device: %s\n", device->name);

    // Find free data slot
    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (!ds5_data[i].initialized) {
            init_input_event(&ds5_data[i].event);
            ds5_data[i].initialized = true;
            ds5_data[i].activation_state = 0;
            ds5_data[i].activation_time = 0;
            ds5_data[i].output_seq = 0;
            ds5_data[i].rumble_left = 0;
            ds5_data[i].rumble_right = 0;
            ds5_data[i].led_r = 0;
            ds5_data[i].led_g = 0;
            ds5_data[i].led_b = 64;  // Default blue
            ds5_data[i].player_led = PLAYER_LED_PATTERNS[0];

            ds5_data[i].event.type = INPUT_TYPE_GAMEPAD;
            ds5_data[i].event.transport = INPUT_TRANSPORT_BT_CLASSIC;
            ds5_data[i].event.dev_addr = device->conn_index;
            ds5_data[i].event.instance = 0;
            ds5_data[i].event.button_count = 14;
            ds5_data[i].event.has_motion = true;

            device->driver_data = &ds5_data[i];

            // Activation happens in task (state machine with delays)
            return true;
        }
    }

    return false;
}

static bool ds5_process_debug_done = false;

static void ds5_process_report(bthid_device_t* device, const uint8_t* data, uint16_t len)
{
    ds5_bt_data_t* ds5 = (ds5_bt_data_t*)device->driver_data;
    if (!ds5 || len < 1) return;

    // Debug: print first report received
    if (!ds5_process_debug_done) {
        printf("[DS5_BT] Process report: len=%d, data[0]=0x%02X\n", len, data[0]);
        ds5_process_debug_done = true;
    }

    uint8_t report_id = data[0];
    const uint8_t* report_data = NULL;
    uint16_t report_len = 0;

    if (report_id == DS5_REPORT_BT_INPUT && len >= 12) {
        // Full BT report: report_id (1) + header (1) = skip 2 bytes
        report_data = data + 2;
        report_len = len - 2;
    } else if (report_id == DS5_REPORT_USB_INPUT && len >= 10) {
        // USB-style report: skip just report_id
        report_data = data + 1;
        report_len = len - 1;
    } else {
        // Unknown report format
        printf("[DS5_BT] Unknown report: len=%d, data[0]=0x%02X\n", len, data[0]);
        return;
    }

    if (report_len < sizeof(ds5_input_report_t)) {
        return;
    }

    const ds5_input_report_t* rpt = (const ds5_input_report_t*)report_data;

    // Parse D-pad (hat format)
    bool dpad_up    = (rpt->dpad == 0 || rpt->dpad == 1 || rpt->dpad == 7);
    bool dpad_right = (rpt->dpad >= 1 && rpt->dpad <= 3);
    bool dpad_down  = (rpt->dpad >= 3 && rpt->dpad <= 5);
    bool dpad_left  = (rpt->dpad >= 5 && rpt->dpad <= 7);

    // Build button state (inverted: 0 = pressed in USBR convention)
    uint32_t buttons = 0x00000000;

    if (dpad_up)       buttons |= JP_BUTTON_DU;
    if (dpad_down)     buttons |= JP_BUTTON_DD;
    if (dpad_left)     buttons |= JP_BUTTON_DL;
    if (dpad_right)    buttons |= JP_BUTTON_DR;
    if (rpt->cross)    buttons |= JP_BUTTON_B1;
    if (rpt->circle)   buttons |= JP_BUTTON_B2;
    if (rpt->square)   buttons |= JP_BUTTON_B3;
    if (rpt->triangle) buttons |= JP_BUTTON_B4;
    if (rpt->l1)       buttons |= JP_BUTTON_L1;
    if (rpt->r1)       buttons |= JP_BUTTON_R1;
    if (rpt->l2)       buttons |= JP_BUTTON_L2;
    if (rpt->r2)       buttons |= JP_BUTTON_R2;
    if (rpt->create)   buttons |= JP_BUTTON_S1;
    if (rpt->option)   buttons |= JP_BUTTON_S2;
    if (rpt->l3)       buttons |= JP_BUTTON_L3;
    if (rpt->r3)       buttons |= JP_BUTTON_R3;
    if (rpt->ps)       buttons |= JP_BUTTON_A1;
    if (rpt->tpad)     buttons |= JP_BUTTON_A2;

    // Update event
    ds5->event.buttons = buttons;

    // Analog sticks (HID convention: 0=up, 255=down)
    ds5->event.analog[ANALOG_LX] = rpt->x1;
    ds5->event.analog[ANALOG_LY] = rpt->y1;
    ds5->event.analog[ANALOG_RX] = rpt->x2;
    ds5->event.analog[ANALOG_RY] = rpt->y2;

    // Triggers
    ds5->event.analog[ANALOG_L2] = rpt->l2_trigger;
    ds5->event.analog[ANALOG_R2] = rpt->r2_trigger;

    // Motion data (DS5 has full 3-axis gyro and accel)
    // Check if we have enough data for motion
    if (report_len >= sizeof(ds5_input_report_t)) {
        ds5->event.has_motion = true;
        ds5->event.accel[0] = rpt->accel[0];
        ds5->event.accel[1] = rpt->accel[1];
        ds5->event.accel[2] = rpt->accel[2];
        ds5->event.gyro[0] = rpt->gyro[0];
        ds5->event.gyro[1] = rpt->gyro[1];
        ds5->event.gyro[2] = rpt->gyro[2];
    } else {
        ds5->event.has_motion = false;
    }

    // Battery: report_data[53] — bits 0-3 = level (0-10), bits 4-7 = status
    if (report_len > 53) {
        uint8_t raw = report_data[53];
        uint8_t level = raw & 0x0F;
        uint8_t status = (raw >> 4) & 0x0F;
        ds5->event.battery_level = (level > 10) ? 100 : level * 10;
        ds5->event.battery_charging = (status == 1);
    }

    // Submit to router
    router_submit_input(&ds5->event);
}

static void ds5_task(bthid_device_t* device)
{
    ds5_bt_data_t* ds5 = (ds5_bt_data_t*)device->driver_data;
    if (!ds5) return;

    uint32_t now = to_ms_since_boot(get_absolute_time());

    // State machine for activation with delays
    switch (ds5->activation_state) {
        case 0:  // Wait a moment then send initial LED
            ds5->activation_state = 1;
            ds5->activation_time = now;
            break;

        case 1:  // Wait 100ms then send initial LED
            if (now - ds5->activation_time >= 100) {
                // Set initial LED based on player index
                int player_idx = find_player_index(ds5->event.dev_addr, ds5->event.instance);
                int idx = (player_idx >= 0 && player_idx < 7) ? player_idx : 0;
                int pat_idx = (idx < 5) ? idx : idx % 5;
                ds5_send_output(device, 0, 0,
                    PLAYER_COLORS[idx][0], PLAYER_COLORS[idx][1], PLAYER_COLORS[idx][2],
                    PLAYER_LED_PATTERNS[pat_idx]);
                ds5->activation_state = 2;
            }
            break;

        case 2:  // Activated - monitor feedback system for rumble/LED updates
            {
                int player_idx = find_player_index(ds5->event.dev_addr, ds5->event.instance);
                if (player_idx >= 0) {
                    feedback_state_t* fb = feedback_get_state(player_idx);
                    if (!fb) break;

                    bool need_update = false;
                    uint8_t r = ds5->led_r;
                    uint8_t g = ds5->led_g;
                    uint8_t b = ds5->led_b;
                    uint8_t player_led = ds5->player_led;
                    uint8_t rumble_left = ds5->rumble_left;
                    uint8_t rumble_right = ds5->rumble_right;

                    // Calculate player LED from pattern (like DS3)
                    // DS5 has separate player LED bar and RGB lightbar
                    uint8_t calc_player_led;
                    if (fb->led.pattern != 0) {
                        // Map feedback pattern to player number via PLAYER_LEDS[] lookup
                        int player_num = 0;
                        for (int p = 1; p <= 7; p++) {
                            if (fb->led.pattern == PLAYER_LEDS[p]) {
                                player_num = p - 1;
                                break;
                            }
                        }
                        int pat_idx = (player_num < 5) ? player_num : player_num % 5;
                        calc_player_led = PLAYER_LED_PATTERNS[pat_idx];
                    } else {
                        // Default to player index
                        int idx = (player_idx < 5) ? player_idx : player_idx % 5;
                        calc_player_led = PLAYER_LED_PATTERNS[idx];
                    }

                    // Check if player LED changed
                    if (calc_player_led != ds5->player_led) {
                        player_led = calc_player_led;
                        need_update = true;
                    }

                    // Check RGB lightbar from feedback
                    if (fb->led_dirty) {
                        if (fb->led.r != 0 || fb->led.g != 0 || fb->led.b != 0) {
                            // Host specified RGB color directly
                            r = fb->led.r;
                            g = fb->led.g;
                            b = fb->led.b;
                        } else if (fb->led.pattern != 0) {
                            // Use player color based on pattern via PLAYER_LEDS[] lookup
                            int player_num = 0;
                            for (int p = 1; p <= 7; p++) {
                                if (fb->led.pattern == PLAYER_LEDS[p]) {
                                    player_num = p - 1;
                                    break;
                                }
                            }
                            int color_idx = (player_num < 7) ? player_num : player_num % 7;
                            r = PLAYER_COLORS[color_idx][0];
                            g = PLAYER_COLORS[color_idx][1];
                            b = PLAYER_COLORS[color_idx][2];
                        } else {
                            // Default to player index color
                            int idx = (player_idx < 7) ? player_idx : player_idx % 7;
                            r = PLAYER_COLORS[idx][0];
                            g = PLAYER_COLORS[idx][1];
                            b = PLAYER_COLORS[idx][2];
                        }
                        player_led = calc_player_led;
                        need_update = true;
                    }

                    // Check rumble
                    if (fb->rumble_dirty) {
                        rumble_left = fb->rumble.left;
                        rumble_right = fb->rumble.right;
                        need_update = true;
                    }

                    // Also check if values changed (even without dirty flag)
                    if (rumble_left != ds5->rumble_left || rumble_right != ds5->rumble_right ||
                        r != ds5->led_r || g != ds5->led_g || b != ds5->led_b ||
                        player_led != ds5->player_led) {
                        need_update = true;
                    }

                    if (need_update) {
                        ds5_send_output(device, rumble_left, rumble_right, r, g, b, player_led);
                        feedback_clear_dirty(player_idx);
                    }
                }
            }
            break;
    }
}

static void ds5_disconnect(bthid_device_t* device)
{
    printf("[DS5_BT] Disconnect: %s\n", device->name);

    ds5_bt_data_t* ds5 = (ds5_bt_data_t*)device->driver_data;
    if (ds5) {
        // Clear router state first (sends zeroed input report)
        router_device_disconnected(ds5->event.dev_addr, ds5->event.instance);
        // Remove player assignment
        remove_players_by_address(ds5->event.dev_addr, ds5->event.instance);

        init_input_event(&ds5->event);
        ds5->initialized = false;
    }
}

// ============================================================================
// DRIVER STRUCT
// ============================================================================

const bthid_driver_t ds5_bt_driver = {
    .name = "Sony DualSense (BT)",
    .match = ds5_match,
    .init = ds5_init,
    .process_report = ds5_process_report,
    .task = ds5_task,
    .disconnect = ds5_disconnect,
};

void ds5_bt_register(void)
{
    bthid_register_driver(&ds5_bt_driver);
}
