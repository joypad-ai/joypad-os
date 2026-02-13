// wiimote_bt.c - Nintendo Wiimote Bluetooth Driver
//
// Supports the Wiimote (RVL-CNT-01) core buttons and Nunchuk extension.
// Device name: "Nintendo RVL-CNT-01"
//
// References:
// - USB_Host_Shield_2.0/Wii.cpp
// - https://wiibrew.org/wiki/Wiimote

#include "wiimote_bt.h"
#include "bt/bthid/bthid.h"
#include "bt/btstack/btstack_host.h"
#include "core/input_event.h"
#include "core/router/router.h"
#include "core/buttons.h"
#include "core/services/players/manager.h"
#include "core/services/players/feedback.h"
#include "core/services/storage/flash.h"
#include <string.h>
#include <stdio.h>
#include "pico/time.h"

#define WIIMOTE_INIT_DELAY_MS     100
#define WIIMOTE_INIT_MAX_RETRIES  5
#define WIIMOTE_KEEPALIVE_MS      30000

// ============================================================================
// WIIMOTE BUTTON BITS (from core buttons in bytes 10-11)
// ============================================================================

// Byte 10 (bits 0-4 used)
#define WII_BTN_LEFT    0x0001
#define WII_BTN_RIGHT   0x0002
#define WII_BTN_DOWN    0x0004
#define WII_BTN_UP      0x0008
#define WII_BTN_PLUS    0x0010

// Byte 11 (bits 0,1,2,3,4,7 used)
#define WII_BTN_TWO     0x0100
#define WII_BTN_ONE     0x0200
#define WII_BTN_B       0x0400
#define WII_BTN_A       0x0800
#define WII_BTN_MINUS   0x1000
#define WII_BTN_HOME    0x8000

// Nunchuk buttons (from extension byte 5, inverted)
#define WII_BTN_Z       0x01  // Bit 0
#define WII_BTN_C       0x02  // Bit 1

// Classic Controller buttons (from extension bytes 4-5, inverted)
// Byte 4: BDR, BDD, BLT, B-, BH, B+, BRT, (1)
#define WII_CC_BTN_RT       0x0002  // Right trigger click
#define WII_CC_BTN_PLUS     0x0004
#define WII_CC_BTN_HOME     0x0008
#define WII_CC_BTN_MINUS    0x0010
#define WII_CC_BTN_LT       0x0020  // Left trigger click
#define WII_CC_BTN_DOWN     0x0040
#define WII_CC_BTN_RIGHT    0x0080
// Byte 5: BZL, BB, BY, BA, BX, BZR, BDL, BDU
#define WII_CC_BTN_UP       0x0100
#define WII_CC_BTN_LEFT     0x0200
#define WII_CC_BTN_ZR       0x0400
#define WII_CC_BTN_X        0x0800
#define WII_CC_BTN_A        0x1000
#define WII_CC_BTN_Y        0x2000
#define WII_CC_BTN_B        0x4000
#define WII_CC_BTN_ZL       0x8000

// Extension types
typedef enum {
    WII_EXT_NONE,
    WII_EXT_NUNCHUK,
    WII_EXT_CLASSIC,       // Classic Controller / Classic Controller Pro (has analog sticks)
    WII_EXT_CLASSIC_MINI,  // NES/SNES Classic Controller (digital only, no sticks)
    WII_EXT_GUITAR,        // Guitar Hero guitar
} wiimote_ext_type_t;

// Guitar Hero button bits (bytes 4-5, active low)
// Byte 4: BD=bit6, B-=bit4, B+=bit2
// Byte 5: BO=bit7, BR=bit6, BY=bit3, BG=bit4, BB=bit5, BU=bit0
#define GH_BTN_STRUM_DOWN   0x0040  // Byte 4 bit 6
#define GH_BTN_MINUS        0x0010  // Byte 4 bit 4
#define GH_BTN_PLUS         0x0004  // Byte 4 bit 2
#define GH_BTN_STRUM_UP     0x0100  // Byte 5 bit 0
#define GH_BTN_GREEN        0x1000  // Byte 5 bit 4
#define GH_BTN_RED          0x4000  // Byte 5 bit 6
#define GH_BTN_YELLOW       0x0800  // Byte 5 bit 3
#define GH_BTN_BLUE         0x2000  // Byte 5 bit 5
#define GH_BTN_ORANGE       0x8000  // Byte 5 bit 7

// Report IDs
#define WII_REPORT_STATUS       0x20
#define WII_REPORT_READ_DATA    0x21
#define WII_REPORT_ACK          0x22
#define WII_REPORT_BUTTONS      0x30  // Core buttons only
#define WII_REPORT_BUTTONS_ACC  0x31  // Buttons + accelerometer
#define WII_REPORT_BUTTONS_EXT8 0x32  // Buttons + 8 extension bytes
#define WII_REPORT_BUTTONS_ACC_IR 0x33  // Buttons + accel + IR
#define WII_REPORT_BUTTONS_EXT19 0x34  // Buttons + 19 extension bytes
#define WII_REPORT_BUTTONS_ACC_EXT16 0x35  // Buttons + accel + 16 extension
#define WII_REPORT_BUTTONS_IR_EXT9 0x36  // Buttons + IR + 9 extension
#define WII_REPORT_BUTTONS_ACC_IR_EXT6 0x37  // Buttons + accel + IR + 6 ext

// Accelerometer orientation detection
// When Wiimote is held sideways (NES style), X-axis deviates from center (~101 or ~155)
// When pointing at screen (vertical), X-axis stays near center (~128)
#define WII_ACCEL_CENTER    128
#define WII_ACCEL_THRESH_ON  20  // Threshold to switch TO horizontal (lower = more sensitive)
#define WII_ACCEL_THRESH_OFF 12  // Threshold to switch FROM horizontal (hysteresis)

// Output report IDs
#define WII_CMD_LED             0x11
#define WII_CMD_REPORT_MODE     0x12
#define WII_CMD_STATUS_REQ      0x15
#define WII_CMD_WRITE_DATA      0x16
#define WII_CMD_READ_DATA       0x17

// ============================================================================
// DRIVER STATE
// ============================================================================

typedef enum {
    WII_STATE_IDLE,
    WII_STATE_WAIT_INIT,
    WII_STATE_SEND_STATUS_REQ,
    WII_STATE_WAIT_STATUS,
    WII_STATE_SEND_EXT_INIT1,
    WII_STATE_WAIT_EXT_INIT1_ACK,
    WII_STATE_SEND_EXT_INIT2,
    WII_STATE_WAIT_EXT_INIT2_ACK,
    WII_STATE_READ_EXT_TYPE,
    WII_STATE_WAIT_EXT_TYPE,
    WII_STATE_SEND_REPORT_MODE,
    WII_STATE_WAIT_REPORT_ACK,
    WII_STATE_SEND_LED,
    WII_STATE_WAIT_LED_ACK,
    WII_STATE_READY
} wiimote_state_t;

// Wiimote orientation (current detected state)
typedef enum {
    WII_ORIENT_HORIZONTAL,  // Sideways like NES controller (D-pad on left)
    WII_ORIENT_VERTICAL,    // Pointing at screen (D-pad on top)
} wiimote_orient_t;

// Global orientation mode setting (shared across all Wiimotes)
// Uses WII_ORIENT_MODE_* constants from header
static uint8_t wiimote_orient_mode = WII_ORIENT_MODE_AUTO;

typedef struct {
    input_event_t event;
    bool initialized;
    wiimote_state_t state;
    uint32_t init_time;
    uint8_t init_retries;
    uint32_t last_keepalive;
    wiimote_ext_type_t ext_type;
    bool extension_connected;
    uint8_t player_led;
    bool rumble_on;
    wiimote_orient_t orientation;
    bool orient_hotkey_active;  // Prevent repeated hotkey triggers
} wiimote_data_t;

static wiimote_data_t wiimote_data[BTHID_MAX_DEVICES];

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Set LEDs using raw pattern (bits 4-7 = LEDs 1-4)
static void wiimote_set_leds_raw(bthid_device_t* device, uint8_t led_pattern)
{
    uint8_t buf[3] = { 0xA2, WII_CMD_LED, led_pattern };
    btstack_wiimote_send_raw(device->conn_index, buf, sizeof(buf));
}

static void wiimote_set_leds(bthid_device_t* device, uint8_t player)
{
    uint8_t led_pattern = 0;
    if (player >= 1 && player <= 4) {
        led_pattern = (1 << (player + 3));
    }
    wiimote_set_leds_raw(device, led_pattern);
}

static bool wiimote_request_status(bthid_device_t* device)
{
    uint8_t buf[3] = { 0xA2, WII_CMD_STATUS_REQ, 0x00 };
    return btstack_wiimote_send_control(device->conn_index, buf, sizeof(buf));
}

static bool wiimote_write_data(bthid_device_t* device, uint32_t address, uint8_t data)
{
    uint8_t buf[23];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0xA2;
    buf[1] = WII_CMD_WRITE_DATA;
    buf[2] = 0x04;  // Extension register space
    buf[3] = (uint8_t)((address >> 16) & 0xFF);
    buf[4] = (uint8_t)((address >> 8) & 0xFF);
    buf[5] = (uint8_t)(address & 0xFF);
    buf[6] = 0x01;  // Size = 1
    buf[7] = data;
    return btstack_wiimote_send_control(device->conn_index, buf, sizeof(buf));
}

static bool wiimote_read_data(bthid_device_t* device, uint32_t address, uint16_t size)
{
    uint8_t buf[8];
    buf[0] = 0xA2;
    buf[1] = WII_CMD_READ_DATA;
    buf[2] = 0x04;  // Extension register space
    buf[3] = (uint8_t)((address >> 16) & 0xFF);
    buf[4] = (uint8_t)((address >> 8) & 0xFF);
    buf[5] = (uint8_t)(address & 0xFF);
    buf[6] = (uint8_t)((size >> 8) & 0xFF);
    buf[7] = (uint8_t)(size & 0xFF);
    return btstack_wiimote_send_control(device->conn_index, buf, sizeof(buf));
}

static void wiimote_set_report_mode(bthid_device_t* device, bool has_extension)
{
    // 0x35 = buttons + accel + 16 ext bytes (for orientation detection + extension)
    // 0x31 = buttons + accel only (for orientation detection, no extension)
    uint8_t mode = has_extension ? 0x35 : 0x31;
    uint8_t buf[4] = { 0xA2, WII_CMD_REPORT_MODE, 0x00, mode };
    printf("[WIIMOTE] Setting report mode 0x%02X\n", mode);
    btstack_wiimote_send_raw(device->conn_index, buf, sizeof(buf));
}

// Set rumble on/off
// Report 0x10: rumble only, bit 0 = on/off
static void wiimote_set_rumble(bthid_device_t* device, bool on)
{
    uint8_t buf[3] = { 0xA2, 0x10, on ? 0x01 : 0x00 };
    btstack_wiimote_send_raw(device->conn_index, buf, sizeof(buf));
}

// Detect orientation from accelerometer data with hysteresis
// Returns WII_ORIENT_HORIZONTAL if held sideways (X-axis has gravity)
// Returns WII_ORIENT_VERTICAL otherwise
static wiimote_orient_t wiimote_detect_orientation(uint8_t accel_x, wiimote_orient_t current)
{
    // Calculate X-axis deviation from center (zero-G)
    int x_dev = (int)accel_x - WII_ACCEL_CENTER;
    if (x_dev < 0) x_dev = -x_dev;

    // Use hysteresis to prevent flapping
    if (current == WII_ORIENT_VERTICAL) {
        // Currently vertical - need higher threshold to switch to horizontal
        if (x_dev >= WII_ACCEL_THRESH_ON) {
            return WII_ORIENT_HORIZONTAL;
        }
    } else {
        // Currently horizontal - need lower threshold to switch to vertical
        if (x_dev < WII_ACCEL_THRESH_OFF) {
            return WII_ORIENT_VERTICAL;
        }
    }

    return current;  // Stay in current state
}

// Rotate controls based on orientation
// Vertical (pointing at screen): no rotation needed
// Horizontal (sideways NES-style): rotate D-pad and swap face buttons
static uint32_t wiimote_rotate_controls(uint32_t buttons, wiimote_orient_t orient)
{
    if (orient == WII_ORIENT_VERTICAL) {
        return buttons;  // No rotation
    }

    // Horizontal orientation: rotate D-pad 90° counter-clockwise
    uint32_t dpad = buttons & (JP_BUTTON_DU | JP_BUTTON_DD | JP_BUTTON_DL | JP_BUTTON_DR);
    uint32_t face = buttons & (JP_BUTTON_B1 | JP_BUTTON_B2 | JP_BUTTON_B3 | JP_BUTTON_B4);
    uint32_t other = buttons & ~(JP_BUTTON_DU | JP_BUTTON_DD | JP_BUTTON_DL | JP_BUTTON_DR |
                                  JP_BUTTON_B1 | JP_BUTTON_B2 | JP_BUTTON_B3 | JP_BUTTON_B4);

    // Rotate D-pad counter-clockwise
    uint32_t rotated_dpad = 0;
    if (dpad & JP_BUTTON_DU) rotated_dpad |= JP_BUTTON_DL;  // Up -> Left
    if (dpad & JP_BUTTON_DL) rotated_dpad |= JP_BUTTON_DD;  // Left -> Down
    if (dpad & JP_BUTTON_DD) rotated_dpad |= JP_BUTTON_DR;  // Down -> Right
    if (dpad & JP_BUTTON_DR) rotated_dpad |= JP_BUTTON_DU;  // Right -> Up

    // Swap face buttons: B1↔B3, B2↔B4
    // Makes 1/2 buttons become primary when held sideways
    uint32_t swapped_face = 0;
    if (face & JP_BUTTON_B1) swapped_face |= JP_BUTTON_B3;  // B -> B3
    if (face & JP_BUTTON_B2) swapped_face |= JP_BUTTON_B4;  // A -> B4
    if (face & JP_BUTTON_B3) swapped_face |= JP_BUTTON_B1;  // 1 -> B1
    if (face & JP_BUTTON_B4) swapped_face |= JP_BUTTON_B2;  // 2 -> B2

    return other | rotated_dpad | swapped_face;
}

// ============================================================================
// DRIVER IMPLEMENTATION
// ============================================================================

static bool wiimote_match(const char* device_name, const uint8_t* class_of_device,
                          uint16_t vendor_id, uint16_t product_id, bool is_ble)
{
    (void)class_of_device;
    (void)is_ble;

    // Match by VID/PID (Nintendo VID = 0x057E, Wiimote PID = 0x0306)
    if (vendor_id == 0x057E && product_id == 0x0306) {
        return true;
    }

    // Match by name (exclude Wii U Pro which has "-UC" suffix)
    if (device_name && strstr(device_name, "Nintendo RVL-CNT-01") != NULL &&
        strstr(device_name, "-UC") == NULL) {
        return true;
    }

    return false;
}

static bool wiimote_init(bthid_device_t* device)
{
    printf("[WIIMOTE] Init: %s\n", device->name);

    for (int i = 0; i < BTHID_MAX_DEVICES; i++) {
        if (!wiimote_data[i].initialized) {
            init_input_event(&wiimote_data[i].event);
            wiimote_data[i].initialized = true;
            wiimote_data[i].event.type = INPUT_TYPE_GAMEPAD;
            wiimote_data[i].event.transport = INPUT_TRANSPORT_BT_CLASSIC;
            wiimote_data[i].event.dev_addr = device->conn_index;
            wiimote_data[i].event.instance = 0;
            wiimote_data[i].event.button_count = 11;  // Wiimote has fewer buttons
            wiimote_data[i].ext_type = WII_EXT_NONE;
            wiimote_data[i].extension_connected = false;

            device->driver_data = &wiimote_data[i];

            wiimote_data[i].state = WII_STATE_WAIT_INIT;
            wiimote_data[i].init_time = time_us_32() + (WIIMOTE_INIT_DELAY_MS * 1000);
            wiimote_data[i].init_retries = 0;

            printf("[WIIMOTE] Init started, waiting %d ms\n", WIIMOTE_INIT_DELAY_MS);
            return true;
        }
    }
    return false;
}

static void wiimote_process_report(bthid_device_t* device, const uint8_t* data, uint16_t len)
{
    wiimote_data_t* wii = (wiimote_data_t*)device->driver_data;
    if (!wii || len < 1) return;

    uint8_t report_id = data[0];

    // Parse core buttons from most report types
    if ((report_id >= 0x30 && report_id <= 0x37) || report_id == 0x3e || report_id == 0x3f) {
        if (len >= 3) {
            // Core buttons in bytes 1-2 (after HID header byte 0)
            // Byte 1: bits 0-4 used (LEFT, RIGHT, DOWN, UP, PLUS)
            // Byte 2: bits 0,1,2,3,4,7 used (TWO, ONE, B, A, MINUS, HOME)
            uint16_t raw_buttons = ((data[1] & 0x1F) | ((data[2] & 0x9F) << 8));

            uint32_t buttons = 0;

            // D-pad (will be rotated based on orientation if no extension)
            if (raw_buttons & WII_BTN_UP)    buttons |= JP_BUTTON_DU;
            if (raw_buttons & WII_BTN_DOWN)  buttons |= JP_BUTTON_DD;
            if (raw_buttons & WII_BTN_LEFT)  buttons |= JP_BUTTON_DL;
            if (raw_buttons & WII_BTN_RIGHT) buttons |= JP_BUTTON_DR;

            // Face buttons (Wiimote held sideways: 1=left, 2=right, A=top, B=bottom trigger)
            if (raw_buttons & WII_BTN_A)     buttons |= JP_BUTTON_B2;  // A
            if (raw_buttons & WII_BTN_B)     buttons |= JP_BUTTON_B1;  // B (trigger)
            if (raw_buttons & WII_BTN_ONE)   buttons |= JP_BUTTON_B3;  // 1
            if (raw_buttons & WII_BTN_TWO)   buttons |= JP_BUTTON_B4;  // 2

            // System buttons
            if (raw_buttons & WII_BTN_MINUS) buttons |= JP_BUTTON_S1;
            if (raw_buttons & WII_BTN_PLUS)  buttons |= JP_BUTTON_S2;
            if (raw_buttons & WII_BTN_HOME)  buttons |= JP_BUTTON_A1;

            /// Orientation hotkeys: S2 (Plus) + D-pad Up = Vertical, Right = Horizontal, Down/Left = Auto
            bool plus_held = (raw_buttons & WII_BTN_PLUS) != 0;
            bool up_held = (raw_buttons & WII_BTN_UP) != 0;
            bool down_held = (raw_buttons & WII_BTN_DOWN) != 0;
            bool left_held = (raw_buttons & WII_BTN_LEFT) != 0;
            bool right_held = (raw_buttons & WII_BTN_RIGHT) != 0;

            if (plus_held && (up_held || down_held || left_held || right_held)) {
                if (!wii->orient_hotkey_active) {
                    wii->orient_hotkey_active = true;
                    uint8_t new_mode = up_held ? WII_ORIENT_MODE_VERTICAL :
                                       right_held ? WII_ORIENT_MODE_HORIZONTAL :
                                       WII_ORIENT_MODE_AUTO;
                    if (new_mode != wiimote_orient_mode) {
                        wiimote_orient_mode = new_mode;
                        printf("[WIIMOTE] Hotkey: orientation set to %s\n",
                               wiimote_get_orient_mode_name(wiimote_orient_mode));
                        // Queue flash save (deferred until BT disconnects)
                        flash_t flash_data;
                        if (flash_load(&flash_data)) {
                            flash_data.wiimote_orient_mode = wiimote_orient_mode;
                            flash_save(&flash_data);
                        }
                    }
                }
                // Consume the hotkey buttons so they don't pass through
                buttons &= ~(JP_BUTTON_S2 | JP_BUTTON_DU | JP_BUTTON_DD | JP_BUTTON_DL | JP_BUTTON_DR);
            } else {
                wii->orient_hotkey_active = false;
            }

            // Determine orientation based on mode setting
            // For forced modes, apply immediately without needing accelerometer data
            wiimote_orient_t new_orient = wii->orientation;
            if (wiimote_orient_mode == WII_ORIENT_MODE_HORIZONTAL) {
                new_orient = WII_ORIENT_HORIZONTAL;
            } else if (wiimote_orient_mode == WII_ORIENT_MODE_VERTICAL) {
                new_orient = WII_ORIENT_VERTICAL;
            } else {
                // Auto mode: detect from accelerometer if available
                // Report 0x31: [0]=id, [1-2]=buttons, [3-5]=accel
                // Report 0x35: [0]=id, [1-2]=buttons, [3-5]=accel, [6-21]=extension
                bool has_accel = (report_id == WII_REPORT_BUTTONS_ACC ||
                                  report_id == WII_REPORT_BUTTONS_ACC_EXT16 ||
                                  report_id == WII_REPORT_BUTTONS_ACC_IR ||
                                  report_id == WII_REPORT_BUTTONS_ACC_IR_EXT6);
                if (has_accel && len >= 6) {
                    uint8_t accel_x = data[3];
                    new_orient = wiimote_detect_orientation(accel_x, wii->orientation);
                }
            }

            // Only log orientation changes
            if (new_orient != wii->orientation) {
                printf("[WIIMOTE] Orientation: %s (mode=%d)\n",
                       new_orient == WII_ORIENT_HORIZONTAL ? "HORIZONTAL" : "VERTICAL",
                       wiimote_orient_mode);
                wii->orientation = new_orient;
            }

            // Determine extension data offset based on report type
            const uint8_t* ext = NULL;
            int ext_len = 0;

            if (report_id == WII_REPORT_BUTTONS_EXT8 && len >= 9) {
                // Report 0x32: extension at offset 3 (8 bytes)
                ext = &data[3];
                ext_len = 8;
            } else if (report_id == WII_REPORT_BUTTONS_ACC_EXT16 && len >= 22) {
                // Report 0x35: extension at offset 6 (16 bytes, but we only need first 6)
                ext = &data[6];
                ext_len = 16;
            }

            // Parse extension data
            if (ext != NULL && ext_len >= 6) {

                if (wii->ext_type == WII_EXT_NUNCHUK) {
                    // Nunchuk format (6 bytes):
                    // Byte 0: joystick X (0-255, center ~128)
                    // Byte 1: joystick Y (0-255, center ~128)
                    // Bytes 2-4: accelerometer
                    // Byte 5: buttons (inverted) - bit 0 = Z, bit 1 = C
                    uint8_t ext_buttons = ~ext[5];  // Invert

                    if (ext_buttons & WII_BTN_Z) buttons |= JP_BUTTON_L2;
                    if (ext_buttons & WII_BTN_C) buttons |= JP_BUTTON_L1;

                    wii->event.analog[ANALOG_LX] = ext[0];
                    wii->event.analog[ANALOG_LY] = 255 - ext[1];  // Invert Y

                } else if (wii->ext_type == WII_EXT_CLASSIC) {
                    // Classic Controller format (6 bytes):
                    // Byte 0: RX<4:3>, LX<5:0>
                    // Byte 1: RX<2:1>, LY<5:0>
                    // Byte 2: RX<0>, LT<4:3>, RY<4:0>
                    // Byte 3: LT<2:0>, RT<4:0>
                    // Byte 4-5: Buttons (inverted)
                    uint8_t lx = ext[0] & 0x3F;
                    uint8_t ly = ext[1] & 0x3F;
                    // RX is 5 bits spread across 3 bytes - assemble correctly:
                    // Byte 0 bits 7:6 = RX[4:3], Byte 1 bits 7:6 = RX[2:1], Byte 2 bit 7 = RX[0]
                    uint8_t rx = ((ext[0] >> 3) & 0x18) |  // RX[4:3] -> bits 4:3
                                 ((ext[1] >> 5) & 0x06) |  // RX[2:1] -> bits 2:1
                                 ((ext[2] >> 7) & 0x01);   // RX[0] -> bit 0
                    uint8_t ry = ext[2] & 0x1F;

                    uint8_t lt = ((ext[2] >> 5) & 0x03) | ((ext[3] >> 2) & 0x1C);
                    uint8_t rt = ext[3] & 0x1F;

                    // Scale 6-bit sticks (0-63) to 8-bit (0-255)
                    wii->event.analog[ANALOG_LX] = (lx << 2) | (lx >> 4);
                    wii->event.analog[ANALOG_LY] = 255 - ((ly << 2) | (ly >> 4));  // Invert Y
                    // Scale 5-bit right stick (0-31) to 8-bit
                    wii->event.analog[ANALOG_RX] = (rx << 3) | (rx >> 2);
                    wii->event.analog[ANALOG_RY] = 255 - ((ry << 3) | (ry >> 2));  // Invert Y
                    // Scale 5-bit triggers (0-31) to 8-bit
                    wii->event.analog[ANALOG_L2] = (lt << 3) | (lt >> 2);      // Left trigger
                    wii->event.analog[ANALOG_R2] = (rt << 3) | (rt >> 2);  // Right trigger

                    // Buttons (inverted)
                    uint16_t cc_buttons = ~((ext[4] << 0) | (ext[5] << 8));

                    // Nintendo layout: B=bottom, A=right, Y=left, X=top
                    if (cc_buttons & WII_CC_BTN_B)     buttons |= JP_BUTTON_B1;  // Bottom
                    if (cc_buttons & WII_CC_BTN_A)     buttons |= JP_BUTTON_B2;  // Right
                    if (cc_buttons & WII_CC_BTN_Y)     buttons |= JP_BUTTON_B3;  // Left
                    if (cc_buttons & WII_CC_BTN_X)     buttons |= JP_BUTTON_B4;  // Top
                    if (cc_buttons & WII_CC_BTN_LT)    buttons |= JP_BUTTON_L1;
                    if (cc_buttons & WII_CC_BTN_RT)    buttons |= JP_BUTTON_R1;
                    if (cc_buttons & WII_CC_BTN_ZL)    buttons |= JP_BUTTON_L2;
                    if (cc_buttons & WII_CC_BTN_ZR)    buttons |= JP_BUTTON_R2;
                    if (cc_buttons & WII_CC_BTN_MINUS) buttons |= JP_BUTTON_S1;
                    if (cc_buttons & WII_CC_BTN_PLUS)  buttons |= JP_BUTTON_S2;
                    if (cc_buttons & WII_CC_BTN_HOME)  buttons |= JP_BUTTON_A1;
                    if (cc_buttons & WII_CC_BTN_UP)    buttons |= JP_BUTTON_DU;
                    if (cc_buttons & WII_CC_BTN_DOWN)  buttons |= JP_BUTTON_DD;
                    if (cc_buttons & WII_CC_BTN_LEFT)  buttons |= JP_BUTTON_DL;
                    if (cc_buttons & WII_CC_BTN_RIGHT) buttons |= JP_BUTTON_DR;

                } else if (wii->ext_type == WII_EXT_CLASSIC_MINI) {
                    // NES/SNES Classic Controller - same button format, no analog sticks
                    // Byte 4-5: Buttons (inverted)
                    uint16_t cc_buttons = ~((ext[4] << 0) | (ext[5] << 8));

                    // Nintendo layout: B=bottom, A=right, Y=left, X=top
                    if (cc_buttons & WII_CC_BTN_B)     buttons |= JP_BUTTON_B1;  // Bottom
                    if (cc_buttons & WII_CC_BTN_A)     buttons |= JP_BUTTON_B2;  // Right
                    if (cc_buttons & WII_CC_BTN_Y)     buttons |= JP_BUTTON_B3;  // Left
                    if (cc_buttons & WII_CC_BTN_X)     buttons |= JP_BUTTON_B4;  // Top
                    if (cc_buttons & WII_CC_BTN_LT)    buttons |= JP_BUTTON_L1;
                    if (cc_buttons & WII_CC_BTN_RT)    buttons |= JP_BUTTON_R1;
                    if (cc_buttons & WII_CC_BTN_ZL)    buttons |= JP_BUTTON_L2;
                    if (cc_buttons & WII_CC_BTN_ZR)    buttons |= JP_BUTTON_R2;
                    if (cc_buttons & WII_CC_BTN_MINUS) buttons |= JP_BUTTON_S1;
                    if (cc_buttons & WII_CC_BTN_PLUS)  buttons |= JP_BUTTON_S2;
                    if (cc_buttons & WII_CC_BTN_HOME)  buttons |= JP_BUTTON_A1;
                    if (cc_buttons & WII_CC_BTN_UP)    buttons |= JP_BUTTON_DU;
                    if (cc_buttons & WII_CC_BTN_DOWN)  buttons |= JP_BUTTON_DD;
                    if (cc_buttons & WII_CC_BTN_LEFT)  buttons |= JP_BUTTON_DL;
                    if (cc_buttons & WII_CC_BTN_RIGHT) buttons |= JP_BUTTON_DR;

                } else if (wii->ext_type == WII_EXT_GUITAR) {
                    // Guitar Hero Guitar format (6 bytes):
                    // Byte 0: bits 5:0 = Stick X (6-bit)
                    // Byte 1: bits 5:0 = Stick Y (6-bit)
                    // Byte 2: bits 4:0 = Touch bar
                    // Byte 3: bits 4:0 = Whammy bar
                    // Byte 4-5: Buttons (inverted)
                    uint8_t stick_x = ext[0] & 0x3F;
                    uint8_t stick_y = ext[1] & 0x3F;
                    uint8_t whammy = ext[3] & 0x1F;

                    // Scale 6-bit stick (0-63) to 8-bit (0-255)
                    wii->event.analog[ANALOG_LX] = (stick_x << 2) | (stick_x >> 4);
                    wii->event.analog[ANALOG_LY] = 255 - ((stick_y << 2) | (stick_y >> 4));  // Invert Y
                    // Scale 5-bit whammy (0-31) to 8-bit
                    wii->event.analog[ANALOG_L2] = (whammy << 3) | (whammy >> 2);

                    // Buttons (inverted)
                    uint16_t gh_buttons = ~((ext[4] << 0) | (ext[5] << 8));

                    // Fret buttons -> face buttons + L1
                    if (gh_buttons & GH_BTN_GREEN)      buttons |= JP_BUTTON_B1;  // Green = B1
                    if (gh_buttons & GH_BTN_RED)        buttons |= JP_BUTTON_B2;  // Red = B2
                    if (gh_buttons & GH_BTN_YELLOW)     buttons |= JP_BUTTON_B4;  // Yellow = B4
                    if (gh_buttons & GH_BTN_BLUE)       buttons |= JP_BUTTON_B3;  // Blue = B3
                    if (gh_buttons & GH_BTN_ORANGE)     buttons |= JP_BUTTON_L1;  // Orange = L1
                    // Strum bar -> D-pad
                    if (gh_buttons & GH_BTN_STRUM_UP)   buttons |= JP_BUTTON_DU;
                    if (gh_buttons & GH_BTN_STRUM_DOWN) buttons |= JP_BUTTON_DD;
                    // System buttons
                    if (gh_buttons & GH_BTN_PLUS)       buttons |= JP_BUTTON_S2;  // + = Start
                    if (gh_buttons & GH_BTN_MINUS)      buttons |= JP_BUTTON_S1;  // - = Select

                } else if (wii->extension_connected) {
                    // Debug: unknown extension type
                    static uint32_t last_ext_debug = 0;
                    if (time_us_32() - last_ext_debug > 2000000) {
                        printf("[WIIMOTE] Ext data (unknown type): %02X %02X %02X %02X %02X %02X\n",
                               ext[0], ext[1], ext[2], ext[3], ext[4], ext[5]);
                        last_ext_debug = time_us_32();
                    }
                }
            }

            // Apply control rotation based on orientation for Wiimote-only mode
            // When extension is connected (Nunchuk, CC, etc.), the extension has its own controls
            // so we don't rotate the Wiimote core buttons
            if (wii->ext_type == WII_EXT_NONE && !wii->extension_connected) {
                buttons = wiimote_rotate_controls(buttons, wii->orientation);
            }

            wii->event.buttons = buttons;

            if (wii->state == WII_STATE_READY) {
                router_submit_input(&wii->event);
            }
        }
    }

    // Status report (0x20): [0]=id, [1-2]=buttons, [3]=LF (LED|flags), [4-5]=reserved, [6]=battery
    // Flags in low nibble of byte 3: bit0=battery_low, bit1=extension, bit2=speaker, bit3=IR
    if (report_id == WII_REPORT_STATUS && len >= 4) {
        uint8_t lf_byte = data[3];
        uint8_t flags = lf_byte & 0x0F;  // Low nibble is flags
        bool ext_now = (flags & 0x02) != 0;

        printf("[WIIMOTE] Status: LF=0x%02X flags=0x%X ext=%d\n", lf_byte, flags, ext_now);

        if (wii->state == WII_STATE_WAIT_STATUS) {
            wii->extension_connected = ext_now;
            if (wii->extension_connected) {
                wii->state = WII_STATE_SEND_EXT_INIT1;
            } else {
                wii->state = WII_STATE_SEND_REPORT_MODE;
            }
        }
        // Hot-swap: detect extension change while in READY state
        else if (wii->state == WII_STATE_READY) {
            if (ext_now != wii->extension_connected) {
                printf("[WIIMOTE] Extension %s - re-initializing\n", ext_now ? "connected" : "disconnected");
                wii->extension_connected = ext_now;
                if (ext_now) {
                    // New extension connected - init it
                    wii->ext_type = WII_EXT_NONE;
                    wii->state = WII_STATE_SEND_EXT_INIT1;
                } else {
                    // Extension disconnected - clear type and reset analogs to center
                    wii->ext_type = WII_EXT_NONE;
                    wii->event.analog[ANALOG_LX] = 128;
                    wii->event.analog[ANALOG_LY] = 128;
                    wii->event.analog[ANALOG_RX] = 128;
                    wii->event.analog[ANALOG_RY] = 128;
                    wii->event.analog[ANALOG_L2] = 0;
                    wii->event.analog[ANALOG_R2] = 0;
                    router_submit_input(&wii->event);  // Update output immediately
                    wii->state = WII_STATE_SEND_REPORT_MODE;
                }
            }
        }
    }

    // ACK report (0x22): [0]=id, [1-2]=buttons, [3]=report_acked, [4]=error
    if (report_id == WII_REPORT_ACK && len >= 5) {
        uint8_t acked_report = data[3];
        uint8_t error_code = data[4];

        printf("[WIIMOTE] ACK: report=0x%02X error=%d state=%d\n", acked_report, error_code, wii->state);

        if (error_code == 0) {
            if (wii->state == WII_STATE_WAIT_EXT_INIT1_ACK && acked_report == WII_CMD_WRITE_DATA) {
                wii->state = WII_STATE_SEND_EXT_INIT2;
            } else if (wii->state == WII_STATE_WAIT_EXT_INIT2_ACK && acked_report == WII_CMD_WRITE_DATA) {
                wii->state = WII_STATE_READ_EXT_TYPE;
            } else if (wii->state == WII_STATE_WAIT_REPORT_ACK && acked_report == WII_CMD_REPORT_MODE) {
                wii->state = WII_STATE_SEND_LED;
            } else if (wii->state == WII_STATE_WAIT_LED_ACK && acked_report == WII_CMD_LED) {
                printf("[WIIMOTE] Init complete!\n");
                wii->state = WII_STATE_READY;
                wii->last_keepalive = time_us_32();
            }
        }
    }

    // Read data response (extension type)
    // Report 0x21 format: [0]=report_id, [1-2]=buttons, [3]=SE, [4-5]=addr, [6+]=data
    if (report_id == WII_REPORT_READ_DATA && len >= 7) {
        uint8_t se = data[3];
        uint8_t size = ((se >> 4) & 0x0F) + 1;
        uint8_t error = se & 0x0F;

        printf("[WIIMOTE] Read response: SE=0x%02X size=%d error=%d len=%d\n", se, size, error, len);

        if (wii->state == WII_STATE_WAIT_EXT_TYPE) {
            // Extension type data starts at offset 6
            if (error == 0 && len >= 12) {
                printf("[WIIMOTE] Extension type: %02X %02X %02X %02X %02X %02X\n",
                       data[6], data[7], data[8], data[9], data[10], data[11]);

                // Extension identifiers (after A4 20 signature):
                // Nunchuk:           00 00 A4 20 00 00  (or FF 00 when encrypted)
                // Classic Controller: 00 00 A4 20 01 01  (or FD FD when encrypted)
                // Classic Pro:       01 00 A4 20 01 01
                // NES Classic:       02 00 A4 20 01 01
                // SNES Classic:      03 00 A4 20 01 01
                // Wii U Pro:         00 00 A4 20 01 20
                // Key bytes are A4 20 at positions 2-3 (data[8-9])

                if (data[8] == 0xA4 && data[9] == 0x20) {
                    if (data[10] == 0x00 && data[11] == 0x00) {
                        printf("[WIIMOTE] Nunchuk detected! (encrypted=%d)\n", data[6] == 0xFF);
                        wii->ext_type = WII_EXT_NUNCHUK;
                    }
                    else if (data[10] == 0x01 && data[11] == 0x01) {
                        // Byte 0 distinguishes: 00=CC, 01=CC Pro, 02=NES, 03=SNES
                        if (data[6] >= 0x02) {
                            printf("[WIIMOTE] NES/SNES Classic Controller detected! (type=%02X)\n", data[6]);
                            wii->ext_type = WII_EXT_CLASSIC_MINI;
                        } else {
                            printf("[WIIMOTE] Classic Controller detected! (Pro=%d)\n", data[6] == 0x01);
                            wii->ext_type = WII_EXT_CLASSIC;
                        }
                    }
                    else if (data[10] == 0x01 && data[11] == 0x03) {
                        printf("[WIIMOTE] Guitar Hero Guitar detected!\n");
                        wii->ext_type = WII_EXT_GUITAR;
                    }
                    else if (data[10] == 0x01 && data[11] == 0x20) {
                        printf("[WIIMOTE] Wii U Pro extension detected\n");
                        // Don't set ext_type, this is handled by wii_u_pro driver
                    }
                    else {
                        printf("[WIIMOTE] Unknown extension %02X %02X\n",
                               data[10], data[11]);
                        wii->ext_type = WII_EXT_NONE;
                    }
                }
            } else if (error != 0) {
                printf("[WIIMOTE] Extension read error: %d\n", error);
            }
            wii->state = WII_STATE_SEND_REPORT_MODE;
        }
    }
}

static void wiimote_task(bthid_device_t* device)
{
    wiimote_data_t* wii = (wiimote_data_t*)device->driver_data;
    if (!wii) return;

    uint32_t now = time_us_32();

    switch (wii->state) {
        case WII_STATE_WAIT_INIT:
            if ((int32_t)(now - wii->init_time) >= 0) {
                wii->state = WII_STATE_SEND_STATUS_REQ;
            }
            break;

        case WII_STATE_SEND_STATUS_REQ:
            if (btstack_wiimote_can_send(device->conn_index)) {
                wiimote_request_status(device);
                wii->state = WII_STATE_WAIT_STATUS;
                wii->init_time = now + 1000000;
            }
            break;

        case WII_STATE_WAIT_STATUS:
            if ((int32_t)(now - wii->init_time) >= 0) {
                wii->init_retries++;
                if (wii->init_retries < WIIMOTE_INIT_MAX_RETRIES) {
                    wii->state = WII_STATE_SEND_STATUS_REQ;
                } else {
                    wii->state = WII_STATE_SEND_REPORT_MODE;
                    wii->init_retries = 0;
                }
            }
            break;

        case WII_STATE_SEND_EXT_INIT1:
            if (btstack_wiimote_can_send(device->conn_index)) {
                wiimote_write_data(device, 0xA400F0, 0x55);
                wii->state = WII_STATE_WAIT_EXT_INIT1_ACK;
                wii->init_time = now + 1000000;
            }
            break;

        case WII_STATE_WAIT_EXT_INIT1_ACK:
            if ((int32_t)(now - wii->init_time) >= 0) {
                wii->state = WII_STATE_SEND_EXT_INIT2;
            }
            break;

        case WII_STATE_SEND_EXT_INIT2:
            if (btstack_wiimote_can_send(device->conn_index)) {
                wiimote_write_data(device, 0xA400FB, 0x00);
                wii->state = WII_STATE_WAIT_EXT_INIT2_ACK;
                wii->init_time = now + 1000000;
            }
            break;

        case WII_STATE_WAIT_EXT_INIT2_ACK:
            if ((int32_t)(now - wii->init_time) >= 0) {
                wii->state = WII_STATE_READ_EXT_TYPE;
            }
            break;

        case WII_STATE_READ_EXT_TYPE:
            if (btstack_wiimote_can_send(device->conn_index)) {
                wiimote_read_data(device, 0xA400FA, 6);
                wii->state = WII_STATE_WAIT_EXT_TYPE;
                wii->init_time = now + 1000000;
            }
            break;

        case WII_STATE_WAIT_EXT_TYPE:
            if ((int32_t)(now - wii->init_time) >= 0) {
                wii->state = WII_STATE_SEND_REPORT_MODE;
            }
            break;

        case WII_STATE_SEND_REPORT_MODE:
            if (btstack_wiimote_can_send(device->conn_index)) {
                wiimote_set_report_mode(device, wii->extension_connected);
                wii->state = WII_STATE_WAIT_REPORT_ACK;
                wii->init_time = now + 1000000;
            }
            break;

        case WII_STATE_WAIT_REPORT_ACK:
            if ((int32_t)(now - wii->init_time) >= 0) {
                wii->state = WII_STATE_SEND_LED;
            }
            break;

        case WII_STATE_SEND_LED:
            if (btstack_wiimote_can_send(device->conn_index)) {
                wii->player_led = 0x10;  // LED1 = bit 4
                wiimote_set_leds(device, 1);
                wii->state = WII_STATE_WAIT_LED_ACK;
                wii->init_time = now + 1000000;
            }
            break;

        case WII_STATE_WAIT_LED_ACK:
            if ((int32_t)(now - wii->init_time) >= 0) {
                printf("[WIIMOTE] Init complete (via timeout)\n");
                wii->state = WII_STATE_READY;
                wii->last_keepalive = now;
            }
            break;

        case WII_STATE_READY:
            // Monitor feedback system for LED and rumble changes
            {
                int player_idx = find_player_index(wii->event.dev_addr, wii->event.instance);
                if (player_idx >= 0) {
                    feedback_state_t* fb = feedback_get_state(player_idx);

                    // Check rumble from feedback system
                    if (fb->rumble_dirty) {
                        bool rumble_wanted = (fb->rumble.left > 0 || fb->rumble.right > 0);
                        if (rumble_wanted != wii->rumble_on) {
                            if (btstack_wiimote_can_send(device->conn_index)) {
                                wii->rumble_on = rumble_wanted;
                                wiimote_set_rumble(device, rumble_wanted);
                            }
                        }
                    }

                    // Check LED from feedback system
                    // Feedback pattern: bits 0-3 for players 1-4 (0x01, 0x02, 0x04, 0x08)
                    // Wiimote LED: bits 4-7 for LEDs 1-4 (0x10, 0x20, 0x40, 0x80)
                    // Conversion: shift left by 4
                    uint8_t led;
                    if (fb->led.pattern != 0) {
                        led = fb->led.pattern << 4;
                    } else {
                        led = PLAYER_LEDS[player_idx + 1] << 4;
                    }

                    if (fb->led_dirty || led != wii->player_led) {
                        if (btstack_wiimote_can_send(device->conn_index)) {
                            wii->player_led = led;
                            wiimote_set_leds_raw(device, led);
                        }
                    }

                    // Clear dirty flags after processing
                    if (fb->rumble_dirty || fb->led_dirty) {
                        feedback_clear_dirty(player_idx);
                    }
                }
            }

            // Send periodic status requests to keep connection alive
            if ((int32_t)(now - wii->last_keepalive) >= (WIIMOTE_KEEPALIVE_MS * 1000)) {
                if (btstack_wiimote_can_send(device->conn_index)) {
                    wiimote_request_status(device);
                    wii->last_keepalive = now;
                }
            }
            break;

        default:
            break;
    }
}

static void wiimote_disconnect(bthid_device_t* device)
{
    printf("[WIIMOTE] Disconnect: %s\n", device->name);

    wiimote_data_t* wii = (wiimote_data_t*)device->driver_data;
    if (wii) {
        router_device_disconnected(wii->event.dev_addr, wii->event.instance);
        remove_players_by_address(wii->event.dev_addr, wii->event.instance);
        init_input_event(&wii->event);
        wii->initialized = false;
        wii->state = WII_STATE_IDLE;
    }
}

// ============================================================================
// ORIENTATION MODE API
// ============================================================================

uint8_t wiimote_get_orient_mode(void)
{
    return wiimote_orient_mode;
}

void wiimote_set_orient_mode(uint8_t mode)
{
    if (mode <= WII_ORIENT_MODE_VERTICAL) {
        wiimote_orient_mode = mode;
        printf("[WIIMOTE] Orientation mode set to: %s\n",
               mode == WII_ORIENT_MODE_AUTO ? "AUTO" :
               mode == WII_ORIENT_MODE_HORIZONTAL ? "HORIZONTAL" : "VERTICAL");
    }
}

const char* wiimote_get_orient_mode_name(uint8_t mode)
{
    switch (mode) {
        case WII_ORIENT_MODE_AUTO: return "Auto";
        case WII_ORIENT_MODE_HORIZONTAL: return "Horizontal";
        case WII_ORIENT_MODE_VERTICAL: return "Vertical";
        default: return "Unknown";
    }
}

// ============================================================================
// DRIVER STRUCT
// ============================================================================

const bthid_driver_t wiimote_bt_driver = {
    .name = "Nintendo Wiimote",
    .match = wiimote_match,
    .init = wiimote_init,
    .process_report = wiimote_process_report,
    .task = wiimote_task,
    .disconnect = wiimote_disconnect,
};

void wiimote_bt_register(void)
{
    // Load orientation mode from flash
    flash_t flash_data;
    if (flash_load(&flash_data)) {
        if (flash_data.wiimote_orient_mode <= WII_ORIENT_MODE_VERTICAL) {
            wiimote_orient_mode = flash_data.wiimote_orient_mode;
            printf("[WIIMOTE] Loaded orientation mode from flash: %s\n",
                   wiimote_get_orient_mode_name(wiimote_orient_mode));
        }
    }

    bthid_register_driver(&wiimote_bt_driver);
}
