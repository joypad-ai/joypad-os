// ds4_bt.c - Sony DualShock 4 Bluetooth Driver
// Handles DS4 controllers over Bluetooth
//
// Report format reference: https://www.psdevwiki.com/ps4/DS4-BT
// BT reports have 2-byte offset compared to USB (report ID 0x11 vs 0x01)

#include "ds4_bt.h"
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

// Player LED colors (RGB values)
static const uint8_t PLAYER_COLORS[][3] = {
    {  0,   0,  64 },   // Player 1: Blue
    { 64,   0,   0 },   // Player 2: Red
    {  0,  64,   0 },   // Player 3: Green
    { 64,   0,  64 },   // Player 4: Pink/Fuchsia
};

// ============================================================================
// DS4 REPORT STRUCTURE (same as USB, but BT has 2-byte header offset)
// ============================================================================

typedef struct __attribute__((packed)) {
    uint8_t x, y, z, rz;    // Joysticks (0-255, centered at 128)

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
        uint8_t share  : 1;
        uint8_t option : 1;
        uint8_t l3     : 1;
        uint8_t r3     : 1;
    };

    struct {
        uint8_t ps      : 1;
        uint8_t tpad    : 1;
        uint8_t counter : 6;
    };

    uint8_t l2_trigger;
    uint8_t r2_trigger;

    // Extended data for motion
    uint16_t timestamp;
    uint8_t battery;
    int16_t gyro[3];    // x, y, z
    int16_t accel[3];   // x, y, z
    // Touchpad etc follows but not parsed
} ds4_input_report_t;

// DS4 BT output report (for rumble/LED)
typedef struct __attribute__((packed)) {
    uint8_t report_id;      // 0x11 for BT
    uint8_t flags1;         // 0x80
    uint8_t flags2;         // 0x00
    uint8_t flags3;         // 0xFF (enable rumble+LED)

    uint8_t reserved1[2];

    uint8_t rumble_right;   // High frequency
    uint8_t rumble_left;    // Low frequency

    uint8_t led_red;
    uint8_t led_green;
    uint8_t led_blue;

    uint8_t flash_on;       // LED flash on duration
    uint8_t flash_off;      // LED flash off duration

    uint8_t reserved2[8];

    // Total: 23 bytes for basic output
} ds4_bt_output_report_t;

// ============================================================================
// DRIVER DATA
// ============================================================================

typedef struct {
    input_event_t event;
    bool initialized;
    bool sixaxis_enabled;
    uint8_t activation_state;
    uint32_t activation_time;

    // Current feedback state (for change detection)
    uint8_t rumble_left;
    uint8_t rumble_right;
    uint8_t led_r, led_g, led_b;
} ds4_bt_data_t;

static ds4_bt_data_t ds4_data[BTHID_MAX_DEVICES];

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static void ds4_send_output(bthid_device_t* device, uint8_t rumble_left, uint8_t rumble_right,
                            uint8_t r, uint8_t g, uint8_t b)
{
    ds4_bt_data_t* ds4 = (ds4_bt_data_t*)device->driver_data;
    if (!ds4) return;

    // DS4 BT output report - must use SET_REPORT on control channel
    // Format: [SET_REPORT header][Report ID 0x11][flags][data...]
    uint8_t buf[79];
    memset(buf, 0, sizeof(buf));

    buf[0] = 0x52;  // SET_REPORT | Output (0x50 | 0x02)
    buf[1] = 0x11;  // Report ID
    buf[2] = 0x80;  // Flags (BT)
    buf[3] = 0x00;
    buf[4] = 0xFF;  // Enable rumble+LED

    buf[7] = rumble_right;  // High frequency motor
    buf[8] = rumble_left;   // Low frequency motor
    buf[9] = r;
    buf[10] = g;
    buf[11] = b;

    bt_send_control(device->conn_index, buf, sizeof(buf));

    // Update cached state
    ds4->rumble_left = rumble_left;
    ds4->rumble_right = rumble_right;
    ds4->led_r = r;
    ds4->led_g = g;
    ds4->led_b = b;
}

static void ds4_enable_sixaxis(bthid_device_t* device)
{
    // Send GET_REPORT Feature to enable full reports
    // buf[0] = 0x43 (GET_REPORT | Feature), buf[1] = 0x02 (report ID)
    uint8_t buf[2] = {0x43, 0x02};
    bt_send_control(device->conn_index, buf, sizeof(buf));
}

// ============================================================================
// DRIVER IMPLEMENTATION
// ============================================================================

static bool ds4_match(const char* device_name, const uint8_t* class_of_device,
                      uint16_t vendor_id, uint16_t product_id, bool is_ble)
{
    (void)class_of_device;
    (void)is_ble;

    // VID/PID match (highest priority) - Sony vendor ID = 0x054C
    // DS4 v1 = 0x05C4, DS4 v2 (Slim) = 0x09CC
    if (vendor_id == 0x054C && (product_id == 0x05C4 || product_id == 0x09CC)) {
        return true;
    }

    // Don't match DualSense by VID/PID (DS5 driver handles those)
    if (vendor_id == 0x054C && (product_id == 0x0CE6 || product_id == 0x0DF2)) {
        return false;
    }

    // Name-based match (fallback if SDP query didn't return VID/PID)
    if (device_name) {
        // Don't match DualSense (DS5) - let DS5 driver handle it
        if (strstr(device_name, "DualSense") != NULL) {
            return false;
        }

        // Don't match Xbox controllers
        if (strstr(device_name, "Xbox") != NULL) {
            return false;
        }

        // Match known DS4 device names
        // Note: DS4 advertises as just "Wireless Controller" (no "Sony" prefix)
        if (strstr(device_name, "Wireless Controller") != NULL) {
            return true;
        }
        if (strstr(device_name, "DUALSHOCK 4") != NULL) {
            return true;
        }
    }

    return false;
}

static bool ds4_init(bthid_device_t* device)
{
    printf("[DS4_BT] Init for device: %s\n", device->name);

    // Find free data slot
    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (!ds4_data[i].initialized) {
            init_input_event(&ds4_data[i].event);
            ds4_data[i].initialized = true;
            ds4_data[i].sixaxis_enabled = false;
            ds4_data[i].activation_state = 0;
            ds4_data[i].activation_time = 0;
            ds4_data[i].rumble_left = 0;
            ds4_data[i].rumble_right = 0;
            ds4_data[i].led_r = 0;
            ds4_data[i].led_g = 0;
            ds4_data[i].led_b = 64;  // Default blue

            ds4_data[i].event.type = INPUT_TYPE_GAMEPAD;
            ds4_data[i].event.transport = INPUT_TRANSPORT_BT_CLASSIC;
            ds4_data[i].event.dev_addr = device->conn_index;
            ds4_data[i].event.instance = 0;
            ds4_data[i].event.button_count = 14;
            ds4_data[i].event.has_motion = true;  // DS4 has motion

            device->driver_data = &ds4_data[i];

            // Activation happens in task (state machine with delays)
            return true;
        }
    }

    return false;
}

static bool ds4_process_debug_done = false;

static void ds4_process_report(bthid_device_t* device, const uint8_t* data, uint16_t len)
{
    ds4_bt_data_t* ds4 = (ds4_bt_data_t*)device->driver_data;
    if (!ds4) {
        return;
    }

    // Debug: print first report received
    if (!ds4_process_debug_done) {
        printf("[DS4_BT] Process report: len=%d, data[0]=0x%02X\n", len, len > 0 ? data[0] : 0);
        ds4_process_debug_done = true;
    }

    // BT reports have different report IDs:
    // 0x01 = Basic report (no motion/touchpad)
    // 0x11 = Full report (with motion/touchpad)

    const uint8_t* report_data = NULL;
    uint16_t report_len = 0;

    if (len >= 1 && data[0] == 0x11 && len >= 12) {
        // Full BT report - skip 3 bytes (report ID + 2 header bytes)
        report_data = data + 3;
        report_len = len - 3;
        ds4->sixaxis_enabled = true;
    } else if (len >= 1 && data[0] == 0x01 && len >= 10) {
        // Basic report - skip 1 byte (report ID)
        report_data = data + 1;
        report_len = len - 1;
    } else {
        // Unknown report format
        printf("[DS4_BT] Unknown report: len=%d, data[0]=0x%02X\n", len, len > 0 ? data[0] : 0);
        return;
    }

    if (report_len < sizeof(ds4_input_report_t)) {
        return;
    }

    const ds4_input_report_t* rpt = (const ds4_input_report_t*)report_data;

    // Parse D-pad (hat format)
    bool dpad_up    = (rpt->dpad == 0 || rpt->dpad == 1 || rpt->dpad == 7);
    bool dpad_right = (rpt->dpad >= 1 && rpt->dpad <= 3);
    bool dpad_down  = (rpt->dpad >= 3 && rpt->dpad <= 5);
    bool dpad_left  = (rpt->dpad >= 5 && rpt->dpad <= 7);

    // Build button state (inverted: 0 = pressed in USBR convention)
    uint32_t buttons = 0x00000000;  // All released (active-high)

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
    if (rpt->share)    buttons |= JP_BUTTON_S1;
    if (rpt->option)   buttons |= JP_BUTTON_S2;
    if (rpt->l3)       buttons |= JP_BUTTON_L3;
    if (rpt->r3)       buttons |= JP_BUTTON_R3;
    if (rpt->ps)       buttons |= JP_BUTTON_A1;
    if (rpt->tpad)     buttons |= JP_BUTTON_A2;

    // Update event
    ds4->event.buttons = buttons;

    // Analog sticks (HID convention: 0=up, 255=down)
    ds4->event.analog[ANALOG_LX] = rpt->x;
    ds4->event.analog[ANALOG_LY] = rpt->y;
    ds4->event.analog[ANALOG_RX] = rpt->z;
    ds4->event.analog[ANALOG_RY] = rpt->rz;

    // Triggers
    ds4->event.analog[ANALOG_L2] = rpt->l2_trigger;
    ds4->event.analog[ANALOG_R2] = rpt->r2_trigger;

    // Motion data (DS4 has full 3-axis gyro and accel)
    // Only available in full report mode (report_len includes gyro/accel)
    if (ds4->sixaxis_enabled && report_len >= sizeof(ds4_input_report_t)) {
        ds4->event.has_motion = true;
        ds4->event.accel[0] = rpt->accel[0];
        ds4->event.accel[1] = rpt->accel[1];
        ds4->event.accel[2] = rpt->accel[2];
        ds4->event.gyro[0] = rpt->gyro[0];
        ds4->event.gyro[1] = rpt->gyro[1];
        ds4->event.gyro[2] = rpt->gyro[2];
    } else {
        ds4->event.has_motion = false;
    }

    // Submit to router
    router_submit_input(&ds4->event);
}

static void ds4_task(bthid_device_t* device)
{
    ds4_bt_data_t* ds4 = (ds4_bt_data_t*)device->driver_data;
    if (!ds4) return;

    uint32_t now = to_ms_since_boot(get_absolute_time());

    // State machine for activation with delays
    switch (ds4->activation_state) {
        case 0:  // Send enable_sixaxis
            ds4_enable_sixaxis(device);
            ds4->activation_state = 1;
            ds4->activation_time = now;
            break;

        case 1:  // Wait 100ms then send initial LED
            if (now - ds4->activation_time >= 100) {
                // Set initial LED color (blue for player 1)
                ds4_send_output(device, 0, 0, 0, 0, 64);
                ds4->activation_state = 2;
            }
            break;

        case 2:  // Activated - monitor feedback system for rumble/LED updates
            {
                int player_idx = find_player_index(ds4->event.dev_addr, ds4->event.instance);
                if (player_idx >= 0) {
                    feedback_state_t* fb = feedback_get_state(player_idx);
                    if (!fb) break;

                    bool need_update = false;
                    uint8_t r = ds4->led_r;
                    uint8_t g = ds4->led_g;
                    uint8_t b = ds4->led_b;
                    uint8_t rumble_left = ds4->rumble_left;
                    uint8_t rumble_right = ds4->rumble_right;

                    // Check LED from feedback system
                    if (fb->led_dirty) {
                        if (fb->led.r != 0 || fb->led.g != 0 || fb->led.b != 0) {
                            // Host specified RGB color directly
                            r = fb->led.r;
                            g = fb->led.g;
                            b = fb->led.b;
                        } else if (fb->led.pattern != 0) {
                            // Player LED pattern - convert to RGB color
                            // Pattern bits: 0x01=P1, 0x02=P2, 0x04=P3, 0x08=P4
                            int player_num = 0;
                            if (fb->led.pattern & 0x01) player_num = 0;
                            else if (fb->led.pattern & 0x02) player_num = 1;
                            else if (fb->led.pattern & 0x04) player_num = 2;
                            else if (fb->led.pattern & 0x08) player_num = 3;

                            r = PLAYER_COLORS[player_num][0];
                            g = PLAYER_COLORS[player_num][1];
                            b = PLAYER_COLORS[player_num][2];
                        } else {
                            // Default to player index-based color
                            int color_idx = player_idx % 4;
                            r = PLAYER_COLORS[color_idx][0];
                            g = PLAYER_COLORS[color_idx][1];
                            b = PLAYER_COLORS[color_idx][2];
                        }
                        need_update = true;
                    }

                    // Check rumble
                    if (fb->rumble_dirty) {
                        rumble_left = fb->rumble.left;
                        rumble_right = fb->rumble.right;
                        need_update = true;
                    }

                    // Also check if values changed (even without dirty flag)
                    if (rumble_left != ds4->rumble_left || rumble_right != ds4->rumble_right ||
                        r != ds4->led_r || g != ds4->led_g || b != ds4->led_b) {
                        need_update = true;
                    }

                    if (need_update) {
                        ds4_send_output(device, rumble_left, rumble_right, r, g, b);
                        feedback_clear_dirty(player_idx);
                    }
                }
            }
            break;
    }
}

static void ds4_disconnect(bthid_device_t* device)
{
    printf("[DS4_BT] Disconnect: %s\n", device->name);

    ds4_bt_data_t* ds4 = (ds4_bt_data_t*)device->driver_data;
    if (ds4) {
        // Clear router state first (sends zeroed input report)
        router_device_disconnected(ds4->event.dev_addr, ds4->event.instance);
        // Remove player assignment
        remove_players_by_address(ds4->event.dev_addr, ds4->event.instance);

        init_input_event(&ds4->event);
        ds4->initialized = false;
    }
}

// ============================================================================
// DRIVER STRUCT
// ============================================================================

const bthid_driver_t ds4_bt_driver = {
    .name = "Sony DualShock 4 (BT)",
    .match = ds4_match,
    .init = ds4_init,
    .process_report = ds4_process_report,
    .task = ds4_task,
    .disconnect = ds4_disconnect,
};

void ds4_bt_register(void)
{
    bthid_register_driver(&ds4_bt_driver);
}
